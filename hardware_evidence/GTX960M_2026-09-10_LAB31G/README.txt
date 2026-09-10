LAB31G GTX960M selection evidence.

Result: SELECT_LAB31G_OVER_LAB30C.
Physical run: NVIDIA GeForce GTX 960M, 1280x720 -> 1920x1080, 60 warmup triads + 600 measured triads, six permutations balanced 100x each.
Overall paired result: LAB31G wins 600/600, median delta 0.020416 ms, median paired speedup 2.819340047%.
P-state split from K185 robust two-segment L1 change point is between triads 148 and 149. Fast regime median paired speedup 4.0993%; slow regime 2.5201%; all samples remain favorable to LAB31G.
Software admission: PASS with corrected ABI-aware harness, canonical/high-frequency float bit-exact, 42-case UNORM8 corpus PASS, synthetic stress PASS.
Codegen: LAB30C 55 instructions / 2632 B / 3 DIV; LAB31G 53 instructions / 2568 B / 0 DIV; texture contract unchanged (1 GatherGreen + 4 SampleLevel + 0 UAV).
Production refs main and SOURCE were not modified.
