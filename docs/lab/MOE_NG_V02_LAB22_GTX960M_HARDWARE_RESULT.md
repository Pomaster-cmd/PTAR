# MoE NG v02 LAB22 — GTX 960M hardware result

Status: **HARDWARE VALIDATED / QUALITY-EQUIVALENT TO LAB07 ON VALIDATION CORPUS**

This record captures the physical GTX 960M result produced by the one-shot LAB22 pack after software/WARP validation. It does not promote LAB22 to `main`.

## Provenance

- Hardware: NVIDIA GeForce GTX 960M
- OS/runtime target: Windows 8.1 x64 / Direct3D 11 / SM5
- LAB07 canonical deployed DXBC SHA-256: `caa7352d84c1a9ea840ff0f783323478a4f1532a949a1d3640ba4c04b441ebd1`
- LAB22 validated DXBC SHA-256: `148a26f453a49fa319ea7aa7a448ef30b01dece508fccc6bd21f4fdae10926b8`
- LAB22 research source blob: `6800a01a4b383ddd2bdf7f3c8b625265a54205e2`
- Hardware pack branch: `lab/moe-ng-v02-lab22-hw-compare`
- Hardware pack commit: `e0e07d506fb2803a8ea35e76721dd9bbb1b6b5e7`
- Pack build workflow run: `34264643779` (PASS)

## Full LAB22 autonomous parity run

- parity cases: **42/42 PASS**
- parity failed cases: **0**
- global max absolute error: **1 LSB**
- global mismatch fraction: **0.039443422527**
- GPU PNG persistence: **42/42 non-zero**
- texture path: **1 Gather4 + 4 SampleLevel + 0 UAV**
- runtime compiler dependency: **none**

LAB22 full-run timing:

- LAB22 median: **0.972352 ms**
- LAB22 P95: **0.9785616 ms**
- LAB22 P99: **0.9805264 ms**
- K185 median: **0.693824 ms**
- median LAB22-K185 overhead: **0.278528 ms**
- 512/512 paired samples: K185 faster, as expected for the control shader

## Same-run balanced LAB07 / LAB22 / K185 comparison

Protocol: 600 triads, six rotating permutations, balanced order.

Medians:

- LAB07: **1.052960 ms**
- LAB22: **0.973568 ms**
- K185: **0.696640 ms**

Paired LAB22 versus LAB07:

- mean delta LAB22-LAB07: **-0.076854133 ms**
- median delta LAB22-LAB07: **-0.077104 ms**
- P95 delta LAB22-LAB07: **-0.0713264 ms**
- LAB22 faster: **600/600**
- LAB07 faster: **0/600**
- ties: **0/600**

Derived:

- LAB22 reduces median shader time by about **7.54%** versus LAB07 in the same run.
- LAB07 median overhead over K185: **0.356320 ms**.
- LAB22 median overhead over K185: **0.276928 ms**.
- LAB22 therefore removes about **22.28%** of LAB07's overhead above the K185 control.

## P-state / frequency regime observation

A frequency/power-state transition is visible near triad 198-199, but the same-run result is robust because all three shaders are interleaved within every triad and all six execution orders rotate.

Before the transition (samples 0-197, medians):

- LAB07: **0.813536 ms**
- LAB22: **0.740976 ms**
- K185: **0.497840 ms**
- LAB22 gain versus LAB07: **0.072560 ms (~8.92%)**

After the transition (samples 199-599, medians):

- LAB07: **1.054048 ms**
- LAB22: **0.975136 ms**
- K185: **0.699072 ms**
- LAB22 gain versus LAB07: **0.078912 ms (~7.49%)**

Thus the LAB22 speedup survives both power/frequency regimes.

## Real-GPU output equivalence to prior LAB07 run

The 42 LAB22 GPU PNG files were compared against the 42 GPU PNG files from the earlier validated LAB07 GTX 960M run. Result:

- **42/42 PNG files byte-identical**
- maximum per-channel pixel difference: **0**
- total differing channel values: **0**
- parity CSV: **identical**

This confirms that LAB22's phase fusion preserved the LAB07 validation output exactly on the physical GTX 960M for the full 42-case corpus.

## Decision

LAB22 supersedes LAB07 as the **best current v02 research/runtime candidate** because it preserves LAB07 output while materially reducing GTX 960M cost.

No promotion to stable `main` is authorized by this record. Further optimization may proceed from LAB22 on lab branches only.
