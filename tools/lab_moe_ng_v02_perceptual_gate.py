#!/usr/bin/env python3
"""LAB05 perceptual/native-band diagnostic for PTAR-NG MoE v02.

This is a diagnostic, not a new acceptance protocol. It evaluates the current
LAB05-MC candidate against bilinear and the validated MoE v01 path on the
immutable V1_A and B_GRID corpora, preserving the A+B / C split used by the
LAB05 Hermite selection.

The spectral band is explicitly defined here as radial 0.20..1/3 cycles per HR
pixel. 1/3 is the LR Nyquist limit when LR samples are spaced 1.5 HR pixels.
Changing this band definition changes the diagnostic and must be reported.
"""
import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image

import lab_moe_ng_v02_hermite_sweep as h5
import lab_moe_ng_v02_sweep as v02base

LUMA = np.asarray([0.2126, 0.7152, 0.0722], dtype=np.float32)
REL_HI = np.float32(0.60)
ABS_HI = np.float32(0.16)
BAND_LO = 0.20
BAND_HI = 1.0 / 3.0
EPS = 1.0e-12


def load_rgb(path):
    with Image.open(path) as im:
        return np.asarray(im.convert("RGB"), dtype=np.float32) / np.float32(255.0)


def psnr(a, b):
    d = np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64)
    mse = float(np.mean(d * d))
    return float("inf") if mse <= 0.0 else 10.0 * math.log10(1.0 / mse)


def luma(a):
    return np.sum(np.asarray(a, dtype=np.float32) * LUMA, axis=2, dtype=np.float32)


def spectral_metrics(ref_rgb, cand_rgb):
    yr = luma(ref_rgb).astype(np.float64)
    yc = luma(cand_rgb).astype(np.float64)
    h, w = yr.shape
    wy = np.hanning(h)
    wx = np.hanning(w)
    win = wy[:, None] * wx[None, :]
    yr = (yr - yr.mean()) * win
    yc = (yc - yc.mean()) * win

    fr = np.fft.rfft2(yr)
    fc = np.fft.rfft2(yc)
    mr = np.abs(fr)
    mc = np.abs(fc)

    fy = np.fft.fftfreq(h)
    fx = np.fft.rfftfreq(w)
    radial = np.sqrt(fy[:, None] ** 2 + fx[None, :] ** 2)
    mask = (radial >= BAND_LO) & (radial <= BAND_HI)
    if not np.any(mask):
        raise RuntimeError("native-band mask is empty")

    ref_band = mr[mask]
    cand_band = mc[mask]
    floor = max(float(ref_band.max()) * 1.0e-6, EPS)
    ref_db = 20.0 * np.log10(np.maximum(ref_band, floor))
    cand_db = 20.0 * np.log10(np.maximum(cand_band, floor))
    log_mae_db = float(np.mean(np.abs(cand_db - ref_db)))

    er = float(np.sum(ref_band * ref_band))
    ec = float(np.sum(cand_band * cand_band))
    energy_ratio_db = 10.0 * math.log10(max(ec, EPS) / max(er, EPS))
    energy_abs_error_db = abs(energy_ratio_db)
    return log_mae_db, energy_ratio_db, energy_abs_error_db


def render_current(lr):
    base, experts, ac, rc = h5.planes(lr)
    gate = h5.sat(rc / REL_HI) * h5.sat(ac / ABS_HI)
    out = base + (experts["mc"] - base) * gate[..., None]
    return out.astype(np.float32), gate.astype(np.float32), base, experts["mc"]


