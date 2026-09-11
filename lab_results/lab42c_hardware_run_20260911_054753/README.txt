PTAR LAB42C GTX960M HARDWARE RUN — 2026-09-11 05:47:53

Decision: REJECT_KEEP_REFERENCE
Reference kept: LAB38A
Candidate rejected: LAB42C

Protocol integrity:
- ZIP CRC: PASS
- 600 contiguous measured triads: PASS
- six permutations x 100: PASS
- positive finite timings: PASS
- paired delta/winner columns: recomputed exact
- BLOCKS_60.csv: recomputed exact
- summary JSON: recomputed exact
- analyzer self-tests: 4/4 PASS

Overall:
- LAB38A median: 0.706016000 ms
- LAB42C median: 0.713008000 ms
- paired median speedup: -1.287523154 %
- bootstrap CI95: [-1.360072186 %, -1.216990338 %]
- candidate/reference/ties: 40/559/1
- two-sided sign-test p: 4.199752847572645773e-118

All six permutation medians are negative.
Both stable K185-defined P-state segment medians are negative.
Mixed transition triad: 406, excluded only from stable-segment inference.

Source results ZIP SHA256:
0113c96756ccacb32ba5272b2ab2956374fe5d61cfba509cc11cd55529817343

No second LAB42C hardware run is warranted. LAB38A remains selected.
