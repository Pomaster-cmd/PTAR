#!/usr/bin/env python3
"""Fresh procedural LAB19 cases authored only after the LAB18 freeze commit.

This module owns case generation and the exact x1.5 HR->LR downsample contract.
It deliberately contains no candidate/gate logic, so validation policy and
content generation remain separate and auditable.
"""
from __future__ import annotations

import math

import numpy as np
import skimage
from skimage.transform import resize

HR_H = 288
HR_W = 288
LR_H = 192
LR_W = 192
SCALE = 1.5
SEED = 19091873
EXPECTED_SKIMAGE = "0.26.0"


def _grid():
    yy, xx = np.mgrid[0:HR_H, 0:HR_W]
    x01 = xx.astype(np.float32) / np.float32(HR_W - 1)
    y01 = yy.astype(np.float32) / np.float32(HR_H - 1)
    x = x01 * np.float32(2.0) - np.float32(1.0)
    y = y01 * np.float32(2.0) - np.float32(1.0)
    return x, y, x01, y01


def _smoothstep(t):
    t = np.clip(t, np.float32(0), np.float32(1)).astype(np.float32)
    return (t * t * (np.float32(3) - np.float32(2) * t)).astype(np.float32)


def _band(sd, half_width, aa=0.006):
    d = np.abs(np.asarray(sd, dtype=np.float32))
    t = (np.float32(half_width + aa) - d) / np.float32(2.0 * aa)
    return _smoothstep(t)


def _disc(x, y, cx, cy, radius, aa=0.006):
    d = np.sqrt((x - np.float32(cx)) ** 2 + (y - np.float32(cy)) ** 2)
    t = (np.float32(radius + aa) - d) / np.float32(2.0 * aa)
    return _smoothstep(t)


def _sd_box(x, y, cx, cy, hx, hy, angle):
    ca = np.float32(math.cos(angle))
    sa = np.float32(math.sin(angle))
    px = x - np.float32(cx)
    py = y - np.float32(cy)
    xr = ca * px + sa * py
    yr = -sa * px + ca * py
    qx = np.abs(xr) - np.float32(hx)
    qy = np.abs(yr) - np.float32(hy)
    ox = np.maximum(qx, np.float32(0))
    oy = np.maximum(qy, np.float32(0))
    outside = np.sqrt(ox * ox + oy * oy)
    inside = np.minimum(np.maximum(qx, qy), np.float32(0))
    return (outside + inside).astype(np.float32)


def _compose(base, mask, color):
    m = np.asarray(mask, dtype=np.float32)[..., None]
    c = np.asarray(color, dtype=np.float32).reshape(1, 1, 3)
    return (base * (np.float32(1) - m) + c * m).astype(np.float32)


def clip01(a):
    return np.clip(np.asarray(a, dtype=np.float32), np.float32(0), np.float32(1)).astype(np.float32)


def downsample(hr):
    if skimage.__version__ != EXPECTED_SKIMAGE:
        raise RuntimeError(f"LAB19 requires scikit-image {EXPECTED_SKIMAGE}, got {skimage.__version__}")
    out = resize(
        np.asarray(hr, dtype=np.float32),
        (LR_H, LR_W, 3),
        order=3,
        mode="reflect",
        anti_aliasing=True,
        preserve_range=True,
        clip=True,
    )
    return np.asarray(out, dtype=np.float32)


