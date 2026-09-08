#!/usr/bin/env python3
"""LAB05-MC second boundary expansion sweep.

The first non-leaky LAB05 sweep selected rel_hi=0.60, abs_hi=0.16 and
strength=1.0. The first boundary expansion then selected rel_hi=0.20,
abs_lo=0.0, abs_hi=0.06 and strength=1.0, again on the permissive edge.
This second follow-up probes substantially lower positive routing thresholds
without exceeding strength=1.0.

Selection remains strictly A+B only. Crop C is evaluated after selection and
is never used to choose a candidate.
"""
import argparse
import csv
import itertools
import json
from pathlib import Path

import lab_moe_ng_v02_hermite_sweep as h5


# Controlled expansion toward an almost-always-on MC expert. rel_hi must stay
# positive because the router divides by it. abs_lo is fixed at zero because
# the previous optimum already hit that physical lower bound.
REL_HI = [0.01, 0.02, 0.04, 0.08, 0.12, 0.16, 0.20]
ABS_LO = [0.0]
ABS_HI = [0.005, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06]
STRENGTH = [0.75, 0.875, 1.0]


def candidates():
    out = []
    for rhi, alo, ahi, strength in itertools.product(REL_HI, ABS_LO, ABS_HI, STRENGTH):
        if ahi <= alo:
            continue
        out.append(("mc", rhi, alo, ahi, strength))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bgrid", required=True)
    ap.add_argument("--v1a", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    train = []
    hold = []
    for proto, path_s in [("B_GRID", a.bgrid), ("V1_A", a.v1a)]:
        path = Path(path_s)
        rr = list(csv.DictReader((path / "CORPUS_MANIFEST.csv").open(newline="", encoding="utf-8")))
        for n, r in enumerate(rr, 1):
            c = h5.prep(path, r, proto)
            (hold if r.get("crop_tag") == "C" else train).append(c)
            print(f"{proto} {n:02d}/{len(rr)} {r['case_id']}")

    C = candidates()
    print("candidates", len(C))
    P, G = h5.matrix(train, C)
    R = []
    for i, c in enumerate(C):
        _, r = h5.summarize(i, c, train, P, G)
        # Same train-only feasibility contract as the original LAB05 sweep.
        r["score"] = (
            r["mean_psnr"]
            - 12 * max(0.0, -0.15 - r["worst_gap"])
            - 4 * max(0.0, 0.20 - r["bg_struct_mean_gap"])
        )
        R.append(r)

    feasible = [r for r in R if r["worst_gap"] >= -0.15 and r["bg_struct_mean_gap"] >= 0.20]
    sel = max(feasible, key=lambda r: r["mean_psnr"]) if feasible else max(R, key=lambda r: r["score"])
    ct = ("mc", sel["rel_hi"], sel["abs_lo"], sel["abs_hi"], sel["strength"])
    idx = C.index(ct)

    train_rows, train_summary = h5.summarize(idx, ct, train, P, G)
    HP, HG = h5.matrix(hold, [ct])
    hold_rows, hold_summary = h5.summarize(0, ct, hold, HP, HG)
    R.sort(key=lambda r: r["score"], reverse=True)

    for name, rows in [
        ("TRAIN_SELECTED_PER_CASE.csv", train_rows),
        ("HOLDOUT_C_PER_CASE.csv", hold_rows),
    ]:
        with (out / name).open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)

    with (out / "TRAIN_SWEEP.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(R[0]))
        w.writeheader()
        w.writerows(R)

    boundary = {
        "rel_hi_at_lower_boundary": sel["rel_hi"] == min(REL_HI),
        "abs_hi_at_lower_boundary": sel["abs_hi"] == min(ABS_HI),
        "abs_lo_fixed_zero": True,
        "strength_at_upper_boundary": sel["strength"] == max(STRENGTH),
    }
    res = {
        "protocol": "LAB05_MC_BOUNDARY_EXPANSION2_AB_TRAIN_C_HOLDOUT",
        "selection_used_holdout": False,
        "search": {
            "rel_hi": REL_HI,
            "abs_lo": ABS_LO,
            "abs_hi": ABS_HI,
            "strength": STRENGTH,
        },
        "candidate_count": len(C),
        "feasible_count": len(feasible),
        "selected": sel,
        "selected_hits_search_boundary": boundary,
        "train": train_summary,
        "holdout_C": hold_summary,
        "top": R[:30],
    }
    (out / "SUMMARY.json").write_text(json.dumps(res, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(res, indent=2))


if __name__ == "__main__":
    main()
