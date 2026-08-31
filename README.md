# PTAR Project Master

This repository snapshot turns PTAR into a durable, reproducible project rather than a one-shot experiment.

## Core rule

No historical result may be silently recreated, replaced or backfilled from a synthetic substitute.

Recovered facts are recorded as recovered facts.
Missing source artifacts are recorded as missing.
Future recovered files may be attached to the historical records, but must not alter the original historical claims.

## Current official architecture

- PTAR MoE v07
- NATURAL v02 — LOCKED
- EDGE v03 — LOCKED BASELINE
- STEP-WENO — EXPERIMENTAL continuation inside EDGE
  - SW1 = p=1, no new gate, no monotone clamp
  - SW2 = p=2 + central monotone clamp
  - SW3 = SW2 modulated by existing EDGE confidence
- RASTER v03 — LOCKED
- NIS is excluded from the PTAR runtime.
- NIS45 exact is only an external benchmark reference.

## Historical benchmark facts currently recovered

- EDGE: 64/64
- horse: 64/64
- naturel: 48/48
- RASTER: 64/64

The original historical corpus image files and the original benchmark harness are currently unavailable.
Those missing artifacts are explicitly tracked in `corpus/historical/LEGACY_CORPUS_MANIFEST.csv` and
`history/MISSING_ARTIFACTS.md`.

## Project policy

1. Every corpus version gets a stable corpus ID.
2. Every source asset is hashed with SHA-256.
3. Raw source assets are immutable.
4. Derived assets are stored separately from raw sources.
5. Benchmark code, configuration and results are versioned together.
6. Each benchmark result must name:
   - PTAR version
   - corpus ID
   - benchmark harness version
   - external reference version
   - date
   - machine/GPU when relevant
7. Historical result rows are append-only.
8. Synthetic tests may validate mathematics and implementation, but may never replace a missing historical corpus.
9. Runtime dependencies and external algorithms must be explicitly declared.
10. Releases must include a complete manifest and integrity hashes.

## Next engineering gate

Build EDGE v04 experimental around the validated STEP-WENO primitive, then validate it on a new permanent PTAR corpus.
If the historical corpus is later recovered, rerun the historical campaign separately and keep both result series.

## v0.2.0 state

EDGE v04 now has a compile-time STEP-WENO adapter without reconstructing missing EDGE v03 internals.
PTAR-CORE-PROBES-V1 is a permanent deterministic regression corpus, explicitly non-equivalent to the historical corpus.
The image benchmark harness is present and self-tested. PERCEPTUAL-V1 assets remain to be populated.

## v0.3.0 state

PTAR-PERCEPTUAL-V1-A is now populated with 42 permanent hashed cases.
The baseline image benchmark pipeline is operational.
The next empirical blocker is no longer the corpus: it is the absence of the preserved full EDGE v03 image renderer/source path needed to produce exact baseline/SW1/SW2/SW3 outputs.

## v0.4.0 state

The PTAR EDGE v04 branch is now integration-ready at the helper/host-contract level.

Added:
- recovered device-C one-pass host contract documentation;
- Shader Model 5.0 compile probe;
- non-destructive Windows FXC build script for 4 compile-time permutations;
- append-only result registration tool;
- explicit variant manifest.

Important: this does not reconstruct the missing historical EDGE v03 renderer.
It prepares a reproducible insertion/test path while preserving that distinction.

## v0.5.0 state

EDGE-NG v01 is a recovery-neutral CPU reference successor. Protocol A sweep is complete; historical EDGE v03 remains untouched.

## v0.6.0 state

Protocol B is now the primary fixed-x1.5 NG geometry corpus. EDGE-NG v01 confirms that the recovered STEP-WENO primitive is not automatically beneficial in the new successor architecture; cubic directional reconstruction is retained as the NG baseline, with cubic4-separable as the quality control for EDGE-NG v02.

## v0.7.0 state

EDGE-NG v02 G5 is the current runtime-oriented EDGE successor. It improves the orientation stage without reintroducing STEP-WENO and has a five-texture-operation source design for SM5. HQ05 remains a non-runtime quality reference.

## v0.9.0 state

A native Windows 8.1 / D3D11 hardware validation kit is now included. It automatically builds K185 with FXC, audits DXBC, selects the NVIDIA adapter, renders the 42 permanent Protocol-B cases, compares them against the float32 semantic references, saves GPU outputs, and measures a 1280x720 -> 1920x1080 pass with 512 non-blocking GPU timestamp samples.

The kit has been statically/regression validated in the current environment, but real FXC/D3D11/GTX execution remains the next hardware gate.

## v0.10.0 — one-click Windows 8.1 validation

The user no longer needs to manually open a Visual Studio Developer Command Prompt.

Extract the project and double-click:

`RUN_PTAR_AUTO.bat`

The launcher detects or installs the Microsoft toolchain, configures MSVC/FXC, builds K185 and launches the complete GTX hardware campaign.