def postfreeze_curved_ribbons(i):
    x, y, x01, _ = _grid()
    bg = np.float32(0.08) + np.float32(0.05) * x01
    img = np.stack([bg * np.float32(0.92), bg, bg * np.float32(1.08)], axis=2).astype(np.float32)
    phase = np.float32(0.43 * i)
    colors = [
        (0.90, 0.75, 0.28),
        (0.25, 0.82, 0.78),
        (0.82, 0.38, 0.72),
    ]
    for k in range(3):
        cx = -0.22 + 0.19 * k + 0.025 * ((i + k) % 3 - 1)
        cy = 0.12 - 0.15 * k + 0.02 * ((2 * i + k) % 3 - 1)
        dx = x - np.float32(cx)
        dy = y - np.float32(cy)
        rr = np.sqrt(dx * dx + dy * dy)
        th = np.arctan2(dy, dx)
        radius = np.float32(0.34 + 0.15 * k + 0.012 * i)
        target = radius + np.float32(0.022 + 0.004 * k) * np.sin(np.float32(3 + k) * th + phase)
        sd = rr - target
        m = _band(sd, 0.012 + 0.003 * ((i + k) % 2), aa=0.005)
        img = _compose(img, m, colors[k])
    # One deliberately finer displaced curve to exercise sparse curved detail.
    path = y - (np.float32(-0.50 + 0.04 * i) + np.float32(0.10) * np.sin(np.float32(4.2) * x + phase))
    img = _compose(img, _band(path, 0.0065, aa=0.0045), (0.92, 0.92, 0.88))
    return clip01(img)


def postfreeze_rotated_boxes(i):
    x, y, _, y01 = _grid()
    bg = np.float32(0.10) + np.float32(0.045) * y01
    img = np.stack([bg, bg * np.float32(1.05), bg * np.float32(0.95)], axis=2).astype(np.float32)
    colors = [(0.92, 0.48, 0.22), (0.30, 0.78, 0.92), (0.78, 0.84, 0.32)]
    for k in range(3):
        angle = 0.17 + 0.21 * k + 0.035 * i
        cx = -0.36 + 0.36 * k
        cy = -0.18 + 0.18 * ((i + k) % 3 - 1)
        hx = 0.22 + 0.035 * ((i + 2 * k) % 3)
        hy = 0.14 + 0.025 * ((2 * i + k) % 3)
        sd = _sd_box(x, y, cx, cy, hx, hy, angle)
        m = _band(sd, 0.010 + 0.002 * ((i + k) % 2), aa=0.0045)
        img = _compose(img, m, colors[k])
    # Offset corner marks make this different from LAB16 junction primitives.
    for k in range(5):
        a = 0.45 + 0.52 * k + 0.11 * i
        cx = 0.58 * math.cos(a)
        cy = 0.58 * math.sin(a)
        m = _disc(x, y, cx, cy, 0.018 + 0.002 * ((i + k) % 2), aa=0.004)
        img = _compose(img, m, (0.88, 0.86, 0.90))
    return clip01(img)


def postfreeze_sparse_paths(i):
    x, y, x01, _ = _grid()
    bg = np.float32(0.075) + np.float32(0.035) * x01
    img = np.stack([bg * np.float32(1.04), bg, bg * np.float32(0.96)], axis=2).astype(np.float32)
    phase = 0.31 * i
    specs = [
        (-0.44, 0.075, 3.1, (0.90, 0.87, 0.80)),
        (-0.02, 0.110, 2.2, (0.30, 0.84, 0.76)),
        (0.40, 0.060, 4.4, (0.86, 0.42, 0.70)),
    ]
    for k, (offset, amp, freq, color) in enumerate(specs):
        arg = np.float32(freq) * x + np.float32(phase + 0.7 * k)
        yp = np.float32(offset + 0.012 * ((i + k) % 3 - 1)) + np.float32(amp) * np.sin(arg)
        deriv = np.float32(amp * freq) * np.cos(arg)
        d = np.abs(y - yp) / np.sqrt(np.float32(1) + deriv * deriv)
        m = _band(d, 0.006 + 0.0015 * ((i + k) % 2), aa=0.004)
        img = _compose(img, m, color)
        for j in range(4):
            cx = -0.72 + 0.48 * j + 0.025 * ((i + j + k) % 3 - 1)
            cy = float(offset + amp * math.sin(freq * cx + phase + 0.7 * k))
            dot = _disc(x, y, cx, cy, 0.012 + 0.002 * ((i + j) % 2), aa=0.004)
            img = _compose(img, dot, color)
    return clip01(img)


