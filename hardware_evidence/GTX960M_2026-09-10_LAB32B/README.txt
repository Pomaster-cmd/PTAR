LAB32B GTX960M hardware candidate — physical result pending.

Current selected baseline: LAB31G.
LAB32C was rejected on GTX960M because its expanded 32-byte axis-pair ABI regressed versus LAB31G despite lower static instruction count.

LAB32B keeps exactly the LAB31G 16-byte inverse+half-inverse constant ABI and changes only the axis construction expression. Full software admission is PASS: CPU/WARP parity, canonical and six-resolution float bit-exact equivalence, 42/42 UNORM8 corpus exact (0 different bytes across 13,934,592), and synthetic stress exact.

Codegen: LAB31G 53 instructions / 2568 B; LAB32B 52 instructions / 2548 B. Both have 0 DIV / 0 UDIV and texture contract 1 GatherGreen + 4 SampleLevel + 0 UAV.

The audited physical comparison pack is ready. It measures LAB31G/LAB32B/K185 in one execution with 60 warmup triads and 600 measured triads over six balanced permutations. Physical GTX960M execution is the only remaining admission gate.

Production refs main and SOURCE were not modified.
