# Historical vs NG branch policy

## Historical branch
- MoE v07
- NATURAL v02
- EDGE v03
- RASTER v03

Recovered historical results remain append-only facts.
Missing source code is never silently recreated.

## NG branch
New code written after recovery loss uses an `NG` identifier until promoted through a documented decision.

Current new branch:
- EDGE-NG v01

A future recovery of EDGE v03 does not invalidate EDGE-NG. It creates an opportunity for direct A/B comparison.
