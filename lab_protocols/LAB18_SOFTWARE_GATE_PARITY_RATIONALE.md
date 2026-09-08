# LAB18 software gate parity rationale

Status: implementation-validation note. This does not retune or modify the frozen LAB18 candidate.

The frozen research implementation forms directional luma slopes as `Luma(f1)-Luma(f0)` through the LAB14 feature path. The HLSL runtime implementation forms the algebraically equivalent `Luma(f1-f0)` from already-available directional differences.

In finite float32 arithmetic these expressions can differ by a few ULPs. The LAB18 support ratios can amplify that tiny slope rounding when their denominator is near zero, so the raw admission scalar can differ by more than the final-image tolerance even when the actual RGB perturbation remains below it.

The software parity gate therefore keeps the established final-output absolute tolerance unchanged at `2e-6` and adds an equally strict `2e-6` hard bound on the effective reconstruction contribution `abs((MC-v01) * (gate_shader-gate_frozen))`. The raw gate maximum remains recorded as a diagnostic and is not used as a hard pass/fail criterion.

D3D11 sampling, GatherGreen ordering, shader compilation, texture footprint, and actual DXBC execution remain independently covered by the Windows FXC + WARP gate.
