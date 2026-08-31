# Architecture Baseline

## PTAR MoE v07

```text
PTAR
├── NATURAL v02       [LOCKED]
├── EDGE v03          [LOCKED BASELINE]
│   └── STEP-WENO     [EXPERIMENT]
│       ├── SW1
│       ├── SW2
│       └── SW3
└── RASTER v03        [LOCKED]
```

## Non-negotiable runtime boundary

PTAR is proprietary and autonomous.

NVIDIA NIS is not part of PTAR:
- no shader reuse;
- no runtime fallback;
- no NIS tables;
- no NIS dependency;
- no NIS path in SAFE mode.

NIS45 exact may be used only as an external quality/performance comparison target.

## Current unresolved gate

STEP-WENO is formally validated but not yet empirically accepted into PTAR V1 because the original PTAR corpus/harness
that produced the historical benchmark series is not currently available.