def postfreeze_wave_interference(i):
    _, _, x, y = _grid()
    phase = 0.37 * i
    params = [
        (5.2 + 0.25 * i, 0.23 + 0.04 * i, 0.14),
        (8.7 + 0.18 * i, 1.07 - 0.03 * i, 0.11),
        (12.3 + 0.21 * i, 2.02 + 0.02 * i, 0.08),
        (15.1 + 0.12 * i, 2.63 - 0.04 * i, 0.055),
    ]
    s = np.full((HR_H, HR_W), np.float32(0.5), dtype=np.float32)
    for k, (freq, ang, amp) in enumerate(params):
        proj = np.float32(math.cos(ang)) * x + np.float32(math.sin(ang)) * y
        s += np.float32(amp) * np.sin(np.float32(2 * math.pi * freq) * proj + np.float32(phase + 0.53 * k))
    r = s + np.float32(0.025) * np.sin(np.float32(2 * math.pi * (3.1 + 0.1 * i)) * y)
    g = s
    b = s - np.float32(0.025) * np.sin(np.float32(2 * math.pi * (2.7 + 0.08 * i)) * x)
    return clip01(np.stack([r, g, b], axis=2))


def postfreeze_crosshatch(i):
    _, _, x, y = _grid()
    a1 = 0.36 + 0.035 * i
    a2 = 1.70 - 0.028 * i
    p1 = np.float32(math.cos(a1)) * x + np.float32(math.sin(a1)) * y
    p2 = np.float32(math.cos(a2)) * x + np.float32(math.sin(a2)) * y
    f1 = np.float32(9.0 + 0.55 * i)
    f2 = np.float32(11.4 + 0.42 * i)
    w1 = np.sin(np.float32(2 * math.pi) * f1 * p1 + np.float32(0.21 * i))
    w2 = np.sin(np.float32(2 * math.pi) * f2 * p2 + np.float32(0.47 + 0.17 * i))
    inter = w1 * w2
    base = np.float32(0.5) + np.float32(0.145) * w1 + np.float32(0.145) * w2 + np.float32(0.075) * inter
    r = base + np.float32(0.018) * w1
    g = base
    b = base - np.float32(0.018) * w2
    return clip01(np.stack([r, g, b], axis=2))


def _band_limited_field(rng, center, width):
    n = rng.standard_normal((HR_H, HR_W), dtype=np.float32)
    f = np.fft.fft2(n)
    fy = np.fft.fftfreq(HR_H) * HR_H
    fx = np.fft.fftfreq(HR_W) * HR_W
    yy, xx = np.meshgrid(fy, fx, indexing="ij")
    rr = np.sqrt(xx * xx + yy * yy)
    filt = np.exp(-0.5 * ((rr - float(center)) / float(width)) ** 2)
    out = np.fft.ifft2(f * filt).real.astype(np.float32)
    out -= np.float32(np.mean(out, dtype=np.float64))
    std = float(np.std(out, dtype=np.float64))
    if std > 1e-8:
        out /= np.float32(std)
    return out


def postfreeze_multiscale_field(i):
    rng = np.random.default_rng(SEED + 1009 * (i + 1))
    low = _band_limited_field(rng, 7.0 + 0.5 * i, 2.2)
    high = _band_limited_field(rng, 18.0 + 0.8 * i, 4.0)
    mix = np.float32(0.5) + np.float32(0.085) * low + np.float32(0.055) * high
    # Small channel offsets prevent a purely greyscale-only acceptance corpus.
    r = mix + np.float32(0.012) * np.roll(high, 3 + i, axis=1)
    g = mix
    b = mix - np.float32(0.012) * np.roll(high, 2 + i, axis=0)
    return clip01(np.stack([r, g, b], axis=2))


GENERATORS = [
    ("postfreeze_curved_ribbons", "structural", postfreeze_curved_ribbons),
    ("postfreeze_rotated_boxes", "structural", postfreeze_rotated_boxes),
    ("postfreeze_sparse_paths", "structural", postfreeze_sparse_paths),
    ("postfreeze_wave_interference", "texture", postfreeze_wave_interference),
    ("postfreeze_crosshatch", "texture", postfreeze_crosshatch),
    ("postfreeze_multiscale_field", "texture", postfreeze_multiscale_field),
]
