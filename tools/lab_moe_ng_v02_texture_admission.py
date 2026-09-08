#!/usr/bin/env python3
"""LAB18: conservative texture-admission router search after LAB17.

LAB16/LAB17 showed that always-on MC is strongly beneficial on fresh
texture/frequency probes but can damage structural junctions and sparse glyph
pixels. LAB18 therefore reverses the previous routing philosophy: v01 is the
safe base and MC is admitted only in regions that look like supported texture.

Candidate selection is A+B ONLY across B_GRID and V1_A. Reused C and the
already-consumed LAB16 set are diagnostics/architecture screens only; neither
can participate in parameter ranking. All features come from the existing 2x2
gather and fm1/f0/f1/f2 samples, so texture-fetch delta remains zero.
"""
from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_feature_audit as q14
import lab_moe_ng_v02_fresh_validation as q16
import lab_moe_ng_v02_hermite_sweep as h5
import lab_moe_ng_v02_step_support as q13
import lab_moe_ng_v02_v01_quality as q10

EPS = np.float32(1e-6)

# Precommitted before the first LAB18 run. LAB16 is consumed and is therefore
# an architecture screen, not independent validation.
LAB16_SCREEN_GATES = {
    "overall_mean_gap_v01_db_min": 0.08,
    "overall_worst_gap_v01_db_min": -0.15,
    "structural_mean_gap_v01_db_min": -0.02,
    "structural_worst_gap_v01_db_min": -0.15,
    "texture_mean_gap_v01_db_min": 0.20,
}


def psnr(a, b):
    d = np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64)
    mse = float(np.mean(d * d, dtype=np.float64))
    return float("inf") if mse <= 0.0 else 10.0 * math.log10(1.0 / mse)


def sat(x):
    return np.clip(x, np.float32(0), np.float32(1))


def low(x, t, w):
    return np.float32(1) - sat((x - np.float32(t)) / np.float32(max(w, 1e-6)))


def high(x, t, w):
    return sat((x - np.float32(t)) / np.float32(max(w, 1e-6)))


def gate(feat, spec):
    # Core hypothesis from LAB17: low/moderate coherence admits the texture
    # families while rejecting the near-1.0-coherence junction failure mode.
    g = low(feat["coh"], spec["coh_t"], spec["coh_w"])
    kind = spec["kind"]
    if kind in ("lc_bal", "lc_bal_central", "lc_bal_noturn"):
        g = g * high(feat["outer_balance"], spec["bal_t"], spec["bal_w"])
    if kind in ("lc_central", "lc_bal_central"):
        g = g * high(feat["central"], spec["central_t"], spec["central_w"])
    if kind == "lc_bal_noturn":
        g = g * (np.float32(1) - feat["turn"])
    return (g * np.float32(spec["strength"]))[..., None].astype(np.float32)


def candidates():
    out = []
    coh_t = [0.40, 0.50, 0.60, 0.70, 0.80]
    coh_w = [0.12, 0.22]
    strengths = [0.50, 0.75, 1.00]
    bal_t = [0.05, 0.15, 0.25, 0.35]
    central_t = [0.08, 0.16, 0.24, 0.32]

    for ct, cw, s in itertools.product(coh_t, coh_w, strengths):
        out.append({"kind": "lc", "coh_t": ct, "coh_w": cw, "strength": s})
    for ct, cw, bt, s in itertools.product(coh_t, coh_w, bal_t, strengths):
        out.append({"kind": "lc_bal", "coh_t": ct, "coh_w": cw,
                    "bal_t": bt, "bal_w": 0.15, "strength": s})
        out.append({"kind": "lc_bal_noturn", "coh_t": ct, "coh_w": cw,
                    "bal_t": bt, "bal_w": 0.15, "strength": s})
    for ct, cw, st, s in itertools.product(coh_t, coh_w, central_t, strengths):
        out.append({"kind": "lc_central", "coh_t": ct, "coh_w": cw,
                    "central_t": st, "central_w": 0.15, "strength": s})
    # Tighter conjunction; keep a smaller grid to avoid a broad brute-force
    # parameter fishing exercise.
    for ct, bt, st, s in itertools.product(
        [0.45, 0.55, 0.65, 0.75], [0.10, 0.20, 0.30],
        [0.10, 0.20, 0.30], strengths
    ):
        out.append({"kind": "lc_bal_central", "coh_t": ct, "coh_w": 0.18,
                    "bal_t": bt, "bal_w": 0.15,
                    "central_t": st, "central_w": 0.15, "strength": s})
    return out


def prepare(corpus, row, proto):
    lr = h5.load(corpus / row["input_path"])
    ref = h5.load(corpus / row["reference_path"])
    v01, mc = q10.reconstruct_pair(lr)
    return {
        "protocol": proto,
        "case_id": row["case_id"],
        "source_id": row.get("source_id", ""),
        "family": row["family"],
        "crop_tag": row.get("crop_tag", ""),
        "ref": ref,
        "v01": v01,
        "v02": mc,
        "feat": q14.features(lr),
        "p01": psnr(v01, ref),
        "p02": psnr(mc, ref),
    }


def evaluate(cases, spec):
    rows = []
    for c in cases:
        g = gate(c["feat"], spec)
        img = np.clip(c["v01"] + (c["v02"] - c["v01"]) * g,
                      np.float32(0), np.float32(1))
        p = psnr(img, c["ref"])
        rows.append({
            "protocol": c["protocol"], "case_id": c["case_id"],
            "source_id": c["source_id"], "family": c["family"],
            "crop_tag": c["crop_tag"], "v01_psnr": c["p01"],
            "v02_psnr": c["p02"], "hybrid_psnr": p,
            "gap_v01": p - c["p01"], "gap_v02": p - c["p02"],
            "gate_mean": float(np.mean(g, dtype=np.float64)),
        })
    return rows


