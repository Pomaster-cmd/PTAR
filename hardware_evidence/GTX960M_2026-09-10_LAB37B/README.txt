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

Physical paired result — run 1:
- LAB36B median = 0.737024 ms
- LAB37B median = 0.718400 ms
- overall paired LAB36B-LAB37B median delta = +0.015088 ms
- paired median speedup = +2.159627182%
- bootstrap 95% CI = [2.041531094146%, 2.256160073020%]
- LAB37B wins / LAB36B wins / ties = 599 / 1 / 0
- two-sided sign-test p = 2.896723677853667e-178
- all six permutation medians are positive; all six permutation bootstrap 95% lower bounds are positive

K185 detects one abrupt P-state transition at triad 177 in run 1.
Stable state 0..176: K185 median 0.494720 ms; LAB37B paired median speedup +1.575781352%; bootstrap CI [1.543026706%, 1.625909657%]; 177 candidate wins / 0 reference wins / 0 ties.
Stable state 177..599: K185 median 0.698240 ms; LAB37B paired median speedup +2.574829639%; bootstrap CI [2.473314241%, 2.686579975%]; 422 / 1 / 0.

Independent physical replication — run 2:
- source ZIP SHA-256 = 8fbf66e496658ce2cc0626f9c9e8120e97d52fa3176fc8992302306b0367b632
- LAB36B median = 0.740336 ms
- LAB37B median = 0.721312 ms
- paired median delta = +0.015808 ms
- paired median speedup = +2.213099877%
- bootstrap 95% CI = [2.132355979%, 2.348839804%]
- LAB37B wins / LAB36B wins / ties = 598 / 2 / 0
- two-sided sign-test p = 8.690219231958302e-176
- all six permutation medians are positive and every permutation bootstrap 95% lower bound remains > 0

Run 2 contains one mixed P-state transition triad at index 159. Its order is LAB37B>K185>LAB36B: LAB37B is still in the fast state while K185 and LAB36B are already in the slow state. Triad 159 is therefore excluded from stable-state interpretation.
Stable state 0..158: K185 median 0.497888 ms; LAB37B paired median speedup +1.603403141%; bootstrap CI [1.541691268%, 1.687708510%]; 159 / 0 / 0.
Stable state 160..599: K185 median 0.700640 ms; LAB37B paired median speedup +2.597064589%; bootstrap CI [2.499784872%, 2.676518366%]; 438 / 2 / 0.

Two-run replication verdict:
- 1200 measured triads total
- LAB37B wins 1197 / 1200; LAB36B wins 3 / 1200; no ties
- overall median speedup range = +2.159627% to +2.213100%
- conservative minimum overall bootstrap 95% lower bound across the two runs = +2.041531%
- fast-state median speedup range = +1.575781% to +1.603403%
- slow-state median speedup range = +2.574830% to +2.597065%

The preregistered promotion rule is satisfied independently twice. LAB37B is therefore not merely promoted from a single run; its hardware-selected baseline status is replicated and confirmed on the GTX 960M.

Run 1 source results ZIP SHA-256: fe9b904bc1c1376fe7ab1f4b6f5ad42b79a3f3feac335abac51ce43849f5cde4
Run 1 source raw CSV SHA-256: 761967d2f0d5fc263536df339f16299a4482c4ac748048f0f4538ce3946e8310
Run 1 source blocks CSV SHA-256: 79cdf3c6e74ea0c83e4ec0ca24f660e6b10ab2e8906170d94da0a6967dcfd05f
Run 1 source summary JSON SHA-256: d3b929a6e9d2abf10c0937951ce555afef16b7651d8bc4e1c414e40360bb4bd6
Run 2 source results ZIP SHA-256: 8fbf66e496658ce2cc0626f9c9e8120e97d52fa3176fc8992302306b0367b632
Run 2 source raw CSV SHA-256: bf2ef4703b48493ea12be40b78981ee5d486ef49ce01a20fc6e6e9e6fdad0489
Run 2 source blocks CSV SHA-256: dd7a8e36ea4ea483f0c5948298fd4a5a4479343aa0985e11686c69b2399c4ed0
Run 2 source summary JSON SHA-256: b1d961d7cbdac40bae87a4e7b74f82aeadf6dbfead448eed14b6e4ba7d510482

Production refs main and SOURCE remain untouched at this evidence-recording step. Further optimization experiments must branch from frozen LAB37B and preserve its exact-output contract unless explicitly admitted as a quality-changing experiment.
