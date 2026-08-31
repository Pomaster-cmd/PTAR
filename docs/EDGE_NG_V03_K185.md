# EDGE-NG v03 K185

## Decision

EDGE-NG v03 keeps the v02 G5 orientation mechanism and replaces only the directional reconstruction kernel.

Texture budget is unchanged: 1 GatherGreen + 4 SampleLevel.

The new kernel is a fixed four-tap Keys-style cubic convolution with `a=-1.85`.

## Selection

Fine sweep on the 15-case Protocol-B EDGE_CORE set: -2.20 .. -1.60, step 0.01.
Candidates within 0.0001 mean SSIM of the best are retained; among them the lowest out-of-range component incidence wins, with PSNR as tie-breaker.

Selected: `a=-1.85`.

## EDGE_CORE

- mean SSIM: 0.916782254
- mean PSNR: 24.808745 dB
- min SSIM: 0.856224141
- edge-mask PSNR: 22.977193 dB

Versus EDGE-NG v02 G5:
- delta SSIM: +0.014091830
- delta PSNR: +0.687396 dB
- delta min SSIM: +0.036200531

Versus cubic4 separable on EDGE_CORE:
- delta SSIM: +0.007815710
- delta PSNR: +0.486021 dB
- delta min SSIM: +0.018852736

## Specialization

K185 is an EDGE expert, not a universal scaler. Full-42 regression scores are recorded but are not the EDGE selection objective. Family-level evidence is in `FAMILY_COMPARISON_VS_CUBIC4.csv`.

## Overshoot

The strong negative lobes can exceed [0,1]. The RGB8 benchmark clips on storage. Runtime behavior must be tied to the actual RTV format; do not silently add a clamp to floating/HDR paths.
