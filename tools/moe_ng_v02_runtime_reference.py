#!/usr/bin/env python3
"""CPU float32 reference for the selected PTAR-NG MoE v02 LAB07 shader.

This module mirrors only the selected always-on MC reconstruction path.  It is
kept separate from the LAB05/LAB07 sweep machinery so runtime validation does
not depend on parameter-selection code.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

F = np.float32


def load_rgb(path: Path) -> np.ndarray:
    with Image.open(path) as im:
        return np.asarray(im.convert("RGB"), dtype=np.float32) / F(255.0)


def sample_linear_clamp(a: np.ndarray, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    h, w, _ = a.shape
    x0 = np.floor(x).astype(np.int32)
    y0 = np.floor(y).astype(np.int32)
    tx = (x - x0).astype(np.float32)
    ty = (y - y0).astype(np.float32)
    xa = np.clip(x0, 0, w - 1)
    xb = np.clip(x0 + 1, 0, w - 1)
    ya = np.clip(y0, 0, h - 1)
    yb = np.clip(y0 + 1, 0, h - 1)
    A = a[ya, xa]
    B = a[ya, xb]
    C = a[yb, xa]
    D = a[yb, xb]
    tx = tx[..., None]
    ty = ty[..., None]
    return ((A * (F(1) - tx) + B * tx) * (F(1) - ty) +
            (C * (F(1) - tx) + D * tx) * ty).astype(np.float32)


def mc_slope(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    avg = F(0.5) * (a + b)
    lim = F(2.0) * np.minimum(np.abs(a), np.abs(b))
    limited = np.clip(avg, -lim, lim)
    same_or_zero = (a * b) >= F(0.0)
    return (limited * same_or_zero.astype(np.float32)).astype(np.float32)


def hermite13(f0: np.ndarray, f1: np.ndarray, m0: np.ndarray, m1: np.ndarray) -> np.ndarray:
    h = (F(20) * f0 + F(4) * m0 + F(7) * f1 - F(2) * m1) * F(1.0 / 27.0)
    return np.minimum(np.maximum(h, np.minimum(f0, f1)), np.maximum(f0, f1)).astype(np.float32)


def hermite23(f0: np.ndarray, f1: np.ndarray, m0: np.ndarray, m1: np.ndarray) -> np.ndarray:
    h = (F(7) * f0 + F(2) * m0 + F(20) * f1 - F(4) * m1) * F(1.0 / 27.0)
    return np.minimum(np.maximum(h, np.minimum(f0, f1)), np.maximum(f0, f1)).astype(np.float32)


def reconstruct_x15(lr: np.ndarray) -> np.ndarray:
    h, w, _ = lr.shape
    out_h = (h * 3) // 2
    out_w = (w * 3) // 2
    oy, ox = np.mgrid[0:out_h, 0:out_w]
    oxu = ox.astype(np.uint32)
    oyu = oy.astype(np.uint32)
    sx = ox.astype(np.float32) * F(2.0 / 3.0)
    sy = oy.astype(np.float32) * F(2.0 / 3.0)
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
    use_x = np.abs(gx) >= np.abs(gy)

    bx = np.where(use_x, fx.astype(np.float32), sx)
    by = np.where(use_x, sy, fy.astype(np.float32))
    ax = use_x.astype(np.float32)
    ay = (~use_x).astype(np.float32)

    fm1 = sample_linear_clamp(lr, bx - ax, by - ay)
    f0 = sample_linear_clamp(lr, bx, by)
    f1 = sample_linear_clamp(lr, bx + ax, by + ay)
    f2 = sample_linear_clamp(lr, bx + F(2) * ax, by + F(2) * ay)

    phase = np.where(use_x, oxu % 3, oyu % 3)
    d0 = f0 - fm1
    d1 = f1 - f0
    d2 = f2 - f1
    m0 = mc_slope(d0, d1)
    m1 = mc_slope(d1, d2)
    h13 = hermite13(f0, f1, m0, m1)
    h23 = hermite23(f0, f1, m0, m1)
    out = np.where((phase == 1)[..., None], h23, h13)
    return np.where((phase == 0)[..., None], f0, out).astype(np.float32)


def quantize_unorm8(rgb: np.ndarray) -> np.ndarray:
    # D3D11 RTV conversion is allowed a small implementation-level rounding
    # difference; the hardware gate remains <= 1 LSB, as for MoE v01.
    return np.floor(np.clip(rgb, F(0), F(1)) * F(255.0) + F(0.5)).astype(np.uint8)


def save_rgb8(path: Path, rgb: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(quantize_unorm8(rgb), mode="RGB").save(path)
