#!/usr/bin/env python3
"""LAB18 frozen admission shader algebra parity.

The reference path is the already-frozen LAB18 research implementation
(q10 v01/LAB07 outputs + q14 features + frozen gate). The candidate path is a
literal float32 mirror of ptar_moe_ng_v02_lab18_admission_ps.hlsl. Sampling is
shared here intentionally; D3D11 sampling/gather behavior is covered separately
by the WARP runtime gate.

The raw admission scalar is recorded diagnostically but is not itself a hard
parity gate. The frozen research path forms directional luma as Luma(f1)-
Luma(f0), while the runtime implementation uses the algebraically equivalent
Luma(f1-f0). Float32 reassociation can amplify tiny slope-rounding differences
inside support ratios when the denominator is very small. The hard invariant is
therefore the actual contribution of that scalar to the RGB reconstruction,
using the same absolute tolerance as final-output parity. This does not relax
or raise the established output tolerance.
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import numpy as np

import lab_moe_ng_v02_feature_audit as q14
import lab_moe_ng_v02_hermite_sweep as h5
import lab_moe_ng_v02_postfreeze_cases as c19
import lab_moe_ng_v02_v01_quality as q10

F = np.float32
LUMA = np.asarray([0.2126, 0.7152, 0.0722], dtype=np.float32)
EPS = F(1.0e-6)


def sat(x):
    return np.clip(np.asarray(x, dtype=np.float32), F(0), F(1)).astype(np.float32)


def mc_slope(a, b):
    a = np.asarray(a, dtype=np.float32); b = np.asarray(b, dtype=np.float32)
    avg = F(0.5) * (a + b)
    lim = F(2.0) * np.minimum(np.abs(a), np.abs(b))
    limited = np.minimum(np.maximum(avg, -lim), lim).astype(np.float32)
    return (limited * (a * b >= F(0)).astype(np.float32)).astype(np.float32)


def hermite13(f0, f1, m0, m1):
    h = (F(20)*f0 + F(4)*m0 + F(7)*f1 - F(2)*m1) * F(1.0/27.0)
    return np.minimum(np.maximum(h, np.minimum(f0, f1)), np.maximum(f0, f1)).astype(np.float32)


def hermite23(f0, f1, m0, m1):
    h = (F(7)*f0 + F(2)*m0 + F(20)*f1 - F(4)*m1) * F(1.0/27.0)
    return np.minimum(np.maximum(h, np.minimum(f0, f1)), np.maximum(f0, f1)).astype(np.float32)


def shader_mirror(lr):
    lr = np.asarray(lr, dtype=np.float32)
    h, w, _ = lr.shape
    oh, ow = (h * 3) // 2, (w * 3) // 2
    oy, ox = np.mgrid[0:oh, 0:ow]
    oxu = ox.astype(np.uint32); oyu = oy.astype(np.uint32)
    sx = ox.astype(np.float32) * F(2.0/3.0)
    sy = oy.astype(np.float32) * F(2.0/3.0)
    fx = np.floor(sx).astype(np.int32); fy = np.floor(sy).astype(np.int32)

    x0 = np.clip(fx, 0, w-1); x1 = np.clip(fx+1, 0, w-1)
    y0 = np.clip(fy, 0, h-1); y1 = np.clip(fy+1, 0, h-1)
    g = lr[..., 1]
    tl = g[y0, x0]; tr = g[y0, x1]; bl = g[y1, x0]; br = g[y1, x1]
    gx = (tr + br) - (tl + bl); gy = (bl + br) - (tl + tr)
    agx = np.abs(gx); agy = np.abs(gy); use_x = agx >= agy

    bx = np.where(use_x, fx.astype(np.float32), sx)
    by = np.where(use_x, sy, fy.astype(np.float32))
    ax = use_x.astype(np.float32); ay = (~use_x).astype(np.float32)
    fm1 = h5.sample(lr, bx-ax, by-ay)
    f0 = h5.sample(lr, bx, by)
    f1 = h5.sample(lr, bx+ax, by+ay)
    f2 = h5.sample(lr, bx+F(2)*ax, by+F(2)*ay)
    phase = np.where(use_x, oxu % 3, oyu % 3)
    nz = (phase != 0)[..., None]

    k13 = F(-0.274074074074074)*fm1 + F(0.877777777777778)*f0 + F(0.533333333333333)*f1 + F(-0.137037037037037)*f2
    k23 = F(-0.137037037037037)*fm1 + F(0.533333333333333)*f0 + F(0.877777777777778)*f1 + F(-0.274074074074074)*f2
    edge = np.where((phase == 2)[..., None], k13, k23).astype(np.float32)
    raster = np.minimum(np.maximum(edge, np.minimum(f0, f1)), np.maximum(f0, f1)).astype(np.float32)
    t = np.where(phase == 1, F(2.0/3.0), F(1.0/3.0)).astype(np.float32)
    bilinear = (f0 * (F(1)-t[..., None]) + f1 * t[..., None]).astype(np.float32)
    natural = (bilinear * F(0.30) + raster * F(0.70)).astype(np.float32)

    local_range = np.maximum(np.maximum(tl,tr),np.maximum(bl,br)) - np.minimum(np.minimum(tl,tr),np.minimum(bl,br))
    gradient = F(0.5) * np.maximum(agx, agy)
    diagonal = F(0.5) * np.abs((tl+br)-(tr+bl))
    coherence = np.abs(agx-agy)/(agx+agy+EPS)
    range_conf = sat((local_range-F(0.01))/F(0.10))
    coherence_conf = sat((coherence-F(0.25))/F(0.45))
    diag_ratio = diagonal/(gradient+EPS)
    axis_conf = F(1)-sat((diag_ratio-F(0.08))/F(0.45))
    raster_weight = range_conf*coherence_conf*axis_conf
    edge_base = sat((gradient-F(0.02))/F(0.10))
    edge_weight = (F(1)-raster_weight)*edge_base
    natural_weight = F(1)-raster_weight-edge_weight
    v01 = natural*natural_weight[...,None] + edge*edge_weight[...,None] + raster*raster_weight[...,None]
    v01 = sat(v01)

    d0 = f0-fm1; d1 = f1-f0; d2 = f2-f1
    m0 = mc_slope(d0,d1); m1 = mc_slope(d1,d2)
    mc = np.where((phase == 1)[...,None], hermite23(f0,f1,m0,m1), hermite13(f0,f1,m0,m1)).astype(np.float32)

    ld0 = np.sum(d0[...,:3]*LUMA, axis=2, dtype=np.float32)
    ld1 = np.sum(d1[...,:3]*LUMA, axis=2, dtype=np.float32)
    ld2 = np.sum(d2[...,:3]*LUMA, axis=2, dtype=np.float32)
    ad0 = np.abs(ld0); ad1 = np.abs(ld1); ad2 = np.abs(ld2)
    central = ad1/(ad0+ad1+ad2+EPS)
    outer_balance = np.minimum(ad0,ad2)/(np.maximum(ad0,ad2)+EPS)
    low_coh = F(1)-sat((coherence-F(0.75))/F(0.18))
    balanced = sat((outer_balance-F(0.10))/F(0.15))
    central_support = sat((central-F(0.10))/F(0.15))
    admission = F(0.50)*low_coh*balanced*central_support
    hybrid = sat(v01 + (mc-v01)*admission[...,None])
    return np.where(nz, hybrid, f0).astype(np.float32), admission.astype(np.float32)


def frozen_reference(lr):
    v01, mc = q10.reconstruct_pair(lr)
    feat = q14.features(lr)
    low_coh = F(1)-sat((feat['coh']-F(0.75))/F(0.18))
    balanced = sat((feat['outer_balance']-F(0.10))/F(0.15))
    central_support = sat((feat['central']-F(0.10))/F(0.15))
    admission = F(0.50)*low_coh*balanced*central_support
    ref = sat(v01 + (mc-v01)*admission[...,None])
    return ref.astype(np.float32), admission.astype(np.float32)


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--out',required=True); ap.add_argument('--tol',type=float,default=2.0e-6); a=ap.parse_args()
    out=Path(a.out); out.mkdir(parents=True,exist_ok=True)
    rows=[]; worst=0.0; worst_gate=0.0; worst_gate_effect=0.0
    for family, group, fn in c19.GENERATORS:
        for i in range(6):
            hr=c19.clip01(fn(i)); lr=c19.downsample(hr)
            ref, rg=frozen_reference(lr); got, gg=shader_mirror(lr)
            v01, mc=q10.reconstruct_pair(lr)
            diff=np.abs(got-ref); gdiff=np.abs(gg-rg)
            # Raw gate ratios can be ill-conditioned near zero support. Measure
            # the actual reconstruction perturbation caused by that difference.
            gate_effect=np.abs((mc-v01)*gdiff[...,None])
            rec={
                'family':family,'group':group,'case_index':i+1,
                'max_abs':float(diff.max()),'mean_abs':float(diff.mean()),
                'gate_max_abs':float(gdiff.max()),
                'gate_effect_max_abs':float(gate_effect.max())
            }
            rows.append(rec)
            worst=max(worst,rec['max_abs'])
            worst_gate=max(worst_gate,rec['gate_max_abs'])
            worst_gate_effect=max(worst_gate_effect,rec['gate_effect_max_abs'])
            print(family,i+1,f"max={rec['max_abs']:.9g}",f"gate={rec['gate_max_abs']:.9g}",f"effect={rec['gate_effect_max_abs']:.9g}")
    with (out/'PER_CASE.csv').open('w',newline='',encoding='utf-8') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    summary={
        'protocol':'LAB18_FROZEN_SHADER_ALGEBRA_PARITY',
        'cases':len(rows),
        'tolerance_abs':a.tol,
        'max_abs':worst,
        'gate_max_abs_diagnostic':worst_gate,
        'gate_effect_max_abs':worst_gate_effect,
        'raw_gate_is_hard_gate':False,
        'pass':bool(worst<=a.tol and worst_gate_effect<=a.tol),
        'sampling_scope':'shared CPU sampler; D3D11 footprint is validated separately by WARP',
        'gate_audit_rationale':'Luma(f1)-Luma(f0) vs Luma(f1-f0) is algebraically equivalent; float32 reassociation is audited by its reconstruction effect at the unchanged output tolerance.'
    }
    (out/'SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8'); print(json.dumps(summary,indent=2))
    if not summary['pass']: raise SystemExit('LAB18 shader algebra parity FAIL')

if __name__=='__main__': main()
