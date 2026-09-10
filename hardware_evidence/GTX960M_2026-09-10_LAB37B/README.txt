LAB37B GTX960M hardware candidate — PHYSICAL VERDICT: PROMOTE LAB37B.

LAB37B replaces LAB36B as the hardware-selected PTAR-NG MoE v02 baseline for the exact x1.5 path.

Protocol: LAB37B_GTX960M_SAME_RUN_BALANCED_COMPARE
Adapter: NVIDIA GeForce GTX 960M
Resolution: 1280x720 -> 1920x1080
Warmup: 60 triads
Measured: 600 triads
Order protocol: six rotating permutations, exactly 100 each
Software correctness: PASS, bit-exact against LAB36B

Frozen shaders:
LAB36B SHA-256 = 21c7bb739779c9bf61fd4ee0d502f8a0304c9f9ac75f936128c09220c76c99c0
LAB37B SHA-256 = cd92ead859b9e7e210b96b1b811c95f91b1eba3c18c64a7cfa83e0a004c5a0b5
K185 SHA-256 = 6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16

Static DXBC:
LAB36B = 52 instructions / 2540 bytes
LAB37B = 52 instructions / 2532 bytes
Texture contract = 1 GatherGreen + 4 SampleLevel + 0 UAV
DIV/UDIV = 0
ABI = unchanged 16-byte inverse+half-inverse constants

LAB37B replaces LAB36B's phase coefficient LT + vector MOVC with ROUND_NE + vector MAD. The logical reconstruction, texture issue order, phase-zero branch, ABI and numerical output remain unchanged. Canonical float comparison is bit-exact (0/5184 values differ), multires float passes, UNORM8 corpus passes, synthetic probes pass, and WARP/reference checks pass.

Physical paired result:
- LAB36B median = 0.737024 ms
- LAB37B median = 0.718400 ms
- overall paired LAB36B-LAB37B median delta = +0.015088 ms
- paired median speedup = +2.159627182%
- bootstrap 95% CI = [2.041531094146%, 2.256160073020%]
- LAB37B wins / LAB36B wins / ties = 599 / 1 / 0
- two-sided sign-test p = 2.896723677853667e-178
- all six permutation medians are positive; all six permutation bootstrap 95% lower bounds are positive

K185 detects one abrupt P-state transition at triad 177.
Stable state 0..176: K185 median 0.494720 ms; LAB37B paired median speedup +1.575781352%; bootstrap CI [1.543026706%, 1.625909657%]; 177 candidate wins / 0 reference wins / 0 ties.
Stable state 177..599: K185 median 0.698240 ms; LAB37B paired median speedup +2.574829639%; bootstrap CI [2.473314241%, 2.686579975%]; 422 / 1 / 0.

The preregistered promotion rule requires a reproducible positive paired gain across K185-defined P-state regimes and balanced permutations. LAB37B passes every gate with large margin and therefore becomes the hardware-selected baseline.

Source results ZIP SHA-256: fe9b904bc1c1376fe7ab1f4b6f5ad42b79a3f3feac335abac51ce43849f5cde4
Source raw CSV SHA-256: 761967d2f0d5fc263536df339f16299a4482c4ac748048f0f4538ce3946e8310
Source blocks CSV SHA-256: 79cdf3c6e74ea0c83e4ec0ca24f660e6b10ab2e8906170d94da0a6967dcfd05f
Source summary JSON SHA-256: d3b929a6e9d2abf10c0937951ce555afef16b7651d8bc4e1c414e40360bb4bd6

Production refs main and SOURCE remain untouched at this evidence-recording step. Further optimization experiments must branch from frozen LAB37B and preserve its exact-output contract unless explicitly admitted as a quality-changing experiment.
