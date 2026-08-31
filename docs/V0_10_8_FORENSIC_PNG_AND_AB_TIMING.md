# PTAR v0.10.8 — forensic PNG + paired A/B timing

## Real evidence incorporated

The user-supplied `results.zip` from the real Windows 8.1 / GTX 960M campaign
is now preserved under:

`hardware_evidence/GTX960M_2026-08-17_v0_10_6/`

Recorded facts:

- GPU parity cases: 42
- failed parity cases: 0
- maximum error: 1 RGB8 LSB
- K185 median: 0.497728000 ms
- bilinear median: 0.502352000 ms
- 42 requested GPU PNG outputs existed as zero-byte files

The zero-byte files are explicitly marked invalid as forensic image outputs.
They do not invalidate parity because comparison occurred against GPU readback
in memory before PNG persistence.

## PNG correction

The Windows WIC PNG path now:

1. receives PTAR's RGBA buffer;
2. explicitly converts it to BGRA;
3. requests `GUID_WICPixelFormat32bppBGRA`;
4. checks every WIC HRESULT;
5. commits and releases the encoder/stream;
6. verifies the resulting file size;
7. fails the hardware gate if the file is zero bytes or cannot be persisted.

`parity.csv` now includes `gpu_png_bytes`.

## Timing correction

The old v0.10.6 timing result is valid but measured all K185 samples before all
bilinear samples.

v0.10.8 changes the benchmark to paired A/B measurements:

- 32 paired warm-up iterations;
- 512 measured pairs;
- even pairs: K185 first, then bilinear;
- odd pairs: bilinear first, then K185;
- one timed draw remains in flight at a time;
- no explicit D3D11 `Flush()`.

New file:

`timing_pairs.csv`

contains every pair and the signed delta:

`K185_ms - bilinear_ms`.

The hardware summary also reports paired mean/median delta and win counts.

## Promotion gate

A v0.10.8 real-machine run is complete only if:

- 42/42 parity PASS;
- 42/42 PNG files are non-zero;
- clean process exit;
- paired timing completes without TDR.
