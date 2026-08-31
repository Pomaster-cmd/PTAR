# Decision Log

## D-001 — PTAR runtime independence
Status: LOCKED
Decision: NIS is excluded from PTAR runtime. NIS45 may only be used as an external benchmark.

## D-002 — PTAR MoE baseline
Status: LOCKED
Decision: MoE v07 with NATURAL v02, EDGE v03 and RASTER v03.

## D-003 — STEP-WENO scope
Status: ACTIVE EXPERIMENT
Decision: STEP-WENO is evaluated inside EDGE, not as a fourth expert.

## D-004 — Historical corpus integrity
Status: LOCKED
Decision: Missing historical assets may not be reconstructed and silently presented as originals.

## D-005 — Persistent project structure
Status: LOCKED
Decision: all future corpus manifests, benchmark protocols, results, code and hashes are stored together in versioned releases.

## D-006 — Synthetic validation boundary
Status: LOCKED
Decision: synthetic tests are valid for formal/implementation validation only and cannot replace historical empirical validation.

## D-007 — EDGE v04 experimental insertion
Status: ACTIVE EXPERIMENT
Decision: compile-time baseline/SW1/SW2/SW3 adapter; EDGE v03 remains locked.

## D-008 — Three corpus classes
Status: LOCKED
Decision: historical, core-probe and perceptual corpora are distinct.

## D-009 — Permanent image benchmark harness
Status: LOCKED
Decision: the benchmark harness compares renderer outputs and does not implement PTAR.

## D-010 — PERCEPTUAL-V1-A initial permanent set
Status: LOCKED
Decision: 14 source snapshots × 3 deterministic crops = 42 cases, HR 288×288 / LR 192×192, exact 1.5×.

## D-011 — Input derivation Protocol A
Status: LOCKED FOR CORPUS ID
Decision: scikit-image resize order=3, reflect, anti_aliasing=True in sRGB/code-value domain. Any change requires a new corpus ID.

## D-012 — B18D is an integration host reference only
Status: LOCKED
Decision: recovered B18D runtime evidence may define device-C integration constraints, but B18D must never be relabeled as EDGE v03.

## D-013 — Compile-time candidate permutations
Status: LOCKED
Decision: EDGE baseline/SW1/SW2/SW3 are separate SM5.0 permutations.

## D-014 — Append-only result registration
Status: LOCKED
Decision: benchmark execution IDs and manifests may not be silently overwritten.

## D-015 — Recovery-neutral successor branch
Status: LOCKED
Decision: new EDGE development proceeds as EDGE-NG until direct equivalence to historical EDGE v03 can be proven.

## D-016 — EDGE-NG v01 mapping
Status: LOCKED FOR v01
Decision: fixed 1.5x grid-aligned source coordinate = output_index / 1.5, yielding exact 1/3 and 2/3 phases.

## D-017 — EDGE-NG v01 confidence
Status: EXPERIMENTAL
Decision: adaptive gradient/local-range confidence is new code and must not be attributed to EDGE v03.

## D-018 — Grid-aligned Protocol B
Status: LOCKED
Decision: PTAR-PERCEPTUAL-V1-B-GRID is the primary NG algorithm-geometry corpus for fixed x1.5 development. Protocol A remains a generic cross-check.

## D-019 — STEP-WENO NG v01 promotion decision
Status: NOT PROMOTED
Decision: on grid-aligned Protocol B, directional cubic baseline remains ahead of SW1/SW2/SW3. Preserve STEP-WENO research; do not carry it into EDGE-NG v02 by default.

## D-020 — EDGE-NG v02 G5
Status: ACTIVE RUNTIME CANDIDATE
Decision: use one green-channel 2x2 gather for orientation and four selected-axis color samples for cubic reconstruction. STEP-WENO is excluded from the v02 runtime path.

## D-021 — HQ05
Status: DIAGNOSTIC ONLY
Decision: four +/-0.5 bilinear direction probes define a quality reference, not a performance candidate.

## D-022 — EDGE mask benchmark
Status: LOCKED PROTOCOL v1
Decision: top 20% HR reference luminance gradient per case, dilated one pixel, is an evaluation-only EDGE metric alongside full-image metrics.

## D-019 — Hardware validation is automated and reproducible
Status: LOCKED
Decision: K185 promotion requires the native 42-case GPU parity campaign plus non-blocking 720p->1080p D3D11 timing; console impressions alone are insufficient.

## D-020 — NVIDIA adapter is selected explicitly
Status: LOCKED FOR REFERENCE HARDWARE
Decision: the reference hardware validator prefers/requires VendorId 0x10DE and feature level 11.0 so the Intel iGPU cannot silently invalidate GTX 960M measurements.

## D-019 — One-click Windows 8.1 hardware automation
Status: LOCKED
Decision: PTAR may install missing Microsoft build prerequisites automatically, but only from documented official Microsoft endpoints and only after valid Microsoft Authenticode verification.

## D-020 — No unattended reboot
Status: LOCKED
Decision: even if Microsoft setup returns reboot-required codes, PTAR must not restart Windows automatically because unsaved user work may exist.

## D-021 — Automation is append-only
Status: LOCKED
Decision: every setup/test attempt creates a unique run directory; prior runs and downloaded installers are not deleted.

## D-022 — Multi-engine prerequisite download on Windows 8.1
Status: LOCKED
Decision: do not rely solely on .NET WebClient; try BITS, bitsadmin, WebClient TLS1.2 and certutil.

