LAB35A GTX960M hardware candidate — physical result pending.

Current hardware-selected baseline: LAB31G.

LAB35A keeps exactly LAB31G's 16-byte inverse+half-inverse ABI and compiles to the same 53 instructions / 2568-byte DXBC with the same static texture contract (1 GatherGreen + 4 SampleLevel + 0 UAV). The optimization is dynamic rather than static: on phase 0, the early-return path executes only one SampleLevel before returning; the remaining three SampleLevel instructions are after the branch. LAB31G executes two SampleLevel operations before that return.

Full software admission is PASS: CPU/WARP parity, canonical and six-resolution float bit-exact equivalence, 42/42 UNORM8 corpus exact with 0 differing bytes over 13,934,592 bytes, synthetic stress exact, and independent dynamic-path re-audit PASS.

The audited GTX960M compare pack is ready. It measures LAB31G/LAB35A/K185 in one execution with 60 warmup triads and 600 measured triads over six balanced permutations. Promotion requires a reproducible positive paired gain across K185-defined P-state regimes; otherwise LAB31G remains selected.

Production refs main and SOURCE were not modified.
