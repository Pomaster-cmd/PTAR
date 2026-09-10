PTAR-NG MoE v02 — LAB38A GTX 960M hardware evidence — RUN 2 / REPLICATION
Date: 2026-09-10
Target: Windows 8.1 x64 / NVIDIA GeForce GTX 960M
Comparison: LAB37B replicated hardware-selected reference vs LAB38A candidate, K185 control
Resolution: 1280x720 -> 1920x1080
Protocol: 60 warmup triads + 600 measured triads, all six order permutations balanced (100 each)

VERDICT: RUN 2 STRICT PROMOTION GATE PASS — REPLICATION CONFIRMED.
LAB38A is promoted as the new replicated hardware-selected PTAR-NG MoE v02 baseline for the exact x1.5 path.

Run 2 measured paired result:
- LAB37B median: 0.727248 ms
- LAB38A median: 0.709568 ms
- paired median speedup: 1.750572 %
- bootstrap 95% CI: [1.633964, 1.854273] %
- LAB38A wins: 596/600
- sign-test p: 2.594e-171

All six permutation median speedups are positive and all six bootstrap 95% lower bounds are positive.
K185 detects transitions at triads 152, 394 and 552. Triad 552 is a mixed transition triad (LAB37B>LAB38A>K185) and is excluded only from stable-state interpretation. Every stable state keeps a positive paired median speedup and a positive bootstrap 95% lower bound.

Replication across two independent physical runs:
- 1200 measured triads
- LAB38A wins: 1192/1200
- LAB37B wins: 8/1200
- overall paired median speedup range: 1.750572% to 1.877043%
- conservative minimum overall bootstrap 95% lower bound: +1.633964%

Software/codegen admission remains unchanged:
- bit-exact against LAB37B across the admitted software corpus
- texture contract: 1 Gather + 4 SampleLevel + 0 UAV
- 0 DIV/UDIV
- LAB37B: 52 instructions / 2532 bytes
- LAB38A: 49 instructions / 2528 bytes
- LAB38A DXBC SHA-256: d60820f62ca69e2cb2ef995c37001f100733e179f0821d7eb83247ad2c0014d3

Run 2 source results ZIP SHA-256:
b48a09c8178e6cde0256898c6cb745f8b926be2a15690f155101e9c02d0dc1fa

Production refs main and SOURCE remain untouched. Further exact-output optimization experiments should branch from frozen LAB38A.