def prepare_lab16():
    cases = []
    for family, group, fn in q16.GENERATORS:
        for i in range(6):
            hr = q16.clip01(fn(i))
            lr = q16.downsample(hr)
            v01, mc = q10.reconstruct_pair(lr)
            cases.append({
                "protocol": "LAB16_CONSUMED_DIAGNOSTIC",
                "case_id": f"LAB16-{family.upper().replace('FRESH_','')}-{i+1:02d}",
                "source_id": group,
                "family": family,
                "crop_tag": "CONSUMED",
                "ref": hr, "v01": v01, "v02": mc,
                "feat": q14.features(lr),
                "p01": psnr(v01, hr), "p02": psnr(mc, hr),
            })
    return cases


def lab16_block(rows):
    all_g = [r["gap_v01"] for r in rows]
    st = [r["gap_v01"] for r in rows if r["source_id"] == "structural"]
    tx = [r["gap_v01"] for r in rows if r["source_id"] == "texture"]
    return {
        "cases": len(rows),
        "mean_gap_v01": float(np.mean(all_g)),
        "worst_gap_v01": float(min(all_g)),
        "wins_v01": int(sum(x > 0 for x in all_g)),
        "structural_mean_gap_v01": float(np.mean(st)),
        "structural_worst_gap_v01": float(min(st)),
        "texture_mean_gap_v01": float(np.mean(tx)),
        "texture_worst_gap_v01": float(min(tx)),
        "mean_gate": float(np.mean([r["gate_mean"] for r in rows])),
    }


def screen_checks(b):
    g = LAB16_SCREEN_GATES
    return {
        "overall_mean": b["mean_gap_v01"] >= g["overall_mean_gap_v01_db_min"],
        "overall_worst": b["worst_gap_v01"] >= g["overall_worst_gap_v01_db_min"],
        "structural_mean": b["structural_mean_gap_v01"] >= g["structural_mean_gap_v01_db_min"],
        "structural_worst": b["structural_worst_gap_v01"] >= g["structural_worst_gap_v01_db_min"],
        "texture_mean": b["texture_mean_gap_v01"] >= g["texture_mean_gap_v01_db_min"],
    }


def write_csv(path, rows):
    keys = []
    seen = set()
    for r in rows:
        for k in r:
            if k not in seen:
                seen.add(k); keys.append(k)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=keys); w.writeheader(); w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bgrid", required=True)
    ap.add_argument("--v1a", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)

    train = []
    diag_c = []
    for proto, p in [("B_GRID", Path(a.bgrid)), ("V1_A", Path(a.v1a))]:
        rows = list(csv.DictReader((p / "CORPUS_MANIFEST.csv").open(newline="", encoding="utf-8")))
        for i, row in enumerate(rows, 1):
            c = prepare(p, row, proto)
            (diag_c if row.get("crop_tag") == "C" else train).append(c)
            print(proto, i, len(rows), row["case_id"])

    specs = candidates()
    ranked = []
    for i, spec in enumerate(specs, 1):
        ranked.append(q13.summarize_train(evaluate(train, spec), spec))
        if i % 100 == 0:
            print("candidates", i, "/", len(specs))
    feasible = [r for r in ranked if r["feasible"]]
    selected = max(feasible, key=lambda r: (r["score"], r["all"]["mean_gap_v01"], -r["all"]["mean_gate"])) if feasible else max(ranked, key=lambda r: r["score"])

    selected_train = evaluate(train, selected["spec"])
    c_rows = evaluate(diag_c, selected["spec"])
    lab16_rows = evaluate(prepare_lab16(), selected["spec"])
    lab16_diag = lab16_block(lab16_rows)
    checks = screen_checks(lab16_diag)

    ranked.sort(key=lambda r: r["score"], reverse=True)
    write_csv(out / "TRAIN_SELECTED_PER_CASE.csv", selected_train)
    write_csv(out / "REUSED_C_DIAGNOSTIC_PER_CASE.csv", c_rows)
    write_csv(out / "CONSUMED_LAB16_ARCHITECTURE_SCREEN.csv", lab16_rows)

    flat = []
    for r in ranked:
        z = {"score": r["score"], "feasible": r["feasible"], **r["spec"]}
        for name in ["all", "A", "B", "B_GRID", "V1_A"]:
            for k, v in r[name].items():
                z[f"{name}_{k}"] = v
        flat.append(z)
    write_csv(out / "SWEEP.csv", flat)

    result = {
        "protocol": "LAB18_TEXTURE_ADMISSION_AB_SELECTION",
        "selection_used_C": False,
        "selection_used_LAB16": False,
        "LAB16_is_consumed_diagnostic": True,
        "texture_fetch_delta": 0,
        "candidate_count": len(specs),
        "feasible_count": len(feasible),
        "selected_train": selected,
        "reused_C_diagnostic": q13.block(c_rows),
        "consumed_LAB16_architecture_screen": lab16_diag,
        "precommitted_LAB16_screen_gates": LAB16_SCREEN_GATES,
        "LAB16_screen_checks": checks,
        "architecture_screen_pass": all(checks.values()),
        "freeze_allowed_by_this_run": bool(selected["feasible"] and all(checks.values())),
        "top": ranked[:40],
        "next_step_policy": "If freeze_allowed_by_this_run is true, freeze the selected candidate unchanged before authoring a new post-freeze validation corpus. LAB16/C remain diagnostics only.",
    }
    (out / "SUMMARY.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
