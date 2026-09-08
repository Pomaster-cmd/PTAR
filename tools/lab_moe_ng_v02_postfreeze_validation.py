#!/usr/bin/env python3
"""LAB19: first independent post-freeze validation of LAB18.

The LAB18 candidate was frozen before both this harness and the procedural case
module were authored. This program contains no sweep or parameter search. It
validates the frozen candidate on 36 deterministic fresh cases, applies gates
precommitted in LAB19_FRESH_VALIDATION_PROTOCOL.json, writes complete evidence,
and exits 2 when any acceptance gate fails.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

import numpy as np
import skimage

import lab_moe_ng_v02_feature_audit as q14
import lab_moe_ng_v02_postfreeze_cases as cases19
import lab_moe_ng_v02_v01_quality as q10

EXPECTED_SKIMAGE = "0.26.0"
FROZEN_COMMIT = "bda10f16ad287eaac5fabba58e88aedc2651dd34"
FROZEN_EXPECTED = {
    "kind": "lc_bal_central",
    "coh_t": 0.75,
    "coh_w": 0.18,
    "bal_t": 0.10,
    "bal_w": 0.15,
    "central_t": 0.10,
    "central_w": 0.15,
    "strength": 0.50,
}
GATES_EXPECTED = {
    "overall_mean_gap_v01_db_min": 0.08,
    "overall_worst_gap_v01_db_min": -0.15,
    "overall_wins_v01_min": 24,
    "structural_mean_gap_v01_db_min": -0.02,
    "structural_worst_gap_v01_db_min": -0.15,
    "texture_mean_gap_v01_db_min": 0.15,
    "texture_worst_gap_v01_db_min": -0.05,
    "worst_family_mean_gap_v01_db_min": -0.05,
}


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def array_sha(a):
    x = np.ascontiguousarray(np.asarray(a, dtype=np.float32))
    return sha256_bytes(x.tobytes(order="C"))


def file_sha(path):
    return sha256_bytes(Path(path).read_bytes())


def psnr(a, b):
    d = np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64)
    mse = float(np.mean(d * d, dtype=np.float64))
    return float("inf") if mse <= 0.0 else 10.0 * math.log10(1.0 / mse)


def sat(x):
    return np.clip(x, np.float32(0), np.float32(1))


def frozen_gate(feat, spec):
    lc = np.float32(1) - sat(
        (feat["coh"] - np.float32(spec["coh_t"])) / np.float32(spec["coh_w"])
    )
    bal = sat(
        (feat["outer_balance"] - np.float32(spec["bal_t"])) / np.float32(spec["bal_w"])
    )
    central = sat(
        (feat["central"] - np.float32(spec["central_t"])) / np.float32(spec["central_w"])
    )
    return (lc * bal * central * np.float32(spec["strength"]))[..., None].astype(np.float32)


def validate_inputs(frozen_path, protocol_path):
    if skimage.__version__ != EXPECTED_SKIMAGE:
        raise RuntimeError(f"LAB19 requires scikit-image {EXPECTED_SKIMAGE}, got {skimage.__version__}")
    frozen = json.loads(Path(frozen_path).read_text(encoding="utf-8"))
    protocol = json.loads(Path(protocol_path).read_text(encoding="utf-8"))
    if frozen.get("protocol") != "LAB18_FROZEN_TEXTURE_ADMISSION_CANDIDATE":
        raise RuntimeError("wrong frozen protocol")
    if frozen.get("status") != "FROZEN_FOR_NEXT_INDEPENDENT_VALIDATION":
        raise RuntimeError("candidate is not frozen for validation")
    if frozen.get("candidate") != FROZEN_EXPECTED:
        raise RuntimeError(f"frozen candidate mismatch: {frozen.get('candidate')}")
    if protocol.get("protocol") != "LAB19_POST_FREEZE_FRESH_VALIDATION":
        raise RuntimeError("wrong LAB19 protocol file")
    if protocol.get("frozen_candidate_commit") != FROZEN_COMMIT:
        raise RuntimeError("LAB19 protocol does not point to the LAB18 freeze commit")
    if protocol.get("precommitted_gates") != GATES_EXPECTED:
        raise RuntimeError("LAB19 precommitted gates mismatch")
    if protocol.get("uses_reused_C") is not False or protocol.get("uses_LAB16_cases") is not False:
        raise RuntimeError("LAB19 must not reuse C or LAB16 cases")
    return frozen, protocol


def block(rows):
    gaps = [float(r["gap_v01_db"]) for r in rows]
    return {
        "cases": len(rows),
        "mean_gap_v01_db": float(np.mean(gaps, dtype=np.float64)),
        "worst_gap_v01_db": float(min(gaps)),
        "best_gap_v01_db": float(max(gaps)),
        "wins_v01": int(sum(g > 0.0 for g in gaps)),
        "mean_gate": float(np.mean([float(r["gate_mean"]) for r in rows], dtype=np.float64)),
    }


def write_csv(path, rows):
    with Path(path).open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frozen", required=True)
    ap.add_argument("--protocol", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)

    frozen, protocol = validate_inputs(a.frozen, a.protocol)
    spec = frozen["candidate"]
    rows = []
    hashes = []

    for family, group, fn in cases19.GENERATORS:
        for i in range(6):
            case_id = f"LAB19-{family.upper().replace('POSTFREEZE_','')}-{i+1:02d}"
            hr = cases19.clip01(fn(i))
            if hr.shape != (cases19.HR_H, cases19.HR_W, 3) or not np.all(np.isfinite(hr)):
                raise RuntimeError(f"invalid HR case {case_id}")
            lr = cases19.downsample(hr)
            if lr.shape != (cases19.LR_H, cases19.LR_W, 3) or not np.all(np.isfinite(lr)):
                raise RuntimeError(f"invalid LR case {case_id}")

            v01, mc = q10.reconstruct_pair(lr)
            feat = q14.features(lr)
            g = frozen_gate(feat, spec)
            candidate = np.clip(v01 + (mc - v01) * g, np.float32(0), np.float32(1)).astype(np.float32)
            if not np.all(np.isfinite(candidate)):
                raise RuntimeError(f"non-finite candidate output {case_id}")

            p01 = psnr(v01, hr)
            pmc = psnr(mc, hr)
            pc = psnr(candidate, hr)
            rec = {
                "case_id": case_id,
                "family": family,
                "group": group,
                "v01_psnr_db": p01,
                "mc_psnr_db": pmc,
                "candidate_psnr_db": pc,
                "gap_v01_db": pc - p01,
                "gap_mc_db": pc - pmc,
                "gate_mean": float(np.mean(g, dtype=np.float64)),
                "gate_q90": float(np.quantile(g, 0.90)),
                "output_min": float(np.min(candidate)),
                "output_max": float(np.max(candidate)),
            }
            rows.append(rec)
            hashes.append({
                "case_id": case_id,
                "hr_float32_sha256": array_sha(hr),
                "lr_float32_sha256": array_sha(lr),
                "v01_float32_sha256": array_sha(v01),
                "mc_float32_sha256": array_sha(mc),
                "candidate_float32_sha256": array_sha(candidate),
            })
            print(case_id, f"gap={pc-p01:+.6f} dB", f"gate={rec['gate_mean']:.6f}")

    structural = [r for r in rows if r["group"] == "structural"]
    texture = [r for r in rows if r["group"] == "texture"]
    all_block = block(rows)
    structural_block = block(structural)
    texture_block = block(texture)
    family_blocks = {f: block([r for r in rows if r["family"] == f]) for f, _, _ in cases19.GENERATORS}
    worst_family_mean = min(v["mean_gap_v01_db"] for v in family_blocks.values())

    checks = {
        "overall_mean": all_block["mean_gap_v01_db"] >= GATES_EXPECTED["overall_mean_gap_v01_db_min"],
        "overall_worst": all_block["worst_gap_v01_db"] >= GATES_EXPECTED["overall_worst_gap_v01_db_min"],
        "overall_wins": all_block["wins_v01"] >= GATES_EXPECTED["overall_wins_v01_min"],
        "structural_mean": structural_block["mean_gap_v01_db"] >= GATES_EXPECTED["structural_mean_gap_v01_db_min"],
        "structural_worst": structural_block["worst_gap_v01_db"] >= GATES_EXPECTED["structural_worst_gap_v01_db_min"],
        "texture_mean": texture_block["mean_gap_v01_db"] >= GATES_EXPECTED["texture_mean_gap_v01_db_min"],
        "texture_worst": texture_block["worst_gap_v01_db"] >= GATES_EXPECTED["texture_worst_gap_v01_db_min"],
        "worst_family_mean": worst_family_mean >= GATES_EXPECTED["worst_family_mean_gap_v01_db_min"],
    }
    status = "PASS" if all(checks.values()) else "FAIL"

    summary = {
        "protocol": "LAB19_POST_FREEZE_FRESH_VALIDATION",
        "status": status,
        "validation_not_tuning": True,
        "fresh_generator_authored_after_freeze": True,
        "frozen_candidate_commit": FROZEN_COMMIT,
        "frozen_candidate_file_sha256": file_sha(a.frozen),
        "protocol_file_sha256": file_sha(a.protocol),
        "candidate": spec,
        "old_C_used": False,
        "LAB16_cases_used": False,
        "case_count": len(rows),
        "scale": cases19.SCALE,
        "hr_size": [cases19.HR_H, cases19.HR_W],
        "lr_size": [cases19.LR_H, cases19.LR_W],
        "seed": cases19.SEED,
        "input_generation": {
            "library": "scikit-image",
            "version": skimage.__version__,
            "function": "skimage.transform.resize",
            "order": 3,
            "mode": "reflect",
            "anti_aliasing": True,
            "preserve_range": True,
            "clip": True,
            "domain": "sRGB/code values (not linearized)",
        },
        "precommitted_gates": GATES_EXPECTED,
        "checks": checks,
        "all": all_block,
        "structural": structural_block,
        "texture": texture_block,
        "worst_family_mean_gap_v01_db": worst_family_mean,
        "family_blocks": family_blocks,
        "post_observation_rule": "Do not tune LAB18 parameters, LAB19 thresholds, or LAB19 generators against this result. Failure rejects the frozen candidate; success only permits subsequent software/runtime gates.",
    }

    write_csv(out / "PER_CASE.csv", rows)
    write_csv(out / "CASE_HASHES.csv", hashes)
    (out / "SUMMARY.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    raise SystemExit(0 if status == "PASS" else 2)


if __name__ == "__main__":
    main()
