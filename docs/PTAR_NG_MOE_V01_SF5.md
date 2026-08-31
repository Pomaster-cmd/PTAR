# PTAR-NG MoE v01 SF5

## Identity

This is a **new NG successor architecture**.

It is NOT a reconstruction of missing historical source code for:
- MoE v07;
- NATURAL v02;
- RASTER v03.

The historical architecture and recovered aggregate results remain append-only
facts. No newly written NG result is transferred to those historical records.

## Shared-fetch design

The complete v01 MoE uses the same logical texture footprint as validated K185:

- 1 `GatherGreen`;
- 4 `SampleLevel`;
- 1 source `Texture2D`;
- 0 UAV;
- 0 intermediate texture;
- 0 NIS runtime path.

All experts are analytic transforms of the same four color samples.

### EDGE-NG v03 K185

Unmodified hardware-validated K185 reconstruction.

### RASTER-NG v01 MCLAMP

K185 followed by a component-wise monotone clamp between central samples
`f0/f1`. Purpose: suppress cubic ringing at hard raster/pixel-art transitions.

### NATURAL-NG v01 N70

`70% MCLAMP + 30% bilinear`.

The bilinear term is derived from `f0/f1` and the fixed x1.5 phase. No extra
texture fetch is required.

### ROUTER-NG v01 R1-SOFT

Continuous convex weights derived from the existing 2x2 green gather:
- local range;
- dominant gradient;
- diagonal activity;
- axis coherence.

The initial hard-threshold prototype was discarded because route flips near a
threshold produced unacceptable float64/float32 sensitivity. The released v01
candidate uses soft weights; float64 vs float32 RGB8 semantics are <= 1 LSB.

## Permanent Protocol-B CPU result

Full 42:
- MoE mean SSIM: 0.930264824
- K185 mean SSIM: 0.926039074
- delta SSIM: +0.004225749
- delta PSNR: +0.422479 dB

Scope deltas:
- EDGE_CORE: +0.000943820 SSIM
- RASTER scope: +0.011758809 SSIM
- NATURAL scope: +0.002007691 SSIM

Mean final weights:
- NATURAL: 0.648119
- EDGE: 0.197052
- RASTER: 0.154829

Float64 vs shader-semantic float32:
- max RGB8 difference: 1 LSB
- mismatch fraction: 0.001044882

## Performance hypothesis

Because all three experts share K185's five logical texture operations, v01
should add mostly ALU/routing cost rather than the cost of three independent
upscalers. This is a hypothesis only until the GTX 960M hardware benchmark runs.

## Current gate

CPU/reference and HLSL semantic gates: complete.
Real D3D11/GTX 960M parity and paired MoE-vs-K185 timing: pending.
