#!/usr/bin/env python3
"""LAB17: local harm feature audit after LAB16 rejected LAB15.

This is failure analysis only. It reuses the already-consumed LAB16 corpus and
must not select/tune a new candidate. It asks a narrower question than case PSNR:
which zero-fetch features already available to the v02 shader are associated
with pixels where always-on MC is locally worse than v01?

The audit uses signed per-pixel squared-error delta:
    local_error_delta = MSE_rgb(MC, HR) - MSE_rgb(v01, HR)
Positive values mean MC is locally harmful; negative values mean beneficial.
No shader/runtime/main/SOURCE file is changed by this diagnostic.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

import numpy as np
import skimage

import lab_moe_ng_v02_feature_audit as q14
import lab_moe_ng_v02_fresh_validation as q16
import lab_moe_ng_v02_outer_balance as q15
import lab_moe_ng_v02_v01_quality as q10

EPS = 1e-12
EXPECTED_SKIMAGE = "0.26.0"


class Acc:
    def __init__(self):
        self.n = 0
        self.sx = 0.0
        self.sx2 = 0.0
        self.sy = 0.0
        self.sy2 = 0.0
        self.sxy = 0.0
        self.hn = 0
        self.hx = 0.0
        self.bn = 0
        self.bx = 0.0
        self.thn = 0
        self.thx = 0.0
        self.tbn = 0
        self.tbx = 0.0

    def add(self, x, y, harm, benefit, top_harm, top_benefit):
        xf = np.asarray(x, dtype=np.float64).ravel()
        yf = np.asarray(y, dtype=np.float64).ravel()
        self.n += int(xf.size)
        self.sx += float(np.sum(xf, dtype=np.float64))
        self.sx2 += float(np.dot(xf, xf))
        self.sy += float(np.sum(yf, dtype=np.float64))
        self.sy2 += float(np.dot(yf, yf))
        self.sxy += float(np.dot(xf, yf))
        hf = np.asarray(harm, dtype=bool).ravel()
        bf = np.asarray(benefit, dtype=bool).ravel()
        th = np.asarray(top_harm, dtype=bool).ravel()
        tb = np.asarray(top_benefit, dtype=bool).ravel()
        if np.any(hf):
            self.hn += int(np.sum(hf)); self.hx += float(np.sum(xf[hf], dtype=np.float64))
        if np.any(bf):
            self.bn += int(np.sum(bf)); self.bx += float(np.sum(xf[bf], dtype=np.float64))
        if np.any(th):
            self.thn += int(np.sum(th)); self.thx += float(np.sum(xf[th], dtype=np.float64))
        if np.any(tb):
            self.tbn += int(np.sum(tb)); self.tbx += float(np.sum(xf[tb], dtype=np.float64))

    def result(self):
        if self.n <= 0:
            return {"pixels": 0}
        mx = self.sx / self.n
        my = self.sy / self.n
        vx = max(0.0, self.sx2 / self.n - mx * mx)
        vy = max(0.0, self.sy2 / self.n - my * my)
        cov = self.sxy / self.n - mx * my
        corr = cov / math.sqrt(vx * vy) if vx > EPS and vy > EPS else 0.0
        hm = self.hx / self.hn if self.hn else float("nan")
        bm = self.bx / self.bn if self.bn else float("nan")
        sep = (hm - bm) / math.sqrt(vx) if self.hn and self.bn and vx > EPS else 0.0
        return {
            "pixels": self.n,
            "feature_mean": mx,
            "feature_std": math.sqrt(vx),
            "harm_feature_mean": hm,
            "benefit_feature_mean": bm,
            "harm_minus_benefit_sd": sep,
            "corr_with_local_error_delta": corr,
            "top_harm_feature_mean": self.thx / self.thn if self.thn else float("nan"),
            "top_benefit_feature_mean": self.tbx / self.tbn if self.tbn else float("nan"),
            "harm_pixels": self.hn,
            "benefit_pixels": self.bn,
        }


def psnr(a, b):
    d = np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64)
    mse = float(np.mean(d * d, dtype=np.float64))
    return float("inf") if mse <= 0.0 else 10.0 * math.log10(1.0 / mse)


def fixed_features(lr):
    f = q14.features(lr)
    cross = (np.float32(1.0) - f["coh"]).astype(np.float32)
    derived = {
        "crossness": cross,
        "cross_range": (cross * f["range"]).astype(np.float32),
        "balance_cross": (f["outer_balance"] * cross).astype(np.float32),
        "balance_cross_range": (f["outer_balance"] * cross * f["range"]).astype(np.float32),
        "line_range": (f["coh"] * f["range"]).astype(np.float32),
    }
    return {**f, **derived}


def masks(delta):
    harm = delta > 0.0
    benefit = delta < 0.0
    top_harm = np.zeros(delta.shape, dtype=bool)
    top_benefit = np.zeros(delta.shape, dtype=bool)
    if np.any(harm):
        th = float(np.quantile(delta[harm], 0.90))
        top_harm = harm & (delta >= th)
    if np.any(benefit):
        tb = float(np.quantile(delta[benefit], 0.10))
        top_benefit = benefit & (delta <= tb)
    return harm, benefit, top_harm, top_benefit


def mean_mask(x, m):
    return float(np.mean(np.asarray(x, dtype=np.float64)[m], dtype=np.float64)) if np.any(m) else float("nan")


def write_csv(path, rows):
    fields = []
    seen = set()
    for r in rows:
        for k in r:
            if k not in seen:
                seen.add(k); fields.append(k)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader(); w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frozen", default="lab_protocols/LAB15_FROZEN_CANDIDATE.json")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)

    if skimage.__version__ != EXPECTED_SKIMAGE:
        raise RuntimeError(f"LAB17 requires scikit-image {EXPECTED_SKIMAGE}, got {skimage.__version__}")
    frozen, frozen_sha = q16.validate_frozen(Path(a.frozen))
    rejected_spec = frozen["candidate"]

    scopes = defaultdict(lambda: defaultdict(Acc))
    case_feature_rows = []
    case_rows = []

    for family, group, fn in q16.GENERATORS:
        for i in range(6):
            case_id = f"LAB16-{family.upper().replace('FRESH_','')}-{i+1:02d}"
            hr = q16.clip01(fn(i))
            lr = q16.downsample(hr)
            v01, mc = q10.reconstruct_pair(lr)
            e01 = np.mean((v01 - hr) ** 2, axis=2, dtype=np.float32)
            emc = np.mean((mc - hr) ** 2, axis=2, dtype=np.float32)
            delta = (emc - e01).astype(np.float32)
            harm, benefit, top_harm, top_benefit = masks(delta)
            feat = fixed_features(lr)
            rejected_gate = q15.gate(q15.features15(lr), rejected_spec)[..., 0]

            p01 = psnr(v01, hr); pmc = psnr(mc, hr)
            cr = {
                "case_id": case_id, "family": family, "group": group,
                "v01_psnr_db": p01, "mc_psnr_db": pmc, "mc_minus_v01_psnr_db": pmc - p01,
                "mean_local_error_delta": float(np.mean(delta, dtype=np.float64)),
                "harm_pixel_fraction": float(np.mean(harm, dtype=np.float64)),
                "benefit_pixel_fraction": float(np.mean(benefit, dtype=np.float64)),
                "rejected_gate_mean": float(np.mean(rejected_gate, dtype=np.float64)),
                "rejected_gate_on_harm": mean_mask(rejected_gate, harm),
                "rejected_gate_on_top_harm": mean_mask(rejected_gate, top_harm),
                "rejected_gate_on_benefit": mean_mask(rejected_gate, benefit),
            }
            case_rows.append(cr)

            for name, x in feat.items():
                local = Acc(); local.add(x, delta, harm, benefit, top_harm, top_benefit)
                rr = {"case_id": case_id, "family": family, "group": group, "feature": name, **local.result()}
                case_feature_rows.append(rr)
                for scope in ("ALL", f"GROUP:{group}", f"FAMILY:{family}"):
                    scopes[scope][name].add(x, delta, harm, benefit, top_harm, top_benefit)
            print(case_id, f"MC-v01={pmc-p01:+.6f} dB", f"harm={cr['harm_pixel_fraction']:.4f}")

    agg_rows = []
    for scope in sorted(scopes):
        for name in sorted(scopes[scope]):
            agg_rows.append({"scope": scope, "feature": name, **scopes[scope][name].result()})

    struct = [r for r in agg_rows if r["scope"] == "GROUP:structural"]
    texture = [r for r in agg_rows if r["scope"] == "GROUP:texture"]
    key = lambda r: abs(float(r.get("harm_minus_benefit_sd", 0.0)))
    summary = {
        "protocol": "LAB17_LOCAL_HARM_FEATURE_AUDIT",
        "status": "DIAGNOSTIC_ONLY",
        "selection_or_tuning": False,
        "consumed_dataset": "LAB16_POST_FREEZE_FRESH_VALIDATION",
        "lab15_candidate_status": "REJECTED",
        "frozen_candidate_file_sha256": frozen_sha,
        "texture_fetch_delta": 0,
        "shader_changed": False,
        "case_count": len(case_rows),
        "feature_count": len(scopes["ALL"]),
        "local_error_definition": "mean_rgb((MC-HR)^2) - mean_rgb((v01-HR)^2); positive means MC locally harms",
        "top_structural_separators": sorted(struct, key=key, reverse=True)[:10],
        "top_texture_separators": sorted(texture, key=key, reverse=True)[:10],
        "next_step_policy": "LAB17 may inform architecture only. Any new candidate must be selected/frozen separately and validated on a new post-freeze corpus not authored before that freeze.",
    }

    write_csv(out / "CASE_ERROR_AUDIT.csv", case_rows)
    write_csv(out / "CASE_FEATURE_AUDIT.csv", case_feature_rows)
    write_csv(out / "AGGREGATE_FEATURE_AUDIT.csv", agg_rows)
    (out / "SUMMARY.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
