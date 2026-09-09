# PTAR-NG MoE v02 LAB24 GTX 960M hardware result

Status: HARDWARE VALIDATED FOR CORRECTNESS; PERFORMANCE DOES NOT JUSTIFY REPLACING LAB22.

## Provenance

- GPU: NVIDIA GeForce GTX 960M
- Runtime mode: precompiled DXBC only
- LAB24 DXBC SHA-256: `fdf85502f95404d99ac308f5ed67faff4e0d326550ba8502f722279ab406a618`
- LAB22 DXBC SHA-256: `148a26f453a49fa319ea7aa7a448ef30b01dece508fccc6bd21f4fdae10926b8`
- User hardware result archive SHA-256: `e3efc0fd01bc84b4eaa510fa9213b97ca7dc753ed1f5fb22382df41dbcb6d99e`
- Comparator protocol: 600 triads, six rotating permutations, LAB22/LAB24/K185 in the same run.
- LAB24 host ABI: cb0.xy = input size; cb0.zw = reciprocal input size.

## Full 42-case correctness validation

- parity cases: 42
- failed cases: 0
- global max absolute error: 1 LSB
- global mismatch fraction: 0.039443422527
- GPU PNG persistence: PASS, 42 non-zero PNGs
- LAB24 GPU outputs are byte-identical to the corresponding 42 hardware LAB22 GPU outputs from the prior validated run.

Full-validator timing:

- LAB24 median: 0.974048 ms
- LAB24 p95: 0.9818064 ms
- LAB24 p99: 0.98398944 ms
- K185 median: 0.695504 ms
- paired LAB24-K185 median: +0.271200 ms
- K185 faster: 512/512 pairs

## Same-run LAB22 vs LAB24 timing

Overall 600-triad summary:

- LAB24 median: 0.975600 ms
- LAB22 median: 0.975040 ms
- K185 median: 0.698672 ms
- paired `LAB22 - LAB24` median: +0.000800 ms
- paired `LAB22 - LAB24` mean: +0.000851733 ms
- paired p95: +0.006016 ms
- LAB24 faster: 348
- LAB22 faster: 250
- ties: 2

The apparent overall LAB24 advantage is only about 0.082% by paired median and is not stable across the GPU power-state transition.

### Power-state split

A clear frequency/power-state transition occurs at sample 156. At that sample, K185 jumps from roughly 0.500 ms to 0.703 ms and LAB22 transitions during the triad, so sample 156 must not be treated as stationary timing evidence.

Stationary pre-transition samples 0-155:

- LAB24 median: 0.739360 ms
- LAB22 median: 0.742688 ms
- K185 median: 0.499488 ms
- paired `LAB22 - LAB24` median: +0.003392 ms
- approximate LAB24 gain vs LAB22: 0.457%
- LAB24 faster: 154/156

Stationary post-transition samples 157-599:

- LAB24 median: 0.976832 ms
- LAB22 median: 0.976512 ms
- K185 median: 0.700928 ms
- paired `LAB22 - LAB24` median: -0.000608 ms
- approximate LAB24 change vs LAB22: -0.062%
- LAB24 faster: 193, LAB22 faster: 248, ties: 2

## Decision

LAB24 proves that moving reciprocal input size to the host can remove all four pixel-shader division instructions while preserving the exact selected reconstruction on the tested software and GTX 960M paths. However, on Maxwell the measured benefit is not stable: there is a small low-power-state gain and essentially no gain in the later steady regime.

Therefore:

- LAB24 is accepted as a valid zero-division experimental implementation.
- LAB24 is **not** promoted over LAB22 as the performance baseline.
- LAB22 remains the selected v02 candidate because its earlier ~7.5% hardware win over LAB07 is large and stable, while LAB24 is effectively performance-neutral against LAB22.
- The next optimization target must reduce actual ALU/instruction work, not merely exchange `div` for host-supplied reciprocal/multiplication.