def eval_case(corpus, row, protocol):
    hr = load_rgb(corpus / row["reference_path"])
    lr = load_rgb(corpus / row["input_path"])

    lab05, gate, bil, mc = render_current(lr)
    bil2, v01, _, _ = v02base.components(lr)

    # Both helpers implement the same grid-aligned bilinear contract.
    bilinear_helper_max_abs = float(np.max(np.abs(bil.astype(np.float64) - bil2.astype(np.float64))))

    outputs = {"bilinear": bil, "v01": v01, "lab05": lab05}
    metrics = {}
    for name, out in outputs.items():
        sm, se, sea = spectral_metrics(hr, out)
        yout = luma(out)
        yhr = luma(hr)
        metrics[name] = {
            "psnr": psnr(hr, out),
            "luma_bias": float(np.mean(yout.astype(np.float64) - yhr.astype(np.float64))),
            "abs_luma_bias": abs(float(np.mean(yout.astype(np.float64) - yhr.astype(np.float64)))),
            "native_band_log_mae_db": sm,
            "native_band_energy_ratio_db": se,
            "native_band_energy_abs_error_db": sea,
        }

    # LAB05 is a convex blend between grid bilinear and a monotone-clamped MC
    # expert. These are hard numerical invariants, not perceptual thresholds.
    lo = np.minimum(bil, mc)
    hi = np.maximum(bil, mc)
    below = np.maximum(lo - lab05, 0.0)
    above = np.maximum(lab05 - hi, 0.0)
    convex_violation = float(max(np.max(below), np.max(above)))
    finite = bool(np.isfinite(lab05).all() and np.isfinite(gate).all())
    range_violation = float(max(max(0.0, -float(np.min(lab05))), max(0.0, float(np.max(lab05)) - 1.0)))

    return {
        "protocol": protocol,
        "case_id": row["case_id"],
        "family": row["family"],
        "crop_tag": row.get("crop_tag", ""),
        "split": "holdout_C" if row.get("crop_tag") == "C" else "train_AB",
        "bilinear_psnr": metrics["bilinear"]["psnr"],
        "v01_psnr": metrics["v01"]["psnr"],
        "lab05_psnr": metrics["lab05"]["psnr"],
        "lab05_minus_bilinear_psnr": metrics["lab05"]["psnr"] - metrics["bilinear"]["psnr"],
        "lab05_minus_v01_psnr": metrics["lab05"]["psnr"] - metrics["v01"]["psnr"],
        "bilinear_native_band_log_mae_db": metrics["bilinear"]["native_band_log_mae_db"],
        "v01_native_band_log_mae_db": metrics["v01"]["native_band_log_mae_db"],
        "lab05_native_band_log_mae_db": metrics["lab05"]["native_band_log_mae_db"],
        "lab05_minus_bilinear_native_band_log_mae_db": metrics["lab05"]["native_band_log_mae_db"] - metrics["bilinear"]["native_band_log_mae_db"],
        "lab05_minus_v01_native_band_log_mae_db": metrics["lab05"]["native_band_log_mae_db"] - metrics["v01"]["native_band_log_mae_db"],
        "bilinear_native_band_energy_ratio_db": metrics["bilinear"]["native_band_energy_ratio_db"],
        "v01_native_band_energy_ratio_db": metrics["v01"]["native_band_energy_ratio_db"],
        "lab05_native_band_energy_ratio_db": metrics["lab05"]["native_band_energy_ratio_db"],
        "bilinear_native_band_energy_abs_error_db": metrics["bilinear"]["native_band_energy_abs_error_db"],
        "v01_native_band_energy_abs_error_db": metrics["v01"]["native_band_energy_abs_error_db"],
        "lab05_native_band_energy_abs_error_db": metrics["lab05"]["native_band_energy_abs_error_db"],
        "lab05_abs_luma_bias": metrics["lab05"]["abs_luma_bias"],
        "v01_abs_luma_bias": metrics["v01"]["abs_luma_bias"],
        "bilinear_abs_luma_bias": metrics["bilinear"]["abs_luma_bias"],
        "gate_mean": float(np.mean(gate, dtype=np.float64)),
        "gate_max": float(np.max(gate)),
        "convex_hull_max_violation": convex_violation,
        "range_01_max_violation": range_violation,
        "finite": finite,
        "bilinear_helper_max_abs": bilinear_helper_max_abs,
    }


