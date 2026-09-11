PTAR LAB47A GTX960M HARDWARE RUN2 — REPLICATION CONFIRMED

Source results archive:
- name: results(20260911-120024).zip
- SHA256: b6ea0232381a11cb3e793361b82f4805f8f17ed9991aa19b3880c62039d5d31f
- bytes: 10579
- ZIP CRC: PASS

Protocol:
- LAB47A_GTX960M_SAME_RUN_BALANCED_COMPARE
- NVIDIA GeForce GTX 960M
- 1280x720 -> 1920x1080
- 60 warmup triads
- 600 measured triads
- six rotating permutations, exactly 100 each
- K185 is P-state/drift control only

Frozen shader hashes:
- LAB38A: d60820f62ca69e2cb2ef995c37001f100733e179f0821d7eb83247ad2c0014d3
- LAB47A: 12ab14074c5de7a91b50aa8c114f4f235aea2f6d8903f8d0e6b2813e8907cc72
- K185: 6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16

RUN2 result:
- LAB38A median: 0.704032000 ms
- LAB47A median: 0.683200000 ms
- K185 median: 0.699344000 ms
- paired median speedup: +3.047615363215%
- bootstrap CI95: [+2.974449545340%, +3.102416550565%]
- wins LAB47A/LAB38A/ties: 599/1/0
- sign-test p: 2.8967236778536667e-178

Strict gate:
- overall median > 0: PASS
- overall bootstrap lower > 0: PASS
- majority wins: PASS
- all six permutation medians > 0: PASS
- all six permutation bootstrap lower bounds > 0: PASS
- all stable K185-defined P-state segment medians > 0: PASS
- all stable segment bootstrap lower bounds > 0: PASS

Mixed transition triads excluded only from per-state inference: 157, 398, 557.

Final replication verdict:
PROMOTE_LAB47A_AS_REPLICATED_HARDWARE_SELECTED_BASELINE

RUN1 and RUN2 are independent source archives and both independently satisfy the strict hardware gate. Protected production refs main and SOURCE are not modified by this lab promotion.
