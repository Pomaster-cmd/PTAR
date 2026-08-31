# EDGE-NG v01 — recovery-neutral successor

EDGE-NG v01 is new PTAR code created after the historical EDGE v03 source became unavailable.
It must never be described as a reconstruction of EDGE v03.

## Why it exists

The PTAR project must remain developable even if the old EDGE v03 implementation is never recovered.
EDGE v03 stays frozen in the historical registry; EDGE-NG evolves independently and can later be compared against a recovered v03.

## Mapping

Exact fixed scale 1.5x with grid-aligned coordinates:
`source_coord = output_index / 1.5`.
This produces integer, 1/3 and 2/3 phases exactly.

## Direction selection

- Rec.709 luminance coefficients on current sRGB/code values;
- central axial differences at floor(source coordinate);
- reconstruction on the axis with the larger absolute gradient;
- linear filtering only on the orthogonal coordinate.

The intent is to place STEP-WENO across the dominant local discontinuity rather than leaving the discontinuity to ordinary interpolation.

## Confidence

EDGE-NG v01 uses a parameter-free local confidence:
`max(|gx|,|gy|) / (local 5-tap luminance range + 1e-8)`, saturated to [0,1].

This is a new definition and is not claimed to match historical EDGE confidence.

## Modes

- BASELINE: ideal cubic combination;
- SW1: p=1, no clamp;
- SW2: p=2 + central monotone clamp;
- SW3: BASELINE -> SW2 using EDGE-NG confidence.

## Acceptance

Primary tuning is done on the EDGE_CORE subset of PTAR-PERCEPTUAL-V1-A:
- curves_high_frequency
- natural_edges
- silhouette_edge
- thin_oblique_edges
- ui_text

Raster/pixel-art families remain the responsibility of RASTER, not EDGE.
Natural texture families are recorded as regressions but are not the primary EDGE tuning objective.