The only operating-system interaction that cannot be bypassed is the normal UAC approval. If Microsoft requires a reboot, PTAR reports it but never restarts the machine automatically.

## v0.10.1

Use v0.10.1 instead of v0.10.0 on Windows 8.1.

The first real v0.10.0 run exposed a PowerShell 4/UAC quoting issue before
toolchain setup. The launcher no longer passes the project path across the UAC
boundary; the script resolves its own directory instead.

## v0.10.2

The real Windows 8.1 v0.10.1 run exposed a TLS/.NET download failure after a
successful machine/GPU preflight. v0.10.2 now prefers BITS and includes several
fallback download paths before any TLS compatibility change is considered.

Use `RUN_PTAR_AUTO.bat` exactly as before.

## v0.10.3

The real v0.10.2 run installed the C++ toolchain successfully but showed that
FXC is not reliably exposed by the legacy SDK setup. v0.10.3 removes FXC as a
hard dependency: the native validator compiles and disassembles HLSL through
the D3DCompiler API.

Use `RUN_PTAR_AUTO.bat` as before.

## v0.10.4

The real v0.10.3 Windows 8.1 run reached MSVC compilation and exposed a Win32
header macro collision at `std::max`. v0.10.4 fixes that exact native build
error with `NOMINMAX` plus a macro-safe call form.

Run `RUN_PTAR_AUTO.bat` as before. The already-installed VS2019 Build Tools
should be reused.

## v0.10.5

The real v0.10.4 machine test passed the previous Win32 `max` macro failure and
exposed two invalid wide-to-narrow string conversions later in the native
validator. v0.10.5 replaces them with explicit UTF-8 conversion.

Run `RUN_PTAR_AUTO.bat` as before; the installed Microsoft toolchain is reused.

## v0.10.6

v0.10.5 reached the first complete GTX 960M shader validation: HLSL/DXBC passed
and all 42 GPU parity images passed. The display-driver reset happened only in
the old timing harness.

v0.10.6 replaces the unsafe queued timing loop with one timed draw in flight at
a time. After the v0.10.5 TDR, restart Windows once before running v0.10.6.

## v0.10.7

v0.10.6 completed both parity and timing successfully on the real GTX 960M.
The only remaining failure was a process crash after the final timing lines.

v0.10.7 fixes COM/D3D cleanup ordering. Run `RUN_PTAR_AUTO.bat` as before.
The expected new evidence is the `[CLEANUP]` line followed by a normal exit
without a Windows crash dialog.

## v0.10.8

The real GTX 960M evidence is now archived inside the project.

v0.10.8 fixes the only invalid artifact from the supplied result set: the 42
zero-byte GPU PNG files. PNG persistence is now a mandatory hardware gate.

The timing comparison is also upgraded from sequential K185-then-bilinear runs
to 512 alternating A/B pairs to reduce order/clock bias.

## v0.11.0 — PTAR-NG MoE

K185 is hardware validated. v0.11.0 introduces the first complete new NG mixture-of-experts candidate.

NATURAL-NG v01 and RASTER-NG v01 are new source code, not recreations of the missing historical NATURAL v02 / RASTER v03.

Run `RUN_PTAR_AUTO.bat` to validate MoE GPU parity and benchmark it directly against K185.

## v0.12.0 — autonomous runtime freeze

The complete MoE is now hardware validated. v0.12.0 freezes the exact validated
DXBC and prepares a runtime-validation host that does not compile HLSL.

Run `RUN_PTAR_AUTO.bat` once while MSVC/SDK are still installed. If that
autonomous test passes, the next release can safely focus on removing the
development toolchain and verifying the same binary after removal.

## v0.12.1

v0.12.0 exposed one compile-time migration error on the real Windows 8.1 host:
two corpus paths still used the old `root` identifier.

v0.12.1 fixes those paths to use the autonomous bundle root. Run
`RUN_PTAR_AUTO.bat` again from a newly extracted v0.12.1 folder.

## v0.12.2

v0.12.1 successfully built the autonomous host and passed the import audit,
but the host returned Usage/code 2 because `%~dp0` supplied a trailing
backslash inside the quoted `--bundle-root` argument.

v0.12.2 canonicalizes the bundle directory before launching the EXE.
Run `RUN_PTAR_AUTO.bat` again from a newly extracted folder.

## v0.12.3

This release performs an exhaustive Windows 8.1 autonomous-launch audit.

The native source that already compiled successfully on the real Windows 8.1
machine is unchanged. The launcher/build layer now uses one path convention,
full asset/result gates and stronger import checks.

Extract to a new folder and run `RUN_PTAR_AUTO.bat`.

## v0.13.0 — production integration core

The validated PTAR MoE is now packaged as an embedded-DXBC D3D11 production
module. It is designed to replace the spatial NIS stage in the current
P1FG7N/VBLANK2 integrated presenter without changing FG or recorder logic.

The next patch must be made against the exact B18K18 package identity recorded
in `runtime_production/ptar_moe_v01_core/EXPECTED_INTEGRATION_BASE.json`.
