# PTAR GPU Validation Protocol v1.1

This supersedes only the timing section of GPU Validation Protocol v1.

## Real GTX 960M milestone from v0.10.5

On Windows 8.1 / NVIDIA GeForce GTX 960M:

- native validator compilation: PASS
- VS `vs_5_0`: PASS
- K185 PS `ps_5_0`: PASS
- bilinear control PS `ps_5_0`: PASS
- DXBC audit: `1 gather4 + 4 sample_l + 0 UAV`: PASS
- permanent corpus GPU parity: 42/42 PASS
- maximum GPU/reference error: 1 RGB8 LSB

Windows then reset the NVIDIA display driver while the old timing harness was
running.

## Defect in the old timing harness

The old harness could enqueue a large amount of fullscreen work:
- 300 consecutive 1920x1080 warm-up draws without waiting for completion;
- an 8-slot timestamp ring;
- DONOTFLUSH-only query probing;
- when the ring was full, more unmeasured draws were still submitted;
- up to 200000 loop iterations.

That design is removed.

## v1.1 timing

- 32 valid warm-up draws;
- one draw in flight at a time;
- every warm-up draw is resolved before the next one;
- 512 valid measured draws;
- every measured draw is resolved before the next one;
- query polling uses `GetData(..., flags=0)` to allow runtime progress;
- PTAR still never calls `ID3D11DeviceContext::Flush()` explicitly;
- polling sleeps 1 ms;
- one query has a 1000 ms bounded wait;
- device removal/reset reason is checked while waiting.

The timestamp interval still surrounds exactly one fullscreen draw.

## TDR policy

PTAR does not change `TdrDelay`, disable TDR, or request a disabled GPU timeout.
The benchmark must adapt to the machine rather than weakening Windows recovery.
