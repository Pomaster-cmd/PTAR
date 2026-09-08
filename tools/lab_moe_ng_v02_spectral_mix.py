#!/usr/bin/env python3
"""LAB09: 2D multi-frequency spectral diagnostic for LAB07 always-on MC.

Purpose
-------
The existing radial native-band diagnostic reports worse spectral log-MAE for
some PTAR-GEN-FREQUENCY_MIX cases although LAB07 improves spatial PSNR and the
LAB08 pure-sine response. LAB09 separates reference-supported energy from
candidate-only spectral excess to determine whether MC creates genuine
harmonic/intermodulation energy or whether the old radial metric mostly sees a
redistribution of useful energy.

This is DIAGNOSTIC ONLY:
  * no parameter is selected or tuned;
  * A+B corpus cases are reported as train diagnostics;
  * crop C is reported separately and is never used to choose anything;
  * fixed analytic multi-tone probes were defined before observing LAB09 output.
"""
import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image

import lab_moe_ng_v02_hermite_sweep as h5

LUMA = np.asarray([0.2126, 0.7152, 0.0722], dtype=np.float64)
EPS = 1.0e-30
SUPPORT_ENERGY_FRACTION = 0.99
SUPPORT_DILATION_BINS = 1
N = 288


def load_rgb(path):
    with Image.open(path) as im:
        return np.asarray(im.convert('RGB'), dtype=np.float32) / np.float32(255.0)


def luma(rgb):
    return np.sum(np.asarray(rgb, dtype=np.float64) * LUMA, axis=2)


def psnr(a, b):
    d = np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64)
    mse = float(np.mean(d * d))
    return float('inf') if mse <= 0.0 else 10.0 * math.log10(1.0 / mse)


def spectrum(rgb):
    y = luma(rgb)
    h, w = y.shape
    win = np.hanning(h)[:, None] * np.hanning(w)[None, :]
    z = (y - float(np.mean(y))) * win
    f = np.fft.fftshift(np.fft.fft2(z))
    p = np.abs(f) ** 2
    return f, p


def dilate(mask, radius=1):
    h, w = mask.shape
    out = mask.copy()
    ys, xs = np.nonzero(mask)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            yy = ys + dy
            xx = xs + dx
            ok = (yy >= 0) & (yy < h) & (xx >= 0) & (xx < w)
            out[yy[ok], xx[ok]] = True
    return out


def reference_support(ref_power, fraction=SUPPORT_ENERGY_FRACTION):
    p = np.asarray(ref_power, dtype=np.float64).copy()
    cy, cx = p.shape[0] // 2, p.shape[1] // 2
    p[cy, cx] = 0.0
    flat = p.ravel()
    total = float(np.sum(flat))
    if total <= EPS:
        return np.zeros_like(p, dtype=bool)
    order = np.argsort(flat)[::-1]
    c = np.cumsum(flat[order])
    k = int(np.searchsorted(c, fraction * total, side='left')) + 1
    mask = np.zeros(flat.size, dtype=bool)
    mask[order[:k]] = True
    return dilate(mask.reshape(p.shape), SUPPORT_DILATION_BINS)


def candidate_metrics(ref_f, ref_p, cand_f, cand_p, support):
    ref_total = float(np.sum(ref_p))
    ref_support = float(np.sum(ref_p[support]))
    cand_support = float(np.sum(cand_p[support]))
    ref_off = ref_p[~support]
    cand_off = cand_p[~support]
    positive_excess = float(np.sum(np.maximum(cand_off - ref_off, 0.0)))

    ref_mag = np.abs(ref_f[support])
    cand_mag = np.abs(cand_f[support])
    mag_nrmse = math.sqrt(float(np.sum((cand_mag - ref_mag) ** 2)) / max(float(np.sum(ref_mag ** 2)), EPS))
    cosine = float(np.sum(cand_mag * ref_mag) / math.sqrt(max(float(np.sum(cand_mag ** 2) * np.sum(ref_mag ** 2)), EPS)))

    return {
        'reference_support_capture': ref_support / max(ref_total, EPS),
        'support_power_ratio_db': 10.0 * math.log10(max(cand_support, EPS) / max(ref_support, EPS)),
        'support_magnitude_nrmse': mag_nrmse,
        'support_magnitude_cosine': cosine,
        'off_support_power_over_ref_total': float(np.sum(cand_off)) / max(ref_total, EPS),
        'off_support_positive_excess_over_ref_total': positive_excess / max(ref_total, EPS),
    }


