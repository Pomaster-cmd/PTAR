#!/usr/bin/env python3
"""LAB07: cost-aware architecture selection for PTAR-NG MoE v02.

LAB06 selected the relative-curvature router by raw A+B mean PSNR, but its
advantage over always-on MC was around one millionth of a dB. LAB07 makes the
selection rule explicit before consulting crop C: candidates whose A+B quality
is numerically equivalent within 1e-4 dB and whose safety metrics are also
within 1e-4 dB are treated as equivalent, then the structurally cheaper router
is preferred.

Protocol:
  * A+B from BOTH B-GRID and V1_A is the only selection set;
  * C is not evaluated until one architecture has been selected;
  * feasibility contract is inherited from LAB06;
  * no strength > 1.0 and no new quality parameter is tuned here.
"""
import argparse
import csv
import json
from pathlib import Path

import lab_moe_ng_v02_router_ablation as lab06

QUALITY_EQUIV_DB = 1.0e-4
SAFETY_EQUIV_DB = 1.0e-4

# Lower is cheaper. always_on lets the shader compiler remove the complete
# luma/curvature/router path; single-gate variants retain one router dimension;
# the product router retains both.
ROUTER_COST_RANK = {
    "always_on": 0,
    "rel_only": 1,
    "abs_only": 1,
    "routed_selected": 2,
}


def load_split(bgrid, v1a):
    train = []
    hold = []
    for proto, path_s in (("B_GRID", bgrid), ("V1_A", v1a)):
        path = Path(path_s)
        manifest = list(csv.DictReader((path / "CORPUS_MANIFEST.csv").open(newline="", encoding="utf-8")))
        for n, row in enumerate(manifest, 1):
            case = lab06.h5.prep(path, row, proto)
            (hold if row.get("crop_tag") == "C" else train).append(case)
            print(f"{proto} {n:02d}/{len(manifest)} {row['case_id']}")
    return train, hold


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bgrid", required=True)
    ap.add_argument("--v1a", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    train, hold = load_split(args.bgrid, args.v1a)

    candidates = []
    train_rows = {}
    for name in lab06.CANDIDATES:
        rows, summary = lab06.eval_candidate(train, name)
        summary = dict(summary)
        summary["router_cost_rank"] = ROUTER_COST_RANK[name]
        summary["feasible"] = summary["worst_gap"] >= -0.15 and summary["bg_struct_mean_gap"] >= 0.20
        train_rows[name] = rows
        candidates.append(summary)

    feasible = [c for c in candidates if c["feasible"]]
    if not feasible:
        raise SystemExit("LAB07: no feasible A+B architecture")

    raw_best = max(feasible, key=lambda c: c["mean_psnr"])
    equivalent = [
        c for c in feasible
        if c["mean_psnr"] >= raw_best["mean_psnr"] - QUALITY_EQUIV_DB
        and c["worst_gap"] >= raw_best["worst_gap"] - SAFETY_EQUIV_DB
        and c["bg_struct_mean_gap"] >= raw_best["bg_struct_mean_gap"] - SAFETY_EQUIV_DB
    ]
    selected = min(equivalent, key=lambda c: (c["router_cost_rank"], -c["mean_psnr"]))
    selected_name = selected["candidate"]

    hold_rows, hold_summary = lab06.eval_candidate(hold, selected_name)

    for c in candidates:
        c["psnr_delta_from_raw_best_db"] = c["mean_psnr"] - raw_best["mean_psnr"]
        c["quality_equivalent_to_raw_best"] = c in equivalent

    candidates.sort(key=lambda c: (c["router_cost_rank"], -c["mean_psnr"]))

    with (out / "TRAIN_COST_AWARE_SWEEP.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(candidates[0]))
        w.writeheader()
        w.writerows(candidates)

    for filename, rows in (
        ("TRAIN_SELECTED_PER_CASE.csv", train_rows[selected_name]),
        ("HOLDOUT_C_SELECTED_PER_CASE.csv", hold_rows),
    ):
        with (out / filename).open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)

    result = {
        "protocol": "LAB07_COST_AWARE_MC_AB_TRAIN_C_HOLDOUT",
        "selection_used_holdout": False,
        "quality_equivalence_db": QUALITY_EQUIV_DB,
        "safety_equivalence_db": SAFETY_EQUIV_DB,
        "raw_best_AplusB": raw_best,
        "equivalent_AplusB_candidates": [c["candidate"] for c in equivalent],
        "selected": selected,
        "train_candidates": candidates,
        "holdout_C_selected": hold_summary,
        "decision": {
            "selected_architecture": selected_name,
            "router_removed": selected_name == "always_on",
            "reason": "prefer lowest structural router cost among A+B quality-equivalent feasible candidates",
        },
    }
    (out / "SUMMARY.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
