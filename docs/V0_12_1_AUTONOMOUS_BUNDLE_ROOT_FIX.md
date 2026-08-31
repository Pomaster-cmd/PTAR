# PTAR v0.12.1 — autonomous bundle-root correction

## Real Windows 8.1 failure in v0.12.0

MSVC stopped with C2065 because two corpus path lines in the autonomous host
still referenced the old `root` identifier after the CLI had been migrated to
`bundleRoot`.

Faulty paths:
- project-level Protocol-B input path;
- project-level MoE semantic-output path.

## Correction

The autonomous host now reads only from its self-contained bundle:

- `bundleRoot\corpus\input_lr`
- `bundleRoot\corpus\expected_moe_float32`

No shader, algorithm, CSO, hash, timing protocol, parity tolerance or runtime
dependency rule is changed.

The fix also adds a regression gate forbidding any remaining standalone C++
identifier named `root` in the autonomous source.
