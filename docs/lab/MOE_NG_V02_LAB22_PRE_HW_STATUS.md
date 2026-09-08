# PTAR-NG MoE v02 LAB22 — pre-hardware status

Status: **LAB22 software/WARP validated; physical GTX 960M timing pending.**

This document records the evidence immediately before the single targeted LAB22 hardware run. It does not promote LAB22 to `main` and does not modify the validated LAB07 runtime integration.

## Frozen references

- LAB07 canonical GTX 960M runtime DXBC SHA-256: `caa7352d84c1a9ea840ff0f783323478a4f1532a949a1d3640ba4c04b441ebd1`.
- LAB22 research source commit: `4d33659856078554f39f02a29ea7c775761195c7`.
- LAB22 source Git blob: `6800a01a4b383ddd2bdf7f3c8b625265a54205e2`.
- LAB22 canonical research/runtime DXBC SHA-256: `148a26f453a49fa319ea7aa7a448ef30b01dece508fccc6bd21f4fdae10926b8`.
- K185 control DXBC SHA-256: `6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16`.
- Fullscreen VS SHA-256: `6328bbd87aac73b07d6112de593f781b2769381aae65fa9c2bcfa06fdc68585c`.

The compiler architecture split in the one-shot comparison pack is deliberate: LAB07 is rebuilt with the x86 FXC path that reproduces the already GTX-validated runtime DXBC, while LAB22 is rebuilt with the x64 FXC path that reproduces the WARP-validated LAB22 research DXBC. The comparison therefore measures the two exact candidate binaries of interest rather than recompiling either into a different DXBC.

## LAB07 physical GTX 960M evidence

Previous autonomous LAB07 run:

- parity: 42/42 PASS;
- global maximum error: 1 LSB;
- 42/42 non-zero GPU forensic PNG outputs;
- 512/512 paired samples slower than K185;
- overall median: LAB07 `1.063184 ms`, K185 `0.699728 ms`, overhead `0.363456 ms`.

The timing trace contains an abrupt power/frequency regime change near paired sample 213, so the overall absolute median is not treated as a stationary measurement. Approximate stationary regions were:

- early region: LAB07 `~0.814448 ms`, K185 `~0.498240 ms`;
- late region: LAB07 `~1.065440 ms`, K185 `~0.704112 ms`.

This is why LAB22 is not evaluated by comparing two separate runs.

## LAB22 software/WARP evidence

LAB22 phase-fuses the Hermite phase selection while preserving LAB07's architecture, exact x1.5 mapping, MC limiter, directional decision, local shape clamp, and texture contract.

Static DXBC comparison under the LAB22 research protocol:

- LAB07: 73 executable instructions, 3332-byte CSO;
- LAB22: 66 executable instructions, 3032-byte CSO;
- delta: -7 instructions (~9.6%), -300 bytes (~9.0%);
- both: 1 Gather4 + 4 SampleLevel + 0 UAV.

WARP gates:

- direct canonical LAB07 vs LAB22 max absolute difference: `0`;
- four additional deterministic probes (constant, affine, steps, high-frequency/noise): max absolute difference `0`;
- independent CPU-reference parity remains below the established `2e-6` float tolerance (observed max approximately one float32 ULP).

Therefore LAB22 has, within the tested software/WARP domain, the same reconstruction output as LAB07. The remaining question is physical Maxwell execution cost.

## One-shot physical comparison protocol

Artifact-producing workflow run: `34264643779`, successful.

Pack source commit: `e0e07d506fb2803a8ea35e76721dd9bbb1b6b5e7` on `lab/moe-ng-v02-lab22-hw-compare`.

The one-click hardware pack performs two phases in a single user action:

1. Full LAB22 autonomous validation: 42-case GPU/CPU parity, 42 forensic PNG persistence checks, LAB22 vs K185 timing.
2. Same-run LAB07/LAB22/K185 comparator: 600 triads using all six shader execution permutations equally.

The six-permutation protocol ensures that LAB07, LAB22 and K185 occupy first, second and third timing position equally. The decisive metric is the paired `LAB22 - LAB07` delta from each triad, which remains meaningful even if the GPU changes P-state during the run.

The comparator is `/MT`, PE32+ x64, Windows subsystem/OS target 6.03, and imports only `d3d11.dll`, `dxgi.dll`, `bcrypt.dll`, and `KERNEL32.dll`. No D3DCompiler or dynamic CRT dependency is present.

## Admission rule after physical run

LAB22 is eligible to replace LAB07 as the v02 runtime candidate only if:

- full 42-case physical parity remains within the established <=1 LSB gate;
- all 42 GPU PNGs persist non-zero;
- no autonomous-runtime dependency regression appears;
- same-run paired timing shows a repeatable reduction vs LAB07, not merely a different global median caused by power-state drift.

If the same-run delta is neutral or worse, LAB22 remains a useful static simplification but is not promoted on performance grounds.
