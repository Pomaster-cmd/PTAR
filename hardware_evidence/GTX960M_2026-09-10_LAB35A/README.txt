LAB35A GTX960M hardware candidate — PHYSICAL VERDICT: REJECT, KEEP LAB31G.

Current hardware-selected baseline remains LAB31G.

Protocol: LAB35A_GTX960M_SAME_RUN_BALANCED_COMPARE
Adapter: NVIDIA GeForce GTX 960M
Resolution: 1280x720 -> 1920x1080
Warmup: 60 triads
Measured: 600 triads
Order protocol: six rotating permutations, exactly 100 each
Software correctness: PASS

Frozen shaders:
LAB31G SHA-256 = ead1e18944b392ef23562d9a45a9429d5bdee77ae914e313b415c3143c39aed1
LAB35A SHA-256 = 4789a5a345770af8c2881293a4e6c5501043b28d0c4d8aed8f7ab33ed218ae2c
K185 SHA-256 = 6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16

LAB35A kept LAB31G's 16-byte inverse+half-inverse ABI and the same 53 instructions / 2568-byte static DXBC size. Its only intended optimization was dynamic: phase 0 executes GatherGreen + one SampleLevel before returning instead of LAB31G's two SampleLevel operations before the return.

Physical paired result:
- overall LAB31G-LAB35A median delta = +0.000064 ms
- nominal LAB35A paired median speedup = +0.009583335%
- LAB35A wins / LAB31G wins / ties = 303 / 296 / 1
- bootstrap 95% CI = [-0.070972321%, +0.087560060%]
- two-sided sign-test p = 0.806362576
- all six permutation medians are not positive: two are negative

K185 detects a strong P-state transition. The robust split is around triad 151; triad 151 is mixed and excluded from stable-state interpretation.
Stable state 0..150: K185 median 0.497824 ms; LAB35A paired median speedup -0.070972321%; bootstrap CI [-0.173789907%, +0.090556274%]; 71 candidate wins / 79 reference wins / 1 tie.
Stable state 152..599: K185 median 0.697744 ms; LAB35A paired median speedup +0.029609653%; bootstrap CI [-0.059499389%, +0.124829114%]; 232 / 216 / 0.

The preregistered promotion rule requires a reproducible positive paired gain across K185-defined P-state regimes and balanced permutations. LAB35A is statistically neutral and not state-robust, so it does not replace LAB31G.

Source results ZIP SHA-256: 048bee225b5a63968f5cb659db0b5da8bcdb3955b365663047578aa63e384ad5
Source raw CSV SHA-256: e127109a75655fa34b91e5d302303dcf529b28937f3d53a255a15b68cbd1fdb2

Production refs main and SOURCE were not modified.