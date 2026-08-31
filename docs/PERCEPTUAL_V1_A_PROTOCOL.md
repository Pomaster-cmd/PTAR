# PTAR-PERCEPTUAL-V1-A Protocol

Version date: 2026-08-17

## Geometry
- HR reference: 288×288 RGB8
- LR input: 192×192 RGB8
- scale: exactly 1.5×

## Input derivation
scikit-image 0.26.0
`transform.resize`
- order=3
- mode=reflect
- anti_aliasing=True
- preserve_range=True
- clip=True
- values are processed in sRGB/code-value domain; no linearization is applied.

This is Protocol A. A future linear-light protocol must use a different corpus ID.

## Acceptance use
This corpus may be used for new PTAR development/acceptance.
It is not the historical corpus and cannot reproduce the historical 64/64 etc. claims.

## Baseline outputs
Nearest, bilinear and bicubic outputs bundled here are pipeline sanity controls only.
They are not PTAR variants.
