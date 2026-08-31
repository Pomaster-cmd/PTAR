# Changelog

## 0.2.0 — 2026-08-17

Added:
- EDGE v04 compile-time STEP-WENO adapter;
- Python reference adapter;
- PTAR-CORE-PROBES-V1;
- STEP-WENO epsilon/core sweep protocol;
- permanent image benchmark harness v1;
- tested dependency versions;
- adapter/harness self-tests;
- corpus separation and EDGE v04 acceptance documentation.

Locked unchanged:
- MoE v07;
- NATURAL v02;
- EDGE v03 baseline;
- RASTER v03;
- NIS excluded from PTAR runtime;
- historical aggregate records.

## 0.3.0 — 2026-08-17

Added:
- PTAR-PERCEPTUAL-V1-A permanent acceptance corpus;
- 8 CC0/public-domain scikit-image source snapshots;
- 6 deterministic PTAR-generated raster/UI stress sources;
- 42 immutable HR/LR cases at exact 1.5×;
- per-source/per-case SHA-256 provenance;
- fixed Protocol A downsample definition;
- nearest/bilinear/bicubic pipeline sanity outputs and benchmark results;
- corpus integrity test.

## 0.4.0 — 2026-08-17

Added:
- documented P1FG7N/B18D-derived device-C integration target;
- SM5.0 HLSL compile probe for EDGE v04;
- non-destructive FXC build matrix for modes 0/1/2/3;
- EDGE v04 variant manifest;
- append-only output registration tooling;
- v0.4.0 structural validation.

No historical EDGE v03 source was reconstructed or replaced.

## 0.5.0 — 2026-08-17

Added:
- EDGE-NG v01 recovery-neutral reference renderer;
- real PTAR-PERCEPTUAL-V1-A epsilon/mode sweep;
- permanent EDGE_CORE tuning subset;
- saved outputs for the selected epsilon of BASELINE/SW1/SW2/SW3;
- Clang HLSL diagnostic compute-codegen path for four modes;
- explicit historical-vs-NG branch policy.

Historical EDGE v03 remains unchanged and is not reconstructed.

## 0.6.0 — 2026-08-17

Added PTAR-PERCEPTUAL-V1-B-GRID with exact grid-aligned x1.5 geometry, scaled Lanczos-3 LR derivation, explicit nearest/bilinear/cubic4 controls, full EDGE-NG v01 rerun and promotion decision. STEP-WENO is preserved but not promoted in the NG branch.

## 0.7.0 — 2026-08-17

Added EDGE-NG v02 G5 (1 GatherGreen + 4 SampleLevel source path), HQ05 diagnostic orientation reference, full Protocol B rendering, dedicated edge-mask benchmark and SM5 FXC build script. STEP-WENO remains preserved but is not in the v02 runtime path.

## 0.9.0 — 2026-08-17

Added:
- native Win32/D3D11 K185 hardware validator source;
- strict NVIDIA adapter selection and FL11.0 gate;
- WIC PNG loader/writer with no third-party runtime dependency;
- 42-case GPU parity campaign against K185 float32 semantic references;
- 1280x720 -> 1920x1080 standalone scaler timing campaign;
- 300-frame warm-up + 512 valid timestamp samples;
- bilinear control pass timing;
- one-command FXC + DXBC audit + MSVC build + run batch;
- non-destructive preflight batch and hardware validation protocol.

No real Windows GPU result is claimed in this release; current environment cannot execute D3D11/FXC.

## 0.10.0 — 2026-08-17

Added:
- one-click `RUN_PTAR_AUTO.bat`;
- PowerShell 4-compatible Windows 8.1 setup/orchestration;
- automatic VS 2019 Build Tools detection/install;
- automatic Windows 8.1 SDK / FXC detection/install;
- Microsoft Authenticode validation before downloaded installers run;
- automatic vcvars64 environment import;
- automatic build, DXBC audit, GPU parity and timing launch;
- unique append-only automation logs/runs;
- verify-only toolchain launcher.

Changed:
- hardware validator now builds with `/MT`;
- hardware validator linker subsystem explicitly targets Windows 8.1 (`6.03`).

Safety:
- no project cleanup/deletion;
- no automatic reboot;
- no UAC bypass.

## 0.10.1 — 2026-08-17

Fixed:
- real Windows 8.1 PowerShell/UAC path failure caused by relaying a
  trailing-backslash ProjectRoot;
- false success message after the early root-resolution failure;
- verify-only launcher now follows the same self-rooting strategy.

Unchanged:
- PTAR algorithms and shaders;
- K185 benchmark data;
- corpus and historical records;
- hardware-test methodology;
- non-destructive safety rules.

