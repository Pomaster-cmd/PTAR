# MoE NG v02 LAB24 — host inverse-size software gate

Status: **SOFTWARE/WARP VALIDATED — PHYSICAL GPU NOT YET EXECUTED**

LAB24 starts from the hardware-validated LAB22 phase-fused MC shader and changes only texture-coordinate normalization: reciprocal input size is supplied by the host in `cb0.zw`, eliminating pixel-shader `div` operations.

## Frozen identifiers

- LAB22 DXBC SHA-256: `148a26f453a49fa319ea7aa7a448ef30b01dece508fccc6bd21f4fdae10926b8`
- LAB24 source blob: `823726cc9dbdf669815a4ce9f1cabb462f147a15`
- LAB24 DXBC SHA-256: `fdf85502f95404d99ac308f5ed67faff4e0d326550ba8502f722279ab406a618`
- LAB24 primary software run: `34316635339` — PASS
- LAB24 dimension sweep run: `34316874238` — PASS

## Static DXBC delta

| metric | LAB22 | LAB24 |
|---|---:|---:|
| executable instruction proxy | 66 | 66 |
| `div` | 4 | 0 |
| `mul` | 9 | 13 |
| `mad` | 3 | 3 |
| Gather4 | 1 | 1 |
| SampleLevel | 4 | 4 |
| UAV | 0 | 0 |
| CSO bytes | 3032 | 3036 |

LAB24 therefore removes all four coordinate divisions without increasing total executable-instruction count or texture traffic.

## WARP equivalence

Primary software gate:

- independent CPU-reference max absolute error: `1.1920928955078125e-7`
- LAB22 vs LAB24 direct canonical WARP difference: **0**
- deterministic constant / affine / step / high-frequency-noise probes: **all bit-exact**

Post-gate dimension sweep used deterministic mixed-frequency inputs and compared LAB22/LAB24 output bytes directly:

- 24x24 -> 36x36: max abs **0**
- 64x64 -> 96x96: max abs **0**
- 192x192 -> 288x288: max abs **0**
- 320x180 -> 480x270: max abs **0**
- 640x360 -> 960x540: max abs **0**
- 1280x720 -> 1920x1080: max abs **0**

Across all six dimensions, differing float values: **0**.

WARP wall-clock figures are deliberately not treated as GPU-performance evidence.

## Runtime ABI condition

For LAB24 only:

- `cb0.xy = (inputWidth, inputHeight)`
- `cb0.zw = (1/inputWidth, 1/inputHeight)`

LAB22 and K185 retain the historical normal constants in the hardware comparator. Constant-buffer setup happens before the timestamped draw, so the same-run timing measures shader execution rather than host-side constant preparation.

## Decision

LAB24 passes the software gate and is eligible for one targeted GTX 960M test against LAB22 and K185. No stable/main promotion is authorized by this record.
