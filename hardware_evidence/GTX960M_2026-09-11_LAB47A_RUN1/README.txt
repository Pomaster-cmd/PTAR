PTAR LAB47A GTX960M HARDWARE RUN1

Source results ZIP:
  results(20260911-115131).zip
  SHA256 db7328385888831c5f06e5ca8d467cd74f0c9c197a79c1eb918b5bcbd3c4ff6b
  CRC PASS

Protocol:
  LAB47A_GTX960M_SAME_RUN_BALANCED_COMPARE
  NVIDIA GeForce GTX 960M
  1280x720 -> 1920x1080
  60 warmup triads
  600 measured triads
  six rotating permutations, 100 triads each

Frozen shader hashes:
  LAB38A d60820f62ca69e2cb2ef995c37001f100733e179f0821d7eb83247ad2c0014d3
  LAB47A 12ab14074c5de7a91b50aa8c114f4f235aea2f6d8903f8d0e6b2813e8907cc72
  K185   6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16

Recomputed result:
  LAB38A median 0.703984 ms
  LAB47A median 0.683472 ms
  paired delta median +0.022080 ms
  paired speedup median +3.011740374781304%
  bootstrap CI95 +2.944850954531538% .. +3.0781240843030577%
  wins LAB47A/LAB38A/ties = 599/1/0
  two-sided sign-test p = 2.8967236778536667e-178

All six permutation medians and bootstrap lower bounds are positive.
K185 jumps >20% occur at triads 156, 398 and 556.
Triads 156 and 555 are genuine mixed transition triads and are excluded only from stable-P-state interpretation.
All four stable segment medians and bootstrap lower bounds are positive.

STRICT HARDWARE GATE: PASS
VERDICT: RUN1_PASS_REPLICATION_PENDING

LAB38A remains the canonical hardware-selected baseline until LAB47A reproduces a second independent strict PASS. main and SOURCE are not modified by this evidence archival.