def aggregate(rows):
    def mean(key):
        vals = [float(r[key]) for r in rows]
        return float(sum(vals) / len(vals))
    return {
        "cases": len(rows),
        "mean_bilinear_psnr": mean("bilinear_psnr"),
        "mean_v01_psnr": mean("v01_psnr"),
        "mean_lab05_psnr": mean("lab05_psnr"),
        "mean_lab05_minus_bilinear_psnr": mean("lab05_minus_bilinear_psnr"),
        "worst_lab05_minus_bilinear_psnr": min(float(r["lab05_minus_bilinear_psnr"]) for r in rows),
        "mean_lab05_minus_v01_psnr": mean("lab05_minus_v01_psnr"),
        "mean_bilinear_native_band_log_mae_db": mean("bilinear_native_band_log_mae_db"),
        "mean_v01_native_band_log_mae_db": mean("v01_native_band_log_mae_db"),
        "mean_lab05_native_band_log_mae_db": mean("lab05_native_band_log_mae_db"),
        "mean_lab05_minus_bilinear_native_band_log_mae_db": mean("lab05_minus_bilinear_native_band_log_mae_db"),
        "mean_lab05_minus_v01_native_band_log_mae_db": mean("lab05_minus_v01_native_band_log_mae_db"),
        "mean_bilinear_native_band_energy_abs_error_db": mean("bilinear_native_band_energy_abs_error_db"),
        "mean_v01_native_band_energy_abs_error_db": mean("v01_native_band_energy_abs_error_db"),
        "mean_lab05_native_band_energy_abs_error_db": mean("lab05_native_band_energy_abs_error_db"),
        "mean_bilinear_abs_luma_bias": mean("bilinear_abs_luma_bias"),
        "mean_v01_abs_luma_bias": mean("v01_abs_luma_bias"),
        "mean_lab05_abs_luma_bias": mean("lab05_abs_luma_bias"),
        "max_gate": max(float(r["gate_max"]) for r in rows),
        "max_convex_hull_violation": max(float(r["convex_hull_max_violation"]) for r in rows),
        "max_range_01_violation": max(float(r["range_01_max_violation"]) for r in rows),
        "max_bilinear_helper_abs": max(float(r["bilinear_helper_max_abs"]) for r in rows),
        "all_finite": all(bool(r["finite"]) for r in rows),
        "lab05_better_bilinear_psnr_cases": sum(float(r["lab05_minus_bilinear_psnr"]) > 0.0 for r in rows),
        "lab05_better_bilinear_native_band_log_mae_cases": sum(float(r["lab05_minus_bilinear_native_band_log_mae_db"]) < 0.0 for r in rows),
        "lab05_better_v01_native_band_log_mae_cases": sum(float(r["lab05_minus_v01_native_band_log_mae_db"]) < 0.0 for r in rows),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bgrid", required=True)
    ap.add_argument("--v1a", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    rows = []
    for protocol, path_s in [("B_GRID", args.bgrid), ("V1_A", args.v1a)]:
        path = Path(path_s)
        manifest = list(csv.DictReader((path / "CORPUS_MANIFEST.csv").open(newline="", encoding="utf-8")))
        for i, row in enumerate(manifest, 1):
            rec = eval_case(path, row, protocol)
            rows.append(rec)
            print(
                f"{protocol} {i:02d}/{len(manifest)} {row['case_id']} "
                f"dPSNR_B={rec['lab05_minus_bilinear_psnr']:+.4f} "
                f"dSpecMAE_B={rec['lab05_minus_bilinear_native_band_log_mae_db']:+.4f} dB"
            )

    with (out / "PER_CASE.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)

    groups = {
        "all": rows,
        "train_AB": [r for r in rows if r["split"] == "train_AB"],
        "holdout_C": [r for r in rows if r["split"] == "holdout_C"],
        "B_GRID": [r for r in rows if r["protocol"] == "B_GRID"],
        "V1_A": [r for r in rows if r["protocol"] == "V1_A"],
    }
    summary = {
        "protocol": "LAB05_MC_NATIVE_BAND_DIAGNOSTIC_V1",
        "acceptance_protocol": False,
        "selection_used_this_diagnostic": False,
        "candidate": {
            "expert": "mc",
            "rel_hi": float(REL_HI),
            "abs_lo": 0.0,
            "abs_hi": float(ABS_HI),
            "strength": 1.0,
        },
        "spectral_definition": {
            "signal": "luminance BT.709 coefficients in sRGB/code-value domain",
            "window": "separable Hann on 288x288 HR grid after mean removal",
            "radial_band_cycles_per_hr_pixel": [BAND_LO, BAND_HI],
            "band_hi_rationale": "LR Nyquist for 1.5 HR-pixel LR sample spacing",
            "log_metric": "mean absolute FFT-magnitude error in dB; lower is better",
            "energy_metric": "absolute band-energy ratio error in dB; lower is better",
        },
        "groups": {k: aggregate(v) for k, v in groups.items()},
    }

    # Only mathematical/runtime invariants are hard-gated here. Spectral and
    # perceptual figures remain diagnostics until an acceptance threshold is
    # explicitly versioned.
    invariants = {
        "all_finite": summary["groups"]["all"]["all_finite"],
        "gate_le_1": summary["groups"]["all"]["max_gate"] <= 1.0 + 1.0e-6,
        "convex_hull_max_violation_le_1e-6": summary["groups"]["all"]["max_convex_hull_violation"] <= 1.0e-6,
        "range_01_max_violation_le_1e-6": summary["groups"]["all"]["max_range_01_violation"] <= 1.0e-6,
        "bilinear_helpers_agree_le_2e-6": summary["groups"]["all"]["max_bilinear_helper_abs"] <= 2.0e-6,
    }
    summary["invariants"] = invariants
    summary["invariants_pass"] = all(invariants.values())
    (out / "SUMMARY.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    if not summary["invariants_pass"]:
        raise SystemExit("LAB05 native-band diagnostic invariant failure")


if __name__ == "__main__":
    main()