def evaluate_triplet(ref_rgb, bil_rgb, mc_rgb):
    rf, rp = spectrum(ref_rgb)
    bf, bp = spectrum(bil_rgb)
    mf, mp = spectrum(mc_rgb)
    support = reference_support(rp)
    b = candidate_metrics(rf, rp, bf, bp, support)
    m = candidate_metrics(rf, rp, mf, mp, support)
    return b, m, {
        'mc_minus_bilinear_spurious_excess': m['off_support_positive_excess_over_ref_total'] - b['off_support_positive_excess_over_ref_total'],
        'bilinear_minus_mc_support_nrmse': b['support_magnitude_nrmse'] - m['support_magnitude_nrmse'],
        'mc_minus_bilinear_support_cosine': m['support_magnitude_cosine'] - b['support_magnitude_cosine'],
        'mc_minus_bilinear_psnr': psnr(ref_rgb, mc_rgb) - psnr(ref_rgb, bil_rgb),
    }


def corpus_cases(bgrid):
    manifest = list(csv.DictReader((bgrid / 'CORPUS_MANIFEST.csv').open(newline='', encoding='utf-8')))
    rows = []
    for row in manifest:
        if row.get('source_id') != 'PTAR-GEN-FREQUENCY_MIX':
            continue
        ref = load_rgb(bgrid / row['reference_path'])
        lr = load_rgb(bgrid / row['input_path'])
        bil, experts, _, _ = h5.planes(lr)
        mc = experts['mc']
        bm, mm, delta = evaluate_triplet(ref, bil, mc)
        rows.append({
            'case_id': row['case_id'],
            'crop_tag': row.get('crop_tag', ''),
            'split': 'holdout_C' if row.get('crop_tag') == 'C' else 'train_AB',
            'bilinear_psnr': psnr(ref, bil),
            'mc_psnr': psnr(ref, mc),
            'bilinear': bm,
            'mc': mm,
            'delta': delta,
        })
    if len(rows) != 3:
        raise RuntimeError(f'expected 3 PTAR-GEN-FREQUENCY_MIX cases, found {len(rows)}')
    return rows


# Frequencies are cycles per LR pixel. Sum of amplitudes is <= 0.40 so all
# analytic signals remain in [0,1]. These probes are fixed, not tuned to LAB09.
PROBES = [
    ('x_two_tone', [(0.10, 0.00, 0.22, 0.17), (0.31, 0.00, 0.16, 0.83)]),
    ('xy_cross', [(0.12, 0.00, 0.20, 0.31), (0.00, 0.33, 0.17, 1.07)]),
    ('diag_two_tone', [(0.14, 0.09, 0.20, 0.23), (0.32, -0.11, 0.16, 0.91)]),
    ('three_way', [(0.08, 0.00, 0.15, 0.11), (0.00, 0.21, 0.13, 0.61), (0.28, 0.17, 0.11, 1.19)]),
]


def analytic_image(width, height, scale_to_lr, tones):
    y, x = np.mgrid[0:height, 0:width]
    xx = x.astype(np.float64) * scale_to_lr
    yy = y.astype(np.float64) * scale_to_lr
    z = np.full((height, width), 0.5, dtype=np.float64)
    for fx, fy, amp, phase in tones:
        z += amp * np.sin(2.0 * np.pi * (fx * xx + fy * yy) + phase)
    z = np.clip(z, 0.0, 1.0).astype(np.float32)
    return np.repeat(z[:, :, None], 3, axis=2)


def known_tone_support(tones, shape=(N, N), radius_bins=2.5):
    h, w = shape
    fy_grid = np.fft.fftshift(np.fft.fftfreq(h))
    fx_grid = np.fft.fftshift(np.fft.fftfreq(w))
    yy, xx = np.meshgrid(fy_grid, fx_grid, indexing='ij')
    mask = np.zeros(shape, dtype=bool)
    # HR frequency is LR frequency * 2/3 because one HR output pixel advances
    # 2/3 of an LR sample coordinate at exact x1.5 reconstruction.
    ry = radius_bins / h
    rx = radius_bins / w
    for fx, fy, amp, phase in tones:
        hx = fx * (2.0 / 3.0)
        hy = fy * (2.0 / 3.0)
        for sx, sy in ((hx, hy), (-hx, -hy)):
            mask |= ((xx - sx) / rx) ** 2 + ((yy - sy) / ry) ** 2 <= 1.0
    return mask