## D-023 — TLS repair is last-resort and audited
Status: LOCKED
Decision: record prior .NET/SChannel state before writing strong-crypto/TLS1.2 values; never reboot automatically.

## D-024 — FXC is optional for the hardware gate
Status: LOCKED
Decision: use the official D3DCompiler API for SM5 compile/disassembly when FXC is absent; preserve FXC only as an optional diagnostic/tooling path.

## D-025 — Win32 min/max macros are disabled in native PTAR tooling
Status: LOCKED
Decision: native Windows validation code defines NOMINMAX before windows.h and uses macro-safe std::min/std::max call forms when needed.

## D-026 — Native PTAR logs use explicit UTF-8 conversion
Status: LOCKED
Decision: never construct `std::string` directly from `std::wstring` iterators. Windows UTF-16 text crossing into byte logs/CSV must use explicit UTF-8 conversion.

## D-027 — Hardware timing must never accumulate an unbounded GPU queue
Status: LOCKED
Decision: submit no new timed fullscreen draw until the previous query completes.

## D-028 — Do not weaken TDR to make PTAR benchmarks pass
Status: LOCKED
Decision: do not increase TdrDelay or disable GPU timeout protection. Correct the benchmark workload instead.

## D-029 — COM apartment lifetime is RAII-managed
Status: LOCKED
Decision: the COM apartment guard must outlive every WIC/D3D COM-owning automatic object. Manual CoUninitialize calls in validator error/success paths are forbidden.

## D-030 — GPU forensic images are a hard validation artifact
Status: LOCKED
Decision: a parity run is not artifact-complete unless every requested GPU PNG is successfully encoded and non-zero.

## D-031 — K185/bilinear timing uses paired alternating order
Status: LOCKED
Decision: timing comparisons alternate which shader runs first and preserve all 512 per-pair deltas to reduce order/clock bias.

## D-032 — Real hardware evidence is preserved in-project
Status: LOCKED
Decision: user-supplied real-machine result packages used for promotion decisions are archived with metadata and hashes inside the project master.

## D-033 — K185 hardware validation closed
Status: LOCKED
Decision: v0.10.8 GTX 960M campaign accepted: 42/42 parity, <=1 LSB, 42 non-zero PNGs, 512 paired samples, no TDR/crash, clean exit confirmed.

## D-034 — PTAR-NG MoE v01 uses shared SF5 fetches
Status: EXPERIMENTAL LOCK
Decision: NATURAL-NG, EDGE-NG and RASTER-NG reuse one 1 GatherGreen + 4 SampleLevel footprint.

## D-035 — MoE routing is soft
Status: EXPERIMENTAL LOCK
Decision: ROUTER-NG v01 uses continuous convex weights. The discarded hard router had unacceptable float precision sensitivity.

## D-036 — PTAR-NG MoE v01 SF5 hardware validation closed
Status: LOCKED
Decision: accept the v0.11.0 GTX 960M result: 42/42 <=1 LSB, 42 non-zero PNGs,
512 paired timings, 0.655488 ms median, clean exit confirmed, no TDR/error.

## D-037 — runtime shaders are frozen from validated DXBC
Status: LOCKED
Decision: autonomous runtime validation loads the exact CSO bytecode produced
and audited by the accepted v0.11.0 hardware run. It does not compile HLSL.

## D-038 — toolchain removal is gated by autonomous proof
Status: LOCKED
Decision: no automated uninstall/deletion before the precompiled-CSO host
passes on the real machine with compiler/FXC hidden from PATH.

## D-039 — Autonomous validator must be bundle-relative
Status: LOCKED
Decision: every runtime asset path used by the autonomous validator is resolved
from `bundleRoot`; project-master corpus/benchmark paths are forbidden in the
autonomous source.

## D-040 — Quoted Windows directory CLI arguments must not end in a raw backslash
Status: LOCKED
Decision: batch launchers canonicalize `%~dp0.` to the directory path before
passing it as a quoted argument to native PTAR executables.

## D-041 — Canonical runtime paths use explicit child separators
Status: LOCKED
Decision: ROOT/BUNDLE carry no implicit trailing separator; every child path
must add its own backslash.

## D-042 — Freeze already-compiling Win8.1 native source during launcher repair
Status: LOCKED
Decision: v0.12.3 does not change autonomous C++ source or validated CSOs.

## D-043 — Autonomous acceptance requires complete preflight/postflight
Status: LOCKED
Decision: exit code alone is insufficient; require 42+42 assets, 42 parity
cases, 42 non-zero outputs, 512 timing pairs and dependency markers.

## D-044 — Production PTAR v01 is exact-x1.5 only
Status: LOCKED
Decision: PTAR-NG MoE v01 is activated only when both output axes are exactly
3/2 of the source. Other ratios fail open to direct/POINT/BILINEAR.

## D-045 — Production integration preserves temporal and recorder modules
Status: LOCKED
Decision: the first proxy integration changes only the spatial reconstruction
selection/path. P1FG7N temporal modules and B18K18 recorder remain byte-identical.

## D-046 — Production PTAR has no NIS fallback
Status: LOCKED
Decision: NIS remains external/reference-only for PTAR. Runtime PTAR failure or
unsupported scale uses direct/POINT/BILINEAR, never NIS.
