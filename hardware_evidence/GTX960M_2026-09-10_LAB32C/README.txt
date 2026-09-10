LAB32C GTX960M rejection evidence.

Result: REJECT_LAB32C_KEEP_LAB31G.
Physical run: NVIDIA GeForce GTX 960M, 1280x720 -> 1920x1080, 60 warmup triads + 600 measured triads, six permutations balanced 100x each.
Overall: LAB31G median 0.753552 ms; LAB32C median 0.754368 ms; paired LAB31G-LAB32C median -0.001968 ms; paired speedup median -0.301209378%; LAB32C wins 217, LAB31G wins 381, ties 2.
Bootstrap 95% CI paired speedup: [-0.392620164%, -0.207672656%]. Two-sided sign-test p=1.98625563834e-11.
K185 robust two-segment L1 transition is between triads 149 and 150; triad 150 is mixed (LAB31G fast, LAB32C/K185 slow) and excluded from stable-state interpretation.
Stable fast 0..149: median paired speedup -0.633421804%, LAB32C 21 wins / LAB31G 128 / 1 tie.
Stable slow 151..599: median paired speedup -0.093732691%, LAB32C 196 wins / LAB31G 252 / 1 tie.
All six balanced permutations and all ten 60-triad blocks have negative median LAB31G-LAB32C delta.
Conclusion: the static reduction from 53 to 51 DXBC instructions does not produce a real GTX960M gain; LAB31G remains the selected experimental baseline.
Production refs main and SOURCE were not modified.
Source result archive SHA256: ea47a0296bd990d63567e97ee76957ecb64b21453a9e63725def24e893ae8a0d
