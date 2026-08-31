# Benchmark Reproducibility Protocol

A PTAR benchmark is valid only if the result bundle contains:

1. exact PTAR build/version;
2. exact corpus ID and corpus manifest hash;
3. exact benchmark harness version and hash;
4. test configuration;
5. external reference implementation/version, if any;
6. raw per-case results;
7. aggregate results;
8. machine/GPU/driver/runtime metadata when GPU timing is involved;
9. UTC/local execution timestamp;
10. manifest SHA-256.

## Quality measurements

The project may use multiple metrics, but the metric definitions and implementation versions must be fixed per campaign.
No metric may be changed mid-series without creating a new benchmark protocol version.

## Synthetic/formal validation

Synthetic or generated patterns are valid for:
- algebraic correctness;
- phase symmetry;
- monotonicity;
- ringing/overshoot stress;
- implementation invariants;
- shader regression tests.

They are not valid substitutes for missing historical photographic/raster corpus assets.

## Historical benchmark registry

Recovered historical aggregate results are preserved even when the underlying files are missing, but their provenance status must
remain `RECOVERED_AGGREGATE_ONLY` until the original inputs and harness are recovered.
