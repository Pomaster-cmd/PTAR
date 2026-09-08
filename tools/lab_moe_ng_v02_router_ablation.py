#!/usr/bin/env python3
"""LAB06: PTAR-NG MoE v02 router ablation.

Question: after LAB05-MC boundary expansion converged to rel_hi=0.08,
abs_lo=0.0, abs_hi=0.005, strength=1.0, is the curvature router still useful,
or should the shape-preserving MC Hermite expert simply run whenever the
interpolated phase can use it?

Protocol:
  * selection uses crop tags A+B only from BOTH B-GRID and V1_A;
  * crop C is untouched until one architecture is selected;
  * holdout C is evaluated only for the selected architecture.

The candidate set is deliberately small and architectural:
  routed_selected : LAB05 best routed configuration
  abs_only        : keep only the absolute-curvature gate
  rel_only        : keep only the relative-curvature gate
  always_on       : remove the router entirely

No strength > 1.0 is evaluated; that would extrapolate beyond the bounded MC
expert rather than test routing architecture.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_hermite_sweep as h5


CANDIDATES = (
    "routed_selected",
    "abs_only",
    "rel_only",
    "always_on",
)


def gate(case, name):
    ac = case["ac"]
    rc = case["rc"]
    if name == "routed_selected":
        return (h5.sat(rc / np.float32(0.08)) * h5.sat(ac / np.float32(0.005))).astype(np.float32)
    if name == "abs_only":
        return h5.sat(ac / np.float32(0.005)).astype(np.float32)
    if name == "rel_only":
        return h5.sat(rc / np.float32(0.08)).astype(np.float32)
    if name == "always_on":
        return np.ones_like(ac, dtype=np.float32)
    raise ValueError(name)


def eval_candidate(cases, name):
    rows = []
    for case in cases:
        g = gate(case, name)
        d = case["delta"]["mc"]
        err = case["err"] + d * g[:, :, None]
        mse = np.mean(err * err, dtype=np.float64)
        pv = h5.psnr_mse(mse)
        rows.append({
            "protocol": case["protocol"],
            "case_id": case["case_id"],
            "family": case["family"],
            "crop_tag": case["crop_tag"],
            "bilinear_psnr": case["pb"],
            "v02_psnr": pv,
            "gap_bilinear": pv - case["pb"],
            "gate_mean": float(np.mean(g, dtype=np.float64)),
        })

    bg = [r for r in rows if r["protocol"] == "B_GRID"]
    va = [r for r in rows if r["protocol"] == "V1_A"]
    struct = [r for r in bg if r["family"] in h5.STRUCT_FAMILIES]

    def mean(xs, key):
        return sum(x[key] for x in xs) / len(xs)

    summary = {
        "candidate": name,
        "mean_psnr": mean(rows, "v02_psnr"),
        "mean_gap": mean(rows, "gap_bilinear"),
        "worst_gap": min(r["gap_bilinear"] for r in rows),
        "bg_struct_mean_gap": mean(struct, "gap_bilinear"),
        "bg_mean_gap": mean(bg, "gap_bilinear"),
        "v1a_mean_gap": mean(va, "gap_bilinear"),
        "bg_worst_gap": min(r["gap_bilinear"] for r in bg),
        "v1a_worst_gap": min(r["gap_bilinear"] for r in va),
        "better_bilinear": sum(r["gap_bilinear"] > 0.0 for r in rows),
        "mean_gate": mean(rows, "gate_mean"),
    }
    summary["score"] = (
        summary["mean_psnr"]
        - 12.0 * max(0.0, -0.15 - summary["worst_gap"])
        - 4.0 * max(0.0, 0.20 - summary["bg_struct_mean_gap"])
    )
    return rows, summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bgrid", required=True)
    ap.add_argument("--v1a", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    train = []
    hold = []
    for proto, path_s in (("B_GRID", args.bgrid), ("V1_A", args.v1a)):
        path = Path(path_s)
        manifest = list(csv.DictReader((path / "CORPUS_MANIFEST.csv").open(newline="", encoding="utf-8")))
        for n, row in enumerate(manifest, 1):
            c = h5.prep(path, row, proto)
            (hold if row.get("crop_tag") == "C" else train).append(c)
            print(f"{proto} {n:02d}/{len(manifest)} {row['case_id']}")

    train_results = []
    train_rows_by_candidate = {}
    for name in CANDIDATES:
        rows, summary = eval_candidate(train, name)
        train_rows_by_candidate[name] = rows
        train_results.append(summary)

    feasible = [
        r for r in train_results
        if r["worst_gap"] >= -0.15 and r["bg_struct_mean_gap"] >= 0.20
    ]
    selected = max(feasible, key=lambda r: r["mean_psnr"]) if feasible else max(train_results, key=lambda r: r["score"])
    selected_name = selected["candidate"]

    hold_rows, hold_summary = eval_candidate(hold, selected_name)
    train_results.sort(key=lambda r: r["score"], reverse=True)

    with (out / "TRAIN_ARCHITECTURE_SWEEP.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(train_results[0]))
        w.writeheader()
        w.writerows(train_results)

    for filename, rows in (
        ("TRAIN_SELECTED_PER_CASE.csv", train_rows_by_candidate[selected_name]),
        ("HOLDOUT_C_SELECTED_PER_CASE.csv", hold_rows),
    ):
        with (out / filename).open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)

    result = {
        "protocol": "LAB06_ROUTER_ABLATION_AB_TRAIN_C_HOLDOUT",
        "selection_used_holdout": False,
        "candidate_order": list(CANDIDATES),
        "feasible_count": len(feasible),
        "selected": selected,
        "train_candidates": train_results,
        "holdout_C_selected": hold_summary,
        "decision": {
            "router_required": selected_name != "always_on",
            "selected_architecture": selected_name,
        },
    }
    (out / "SUMMARY.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