## 0.10.2 — 2026-08-17

Fixed:
- real Windows 8.1 secure-channel failure in Microsoft prerequisite downloading.

Added:
- BITS download path;
- bitsadmin fallback;
- WebClient/TLS1.2 fallback;
- certutil fallback;
- validation/reuse of signed per-engine candidates;
- last-resort TLS1.2/.NET strong-crypto compatibility repair with pre-state backup;
- exit code 61 for manual-restart-required state.

Unchanged:
- PTAR/EDGE-NG/K185 algorithms;
- shaders/corpora/benchmarks;
- no automatic reboot;
- no destructive cleanup.

## 0.10.3 — 2026-08-17

Fixed:
- hardware validation no longer blocks when Windows 8.1 SDK setup succeeds but
  does not expose fxc.exe.

Changed:
- HLSL compilation moved into the native validator via D3DCompileFromFile;
- K185 DXBC audit moved into the validator via D3DDisassemble;
- FXC is now optional instead of mandatory;
- compiled CSO/ASM artifacts remain preserved in the run directory.

Unchanged:
- Direct3D 11 / SM5 target;
- EDGE-NG v03 K185;
- 1 gather + 4 sample target;
- parity and GPU timing gates;
- non-destructive policy.

## 0.10.4 — 2026-08-17

Fixed:
- real MSVC C2589/C2062/C2059 failure at the hardware validator `std::max`
  expression caused by the Win32 `max` macro;
- `NOMINMAX` is now defined before `windows.h`;
- the affected `std::max` invocation is also macro-safe;
- contradictory optional-FXC logging after vcvars import.

Unchanged:
- EDGE-NG v03 K185 algorithm;
- D3D11/SM5 target;
- D3DCompiler bytecode path;
- corpus, historical records and benchmark gates;
- non-destructive automation policy.

## 0.10.5 — 2026-08-17

Fixed:
- real MSVC `/W4 /WX` C4244/C2220 failure caused by constructing
  `std::string` directly from `std::wstring` iterators;
- case IDs and GPU adapter names now use explicit UTF-16 -> UTF-8 conversion
  through `WideCharToMultiByte(CP_UTF8)`.

Unchanged:
- EDGE-NG v03 K185 algorithm and shader;
- D3DCompiler SM5 path;
- corpus/history/benchmarks;
- hardware parity and timing gates;
- non-destructive automation.

## 0.10.6 — 2026-08-17

Real GTX 960M milestone:
- native build PASS;
- SM5 compile PASS;
- DXBC audit PASS: 1 gather4 / 4 sample_l / 0 UAV;
- GPU parity 42/42 PASS with max error 1 LSB.

Fixed:
- old timing harness could accumulate a huge unresolved fullscreen draw backlog
  and triggered Windows/NVIDIA TDR;
- 300-draw unbounded warm-up replaced by 32 completed serial warm-up draws;
- 200000-iteration feed loop removed;
- 512 measured draws are serialized;
- GetData flags=0 is used for bounded forward progress without explicit Flush();
- device-removal reason is checked and reported.

PTAR does not modify Windows TDR configuration.

## 0.10.7 — 2026-08-17

Real GTX 960M v0.10.6 result:
- 42/42 GPU parity PASS;
- K185 median 0.497840 ms;
- bilinear median 0.503136 ms;
- no TDR during the corrected timing campaign.

Fixed:
- process crash after final results caused by COM apartment shutdown occurring
  before automatic WIC/D3D COM-owning objects left scope;
- COM initialization is now managed by an RAII guard declared before all COM
  resources;
- D3D11 context ClearState is called during harness destruction.

Goal:
- preserve all validated GPU/timing behavior and obtain a clean exit code 0.

## 0.10.8 — 2026-08-17

Added:
- official preservation of the user-supplied real GTX 960M results package;
- forensic metadata for the 42 zero-byte PNG outputs found in that run;
- WIC RGBA -> BGRA PNG conversion;
- mandatory PNG HRESULT + non-zero file validation;
- gpu_png_bytes column in parity.csv;
- paired/interleaved K185 vs bilinear timing;
- timing_pairs.csv with 512 A/B pairs and per-pair delta;
- paired mean/median delta and win counts in hardware_summary.txt.

Unchanged:
- EDGE-NG v03 K185 shader/math;
- 1 gather4 + 4 sample_l + 0 UAV target;
- parity tolerance <= 1 LSB;
- TDR-safe one-draw-in-flight timing;
- COM RAII cleanup;
- non-destructive automation policy.

## 0.11.0 — 2026-08-17

Milestone:
- EDGE-NG v03 K185 hardware validation closed on GTX 960M.

