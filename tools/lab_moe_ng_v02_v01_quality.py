#!/usr/bin/env python3
"""LAB10: paired spatial-quality diagnostic, MoE v01 vs LAB07 always-on MC.

This is a diagnostic only. It does not select/tune parameters and therefore
reports A+B and crop-C holdout separately without using either to modify the
candidate. Both candidates use the same directional texture footprint model:
1 GatherGreen + 4 SampleLevel, exact x1.5.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_hermite_sweep as h5

LUMA = np.asarray([0.2126, 0.7152, 0.0722], dtype=np.float32)
STRUCT_FAMILIES = {
    "curves_high_frequency",
    "raster_repeated",
    "thin_oblique_edges",
    "ui_text",
    "silhouette_edge",
    "pixel_art",
    "mixed_frequency",
    "repeated_texture",
    "foliage_texture",
}


def psnr_from_mse(mse: float) -> float:
    return float("inf") if mse <= 0.0 else 10.0 * math.log10(1.0 / mse)


def mse(a: np.ndarray, b: np.ndarray) -> float:
    d = np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64)
    return float(np.mean(d * d, dtype=np.float64))


def masked_psnr(candidate: np.ndarray, ref: np.ndarray, mask: np.ndarray) -> float:
    if not np.any(mask):
        return float("nan")
    d = candidate[mask] - ref[mask]
    return psnr_from_mse(float(np.mean(np.asarray(d, dtype=np.float64) ** 2, dtype=np.float64)))


def luma(img: np.ndarray) -> np.ndarray:
    return np.sum(np.asarray(img, dtype=np.float32) * LUMA, axis=2, dtype=np.float32)


def gradient_xy(y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    y = np.asarray(y, dtype=np.float32)
    dx = np.empty_like(y)
    dy = np.empty_like(y)
    dx[:, 1:-1] = np.float32(0.5) * (y[:, 2:] - y[:, :-2])
    dx[:, 0] = y[:, 1] - y[:, 0]
    dx[:, -1] = y[:, -1] - y[:, -2]
    dy[1:-1, :] = np.float32(0.5) * (y[2:, :] - y[:-2, :])
    dy[0, :] = y[1, :] - y[0, :]
    dy[-1, :] = y[-1, :] - y[-2, :]
    return dx, dy


def laplacian(y: np.ndarray) -> np.ndarray:
    p = np.pad(np.asarray(y, dtype=np.float32), ((1, 1), (1, 1)), mode="edge")
    return (
        p[1:-1, :-2]
        + p[1:-1, 2:]
        + p[:-2, 1:-1]
        + p[2:, 1:-1]
        - np.float32(4.0) * p[1:-1, 1:-1]
    ).astype(np.float32)


def reconstruct_pair(lr: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    h, w, _ = lr.shape
    oh, ow = (h * 3) // 2, (w * 3) // 2
    oy, ox = np.mgrid[0:oh, 0:ow]
    oxu = ox.astype(np.uint32)
    oyu = oy.astype(np.uint32)
    sx = ox.astype(np.float32) * (np.float32(2.0) / np.float32(3.0))
    sy = oy.astype(np.float32) * (np.float32(2.0) / np.float32(3.0))
    fx = np.floor(sx).astype(np.int32)
    fy = np.floor(sy).astype(np.int32)

    x0 = np.clip(fx, 0, w - 1)
    x1 = np.clip(fx + 1, 0, w - 1)
    y0 = np.clip(fy, 0, h - 1)
    y1 = np.clip(fy + 1, 0, h - 1)
    tl = lr[y0, x0, 1]
    tr = lr[y0, x1, 1]
    bl = lr[y1, x0, 1]
    br = lr[y1, x1, 1]
    gx = (tr + br) - (tl + bl)
    gy = (bl + br) - (tl + tr)
    agx = np.abs(gx)
    agy = np.abs(gy)
    use_x = agx >= agy

    bx = np.where(use_x, fx.astype(np.float32), sx)
    by = np.where(use_x, sy, fy.astype(np.float32))
    ax = use_x.astype(np.float32)
    ay = (~use_x).astype(np.float32)

    fm1 = h5.sample(lr, bx - ax, by - ay)
    f0 = h5.sample(lr, bx, by)
    f1 = h5.sample(lr, bx + ax, by + ay)
    f2 = h5.sample(lr, bx + np.float32(2.0) * ax, by + np.float32(2.0) * ay)

    phase = np.where(use_x, oxu % 3, oyu % 3)
    t = np.where(phase == 1, np.float32(2.0 / 3.0), np.float32(1.0 / 3.0)).astype(np.float32)

    # LAB07 always-on MC.
    d0 = f0 - fm1
    d1 = f1 - f0
    d2 = f2 - f1
    m0 = h5.mc(d0, d1)
    m1 = h5.mc(d1, d2)
    mc = h5.hermite(f0, f1, m0, m1, t)
    v02 = np.where((phase == 0)[..., None], f0, mc).astype(np.float32)

    # MoE v01 SF5 exact expert/router equations from the validated HLSL.
    k13 = (
        np.float32(-0.274074074074074) * fm1
        + np.float32(0.877777777777778) * f0
        + np.float32(0.533333333333333) * f1
        + np.float32(-0.137037037037037) * f2
    )
    k23 = (
        np.float32(-0.137037037037037) * fm1
        + np.float32(0.533333333333333) * f0
        + np.float32(0.877777777777778) * f1
        + np.float32(-0.274074074074074) * f2
    )
    edge = np.where((phase == 1)[..., None], k23, k13).astype(np.float32)
    edge = np.where((phase == 0)[..., None], f0, edge).astype(np.float32)
    raster = np.minimum(np.maximum(edge, np.minimum(f0, f1)), np.maximum(f0, f1)).astype(np.float32)
    bilinear = (f0 * (np.float32(1.0) - t[..., None]) + f1 * t[..., None]).astype(np.float32)
    natural = (bilinear * np.float32(0.30) + raster * np.float32(0.70)).astype(np.float32)

    local_range = np.maximum(np.maximum(tl, tr), np.maximum(bl, br)) - np.minimum(
        np.minimum(tl, tr), np.minimum(bl, br)
    )
    gradient = np.float32(0.5) * np.maximum(agx, agy)
    diagonal = np.float32(0.5) * np.abs((tl + br) - (tr + bl))
    coherence = np.abs(agx - agy) / (agx + agy + np.float32(1.0e-6))
    range_conf = np.clip((local_range - np.float32(0.01)) / np.float32(0.10), 0.0, 1.0)
    coherence_conf = np.clip((coherence - np.float32(0.25)) / np.float32(0.45), 0.0, 1.0)
    diag_ratio = diagonal / (gradient + np.float32(1.0e-6))
    axis_conf = np.float32(1.0) - np.clip(
        (diag_ratio - np.float32(0.08)) / np.float32(0.45), 0.0, 1.0
    )
    raster_weight = range_conf * coherence_conf * axis_conf
    edge_base = np.clip((gradient - np.float32(0.02)) / np.float32(0.10), 0.0, 1.0)
    edge_weight = (np.float32(1.0) - raster_weight) * edge_base
    natural_weight = np.float32(1.0) - raster_weight - edge_weight
    v01 = (
        natural * natural_weight[..., None]
        + edge * edge_weight[..., None]
        + raster * raster_weight[..., None]
    ).astype(np.float32)
    v01 = np.where((phase == 0)[..., None], f0, v01).astype(np.float32)

    # Model the UNORM render target used by the hardware validators.
    return np.clip(v01, 0.0, 1.0), np.clip(v02, 0.0, 1.0)


def metrics(candidate: np.ndarray, ref: np.ndarray, masks: tuple[np.ndarray, np.ndarray]) -> dict:
    yr = luma(ref)
    yc = luma(candidate)
    rdx, rdy = gradient_xy(yr)
    cdx, cdy = gradient_xy(yc)
    edge_mask, flat_mask = masks
    grad_rmse = math.sqrt(float(np.mean((cdx - rdx) ** 2 + (cdy - rdy) ** 2, dtype=np.float64)))
    lap_rmse = math.sqrt(mse(laplacian(yc), laplacian(yr)))
    return {
        "psnr_rgb": psnr_from_mse(mse(candidate, ref)),
        "psnr_luma": psnr_from_mse(mse(yc, yr)),
        "psnr_edge_rgb": masked_psnr(candidate, ref, edge_mask),
        "psnr_flat_rgb": masked_psnr(candidate, ref, flat_mask),
        "gradient_rmse_luma": grad_rmse,
        "laplacian_rmse_luma": lap_rmse,
    }


def make_masks(ref: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    y = luma(ref)
    dx, dy = gradient_xy(y)
    mag = np.sqrt(dx * dx + dy * dy, dtype=np.float32)
    q25, q75 = np.quantile(mag, [0.25, 0.75])
    return mag >= q75, mag <= q25


def mean(xs: list[float]) -> float:
    return float(sum(xs) / len(xs)) if xs else float("nan")


def aggregate(rows: list[dict]) -> dict:
    if not rows:
        return {"cases": 0}
    dpsnr = [r["delta_psnr_rgb"] for r in rows]
    dedge = [r["delta_psnr_edge_rgb"] for r in rows]
    dflat = [r["delta_psnr_flat_rgb"] for r in rows]
    dgrad = [r["improvement_gradient_rmse_luma"] for r in rows]
    dlap = [r["improvement_laplacian_rmse_luma"] for r in rows]
    return {
        "cases": len(rows),
        "mean_v01_psnr_rgb": mean([r["v01_psnr_rgb"] for r in rows]),
        "mean_v02_psnr_rgb": mean([r["v02_psnr_rgb"] for r in rows]),
        "mean_delta_psnr_rgb": mean(dpsnr),
        "median_delta_psnr_rgb": float(np.median(np.asarray(dpsnr, dtype=np.float64))),
        "worst_delta_psnr_rgb": min(dpsnr),
        "best_delta_psnr_rgb": max(dpsnr),
        "v02_psnr_wins": sum(x > 0.0 for x in dpsnr),
        "v01_psnr_wins": sum(x < 0.0 for x in dpsnr),
        "psnr_ties": sum(x == 0.0 for x in dpsnr),
        "mean_delta_edge_psnr_rgb": mean(dedge),
        "mean_delta_flat_psnr_rgb": mean(dflat),
        "mean_gradient_rmse_improvement": mean(dgrad),
        "mean_laplacian_rmse_improvement": mean(dlap),
    }


def run_corpus(path: Path, protocol: str) -> list[dict]:
    manifest = list(csv.DictReader((path / "CORPUS_MANIFEST.csv").open(newline="", encoding="utf-8")))
    rows = []
    for i, row in enumerate(manifest, 1):
        ref = h5.load(path / row["reference_path"])
        lr = h5.load(path / row["input_path"])
        v01, v02 = reconstruct_pair(lr)
        if v01.shape != ref.shape or v02.shape != ref.shape:
            raise RuntimeError(f"shape mismatch {row['case_id']}: ref={ref.shape} v01={v01.shape} v02={v02.shape}")
        masks = make_masks(ref)
        m1 = metrics(v01, ref, masks)
        m2 = metrics(v02, ref, masks)
        out = {
            "protocol": protocol,
            "case_id": row["case_id"],
            "family": row["family"],
            "crop_tag": row.get("crop_tag", ""),
            "split": "holdout_C" if row.get("crop_tag") == "C" else "train_AB",
        }
        for k, v in m1.items():
            out[f"v01_{k}"] = v
        for k, v in m2.items():
            out[f"v02_{k}"] = v
        out["delta_psnr_rgb"] = m2["psnr_rgb"] - m1["psnr_rgb"]
        out["delta_psnr_luma"] = m2["psnr_luma"] - m1["psnr_luma"]
        out["delta_psnr_edge_rgb"] = m2["psnr_edge_rgb"] - m1["psnr_edge_rgb"]
        out["delta_psnr_flat_rgb"] = m2["psnr_flat_rgb"] - m1["psnr_flat_rgb"]
        out["improvement_gradient_rmse_luma"] = m1["gradient_rmse_luma"] - m2["gradient_rmse_luma"]
        out["improvement_laplacian_rmse_luma"] = m1["laplacian_rmse_luma"] - m2["laplacian_rmse_luma"]
        rows.append(out)
        print(
            f"{protocol} {i:02d}/{len(manifest)} {row['case_id']} "
            f"dPSNR={out['delta_psnr_rgb']:+.4f} dEdge={out['delta_psnr_edge_rgb']:+.4f}"
        )
    return rows


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bgrid", required=True)
    ap.add_argument("--v1a", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = run_corpus(Path(args.bgrid), "B_GRID") + run_corpus(Path(args.v1a), "V1_A")
    fieldnames = list(rows[0].keys())
    with (out_dir / "PER_CASE.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)

    groups: dict[str, list[dict]] = {
        "all": rows,
        "train_AB": [r for r in rows if r["split"] == "train_AB"],
        "holdout_C": [r for r in rows if r["split"] == "holdout_C"],
        "B_GRID_all": [r for r in rows if r["protocol"] == "B_GRID"],
        "V1_A_all": [r for r in rows if r["protocol"] == "V1_A"],
        "structural_all": [r for r in rows if r["family"] in STRUCT_FAMILIES],
    }
    by_family: dict[str, list[dict]] = defaultdict(list)
    for r in rows:
        by_family[r["family"]].append(r)

    ranked = sorted(rows, key=lambda r: r["delta_psnr_rgb"])
    summary = {
        "protocol": "LAB10_V01_VS_LAB07_PAIRED_QUALITY_V1",
        "acceptance_protocol": False,
        "selection_or_tuning_used": False,
        "candidate_a": "PTAR_NG_MOE_V01_SF5",
        "candidate_b": "LAB07_ALWAYS_ON_MC",
        "comparison_scope": "spatial reconstruction only; FUSEDETAIL1/FG intentionally excluded",
        "aggregate": {k: aggregate(v) for k, v in groups.items()},
        "by_family": {k: aggregate(v) for k, v in sorted(by_family.items())},
        "worst_v02_vs_v01": ranked[:12],
        "best_v02_vs_v01": list(reversed(ranked[-12:])),
    }
    (out_dir / "SUMMARY.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
