LAB32B GTX960M hardware candidate — PHYSICAL VERDICT: REJECT, KEEP LAB31G.

Current selected baseline remains LAB31G.
LAB32C was already rejected on GTX960M because its expanded 32-byte axis-pair ABI regressed versus LAB31G despite lower static instruction count.

LAB32B kept exactly the LAB31G 16-byte inverse+half-inverse constant ABI and changed only the axis construction expression. Full software admission passed: CPU/WARP parity, canonical and six-resolution float bit-exact equivalence, 42/42 UNORM8 corpus exact (0 different bytes across 13,934,592), and synthetic stress exact.

Codegen: LAB31G 53 instructions / 2568 B; LAB32B 52 instructions / 2548 B. Both have 0 DIV / 0 UDIV and texture contract 1 GatherGreen + 4 SampleLevel + 0 UAV.

Physical GTX960M same-run result, 60 warmup + 600 measured triads over six balanced permutations:
- overall paired median LAB31G-LAB32B = +0.002336 ms, nominal LAB32B speedup +0.332638%; 403 wins / 195 losses / 2 ties; bootstrap 95% CI +0.267782% to +0.406536%; all six permutation medians positive;
- K185 robust change point between triads 157 and 158; triad 158 is mixed and excluded from per-state interpretation;
- fast state 0..157: median speedup -0.077260%, 70/87/1, bootstrap 95% CI -0.147905% to +0.041983% => neutral/slightly negative;
- slow state 159..599: median speedup +0.536544%, 332/108/1, bootstrap 95% CI +0.433158% to +0.637023% => positive.

The preregistered promotion rule required a reproducible gain across K185-defined P-state regimes, with neutral or negative meaning keep LAB31G. LAB32B therefore does not replace LAB31G despite its significant sustained/overall gain. This avoids promoting a codegen win that is not state-robust.

Source result archive SHA-256: 5f6ba2211b557844e702c9581c0d620182328f251d57d85fd099f5f8a1a56a6f.
Raw CSV, summary, block medians and detailed statistical analysis are archived beside this file.

Production refs main and SOURCE were not modified.