Added:
- PTAR-NG MoE v01 SF5;
- NATURAL-NG v01 N70;
- RASTER-NG v01 MCLAMP;
- ROUTER-NG v01 R1-SOFT;
- complete MoE HLSL retaining 1 gather + 4 sample logical texture path;
- float64 reference and float32 shader semantic;
- 42-case Protocol-B campaign;
- MoE GPU validator and paired MoE-vs-K185 timing;
- archived real v0.10.8 GTX 960M evidence.

CPU Protocol-B:
- full42 delta SSIM vs K185: +0.004225749;
- full42 delta PSNR: +0.422479 dB;
- EDGE_CORE delta SSIM: +0.000943820;
- semantic max difference: 1 LSB.

Historical MoE v07 / NATURAL v02 / RASTER v03 remain unreconstructed.

## 0.12.0 — 2026-08-18

Milestone:
- PTAR-NG MoE v01 SF5 real GTX 960M hardware validation accepted.
- Clean exit/no TDR/no error confirmed by user.

Real hardware:
- MoE median: 0.655488 ms;
- K185 control median: 0.498240 ms;
- MoE overhead: 0.157248 ms;
- parity: 42/42 PASS, max 1 LSB;
- forensic PNG: 42/42 non-zero.

Added:
- autonomous precompiled-DXBC validation bundle;
- exact validated CSOs frozen with SHA-256 provenance;
- native host with no D3DCompiler source/API dependency;
- shader SHA-256 verification through Windows BCrypt;
- one-time /MT host builder;
- dumpbin import gate rejecting D3DCompiler/VCRUNTIME/MSVCP/UCRTBASE;
- runtime-only launcher with sanitized PATH hiding cl.exe/fxc.exe;
- post-toolchain-removal verification launcher.

No uninstall command is included yet. Toolchain removal remains gated on a
real successful autonomous run.

## 0.12.1 — 2026-08-18

Fixed:
- real MSVC C2065 build failure in the autonomous host;
- two residual corpus paths incorrectly referenced the removed `root`
  identifier instead of `bundleRoot`;
- autonomous validation now reads both input and expected images exclusively
  from its self-contained runtime bundle.

Unchanged:
- validated MoE v01 SF5 DXBC;
- CSO SHA-256 values;
- no-runtime-D3DCompiler policy;
- /MT host design;
- parity/timing/PNG gates;
- non-destructive policy.

## 0.12.2 — 2026-08-18

Fixed:
- real autonomous runtime code-2/Usage failure after a successful host build;
- `%~dp0` trailing backslash is no longer passed directly as the quoted
  `--bundle-root` value;
- bundle path is canonicalized with `%~dp0.` / `%%~fI`;
- runtime launcher logs exact bundle/out arguments before execution.

Unchanged:
- precompiled validated DXBC;
- CSO SHA-256 values;
- no D3DCompiler runtime dependency;
- /MT host and import audit;
- parity/PNG/timing gates;
- non-destructive policy.

## 0.12.3 — 2026-08-18

Fixed:
- v0.12.2 code-40 launcher failure caused by missing separators after BUNDLE
  canonicalization;
- all autonomous path joins now follow one explicit-separator convention.

Hardened:
- EXE/CSO/42+42 asset preflight;
- 42 PNG + 512 timing-pair postflight;
- stage-local .obj output;
- expanded import deny list including API-MS-WIN-CRT;
- output EXE existence/size checks.

Windows 8.1:
- autonomous C++ source kept byte-for-byte identical to v0.12.2, whose real
  Windows 8.1 compile/link succeeded;
- base D3D11/DXGI1.1/WIC/COM/BCrypt API surface audited;
- no D3D12/DXGI1.2+/D3D11.1+/runtime D3DCompiler.

No shader/algorithm/CSO hash change.

## 0.13.0 — 2026-08-18

Added production PTAR spatial core:
- exact validated MoE DXBC embedded in source;
- D3D11 VS/PS/constant-buffer/sampler lifecycle;
- exact x1.5 integer activation gate;
- one fullscreen Draw, no compute/UAV/intermediate texture;
- no D3DCompiler, Flush, Present or wait in the module;
- explicit integration contract for P1FG7N/VBLANK2/NISFG1B18K18;
- expected B18K18 ZIP/DLL identity gate.

Production policy:
- 1280x720 -> 1920x1080: PTAR active;
- native 1:1: existing direct fast path;
- non-x1.5: POINT/BILINEAR fail-open;
- NIS is not a PTAR fallback.

The actual proxy DLL is not patched in v0.13.0 because the exact current
B18K18 source ZIP is not present in this conversation runtime.
