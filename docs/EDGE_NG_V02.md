# EDGE-NG v02

## G5 runtime candidate

G5 replaces the v01 floor-grid orientation decision with a 2x2 green-channel gather around the current source cell. Reconstruction remains the fixed-phase directional cubic baseline; STEP-WENO is not in the v02 runtime path.

The intended SM5 texture path is one `GatherGreen` plus four `SampleLevel` calls. Microsoft documents GatherGreen for Texture2D in Shader Model 5, returning the four green components of the bilinear footprint. The assembly gather order is used only to form unsigned axis magnitudes.

## HQ05 diagnostic

HQ05 measures orientation with four bilinear probes at +/-0.5 texel. It is a quality ceiling for this orientation family, not a runtime candidate.

## Evaluation

Both full-image EDGE_CORE metrics and a reference-derived top-20%-gradient+dilation edge mask are stored. The edge mask is evaluation-only and has no runtime role.