def known_support_metrics(ref_rgb, cand_rgb, tones):
    rf, rp = spectrum(ref_rgb)
    cf, cp = spectrum(cand_rgb)
    support = known_tone_support(tones, rp.shape)
    ref_total = float(np.sum(rp))
    off = ~support
    pos_excess = float(np.sum(np.maximum(cp[off] - rp[off], 0.0))) / max(ref_total, EPS)
    total_off = float(np.sum(cp[off])) / max(ref_total, EPS)
    useful_ref = float(np.sum(rp[support]))
    useful_cand = float(np.sum(cp[support]))
    return {
        'known_support_ref_capture': useful_ref / max(ref_total, EPS),
        'known_support_power_ratio_db': 10.0 * math.log10(max(useful_cand, EPS) / max(useful_ref, EPS)),
        'off_known_support_power_over_ref_total': total_off,
        'off_known_support_positive_excess_over_ref_total': pos_excess,
    }


def analytic_probes():
    out = []
    for name, tones in PROBES:
        lr = analytic_image(192, 192, 1.0, tones)
        ref = analytic_image(288, 288, 2.0 / 3.0, tones)
        bil, experts, _, _ = h5.planes(lr)
        mc = experts['mc']
        b = known_support_metrics(ref, bil, tones)
        m = known_support_metrics(ref, mc, tones)
        out.append({
            'probe': name,
            'tones': [{'fx_lr': x[0], 'fy_lr': x[1], 'amplitude': x[2], 'phase': x[3]} for x in tones],
            'bilinear_psnr': psnr(ref, bil),
            'mc_psnr': psnr(ref, mc),
            'mc_minus_bilinear_psnr': psnr(ref, mc) - psnr(ref, bil),
            'bilinear': b,
            'mc': m,
            'mc_minus_bilinear_spurious_excess': m['off_known_support_positive_excess_over_ref_total'] - b['off_known_support_positive_excess_over_ref_total'],
        })
    return out


def aggregate_corpus(rows):
    def agg(rr):
        if not rr:
            return None
        mean = lambda fn: float(sum(fn(x) for x in rr) / len(rr))
        return {
            'cases': len(rr),
            'mean_mc_minus_bilinear_psnr': mean(lambda x: x['delta']['mc_minus_bilinear_psnr']),
            'mean_mc_minus_bilinear_spurious_excess': mean(lambda x: x['delta']['mc_minus_bilinear_spurious_excess']),
            'mean_bilinear_minus_mc_support_nrmse': mean(lambda x: x['delta']['bilinear_minus_mc_support_nrmse']),
            'mean_mc_minus_bilinear_support_cosine': mean(lambda x: x['delta']['mc_minus_bilinear_support_cosine']),
            'mean_bilinear_spurious_excess': mean(lambda x: x['bilinear']['off_support_positive_excess_over_ref_total']),
            'mean_mc_spurious_excess': mean(lambda x: x['mc']['off_support_positive_excess_over_ref_total']),
        }
    return {
        'train_AB': agg([r for r in rows if r['split'] == 'train_AB']),
        'holdout_C': agg([r for r in rows if r['split'] == 'holdout_C']),
        'all': agg(rows),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bgrid', required=True)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    corpus = corpus_cases(Path(args.bgrid))
    probes = analytic_probes()
    summary = {
        'protocol': 'LAB09_2D_MULTIFREQUENCY_SPECTRAL_DIAGNOSTIC_V1',
        'acceptance_protocol': False,
        'selection_used_this_diagnostic': False,
        'selection_used_holdout': False,
        'candidate': 'LAB07_ALWAYS_ON_MC',
        'reference_support_method': {
            'source': 'HR reference only',
            'non_dc_energy_fraction': SUPPORT_ENERGY_FRACTION,
            'dilation_bins': SUPPORT_DILATION_BINS,
            'window': '2D separable Hann after mean removal',
        },
        'corpus_frequency_mix': corpus,
        'corpus_aggregate': aggregate_corpus(corpus),
        'fixed_analytic_probes': probes,
        'interpretation_signs': {
            'mc_minus_bilinear_spurious_excess': 'negative is better for MC; positive means more candidate-only off-support energy',
            'bilinear_minus_mc_support_nrmse': 'positive is better for MC',
            'mc_minus_bilinear_support_cosine': 'positive is better for MC',
        },
    }
    finite = True
    for r in corpus:
        vals = [r['bilinear_psnr'], r['mc_psnr'], *r['delta'].values(), *r['bilinear'].values(), *r['mc'].values()]
        finite = finite and all(np.isfinite(v) for v in vals)
    for r in probes:
        vals = [r['bilinear_psnr'], r['mc_psnr'], r['mc_minus_bilinear_psnr'], r['mc_minus_bilinear_spurious_excess'], *r['bilinear'].values(), *r['mc'].values()]
        finite = finite and all(np.isfinite(v) for v in vals)
    summary['diagnostic_finite'] = bool(finite)

    (out / 'SUMMARY.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))
    if not finite:
        raise SystemExit('LAB09 produced non-finite diagnostic values')


if __name__ == '__main__':
    main()
