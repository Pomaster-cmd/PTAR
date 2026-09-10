PTAR-NG MoE v02 — LAB38A GTX 960M hardware evidence — RUN 1
Date: 2026-09-10
Target: Windows 8.1 x64 / NVIDIA GeForce GTX 960M
Comparison: LAB37B replicated hardware-selected reference vs LAB38A candidate, K185 control
Resolution: 1280x720 -> 1920x1080
Protocol: 60 warmup triads + 600 measured triads, all six order permutations balanced (100 each)

VERDICT: RUN 1 STRICT PROMOTION GATE PASS — INDEPENDENT REPLICATION PENDING.
LAB37B remains the hardware-selected reference until LAB38A reproduces the result in a second independent physical run.

Measured paired result:
- LAB37B median: 0.721744 ms
- LAB38A median: 0.703744 ms
- paired median speedup: 1.877043 %
- bootstrap 95% CI: [1.721342, 1.993282] %
- LAB38A wins: 596/600
- sign-test p: 2.594e-171

All six permutation median speedups are positive and all six bootstrap 95% lower bounds are positive.
A strict K185 multi-state detector found transitions at triads 162, 404 and 562; every resulting stable segment keeps a positive median speedup and a positive bootstrap 95% lower bound.
One isolated LAB38A latency spike occurs at triad 403 (1.065248 ms), but it does not change any robust admission criterion.

Software/codegen admission:
- bit-exact against LAB37B on canonical float, multi-resolution float, 42-case UNORM8 corpus and synthetic probes
- 1 Gather + 4 SampleLevel, 0 UAV, 0 DIV/UDIV
- LAB37B: 52 instructions / 2532 bytes
- LAB38A: 49 instructions / 2528 bytes
- LAB38A DXBC SHA-256: d60820f62ca69e2cb2ef995c37001f100733e179f0821d7eb83247ad2c0014d3

Source results ZIP SHA-256:
70ebab6131fc7691c1abf8a460ba6b92c8ec4637c953cb0972aebe5fd89061bc

The single-run gate result is based on PAIRED_ANALYSIS.json.
The untouched returned ZIP is identified by hash in RESULT_INTEGRITY.json; it is not required to be stored in the repository.
