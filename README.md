# PTAR

**PTAR** is an experimental Windows 8.1 / Direct3D 11 spatial reconstruction and frame-generation research project.

The project combines a PTAR-NG MoE spatial reconstruction path with an asynchronous frame-generation pipeline using NVIDIA NVENC motion-estimation capabilities and a custom Direct3D 11 presentation path. Its purpose is to extend useful rendering capabilities on older Windows and GPU configurations while keeping the runtime measurable, reproducible and non-destructive.

The project is developed as a research/runtime engineering effort rather than as a simple graphics preset or post-processing filter. PTAR includes spatial reconstruction, frame synthesis, presentation/cadence control, external visible-frame verification, installation/rollback tooling and hardware evidence.

> **Current status:** `main` is the hardware-validated **GW15 LOCK30** baseline. The next development target is **GW16 AUTO**, an adaptive 60/30 FPS mode. GW16 is not promoted to `main` until its hardware gate passes.

## Project goals

PTAR is built around several constraints:

- keep Windows 8.1 x64 and Direct3D 11 as first-class targets;
- preserve compatibility with older NVIDIA mobile hardware such as the GTX 960M test platform;
- separate spatial reconstruction from frame generation and presentation logic;
- use external visible-frame measurement rather than trusting an internal FPS counter;
- preserve known-good subsystems while isolating changes to the active defect or experiment;
- provide deterministic hashes, validation records and rollback paths;
- distinguish laboratory/static validation from real hardware validation;
- keep historical evidence instead of rewriting it to fit a newer implementation.

## Spatial reconstruction — PTAR-NG MoE

The production spatial path is based on **PTAR-NG MoE v01**.

Its common validated geometry is:

`1280x720 -> 1920x1080`

which is an exact x1.5 reconstruction in both dimensions.

The production module is maintained in the `SOURCE` branch under:

`runtime_production/ptar_moe_v01_core/`

It contains the embedded validated DXBC, the D3D11 runtime wrapper and the integration contract used to insert PTAR into the rendering pipeline without redesigning frame generation or the recorder path.

The integration contract preserves the selected REAL/GENERATED source ordering and applies PTAR at the spatial reconstruction stage. A bilinear fail-open path is available when PTAR geometry is unsupported. NIS is not used as the PTAR runtime fallback.

## Frame-generation architecture

PTAR's frame-generation work is built around an asynchronous presentation pipeline rather than a simple duplicate-frame mechanism.

The wider runtime architecture includes:

- Direct3D 11 proxy/presenter interception;
- asynchronous frame-generation work;
- NVIDIA NVENC-based motion-estimation support;
- explicit REAL / GENERATED frame ordering;
- isolated display/presentation handling;
- visible cadence diagnostics;
- externally measured frame identity and dwell timing;
- quality/profile infrastructure inherited from the integrated runtime lineage.

A central design rule is that a generated frame only counts as successful when it is actually visible through the DXGI/DWM presentation chain. Internal generation counters are therefore diagnostic information, not proof of delivered frame rate.

## Current `main` — GW15 LOCK30

`main` currently contains the **hardware-validated GW15 LOCK30 baseline**.

GW15 was selected as the canonical baseline because it delivers a stable and externally verified 30 FPS visible cadence on a fixed 60 Hz display. It uses the validated Flip Sequential 3-buffer presenter with `Present(2, 0)` and a 30 FPS FG target corresponding to a 15 REAL + 15 GENERATED target split.

External visible-frame validation on the Windows 8.1 / GTX 960M test platform measured:

- visible unique FPS: **29.994**
- generated FPS: **14.997**
- real FPS: **14.997**
- generated dwell median: **33.303 ms**
- real dwell median: **33.345 ms**
- pair imbalance median: **0.221%**
- pair imbalance p95: **0.771%**
- midpoint balance: **99.875%**

These measurements are external visible-delivery results, not only internal PTAR counters.

### GW15 identity

Runtime SHA-256:

`2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d`

Validated package SHA-256:

`b78fd4bf42f2e7d2eb9f72b0eb59c454e1fe53380d151dd430b29874f287aa40`

The product files promoted to `main` were kept byte-identical to the hardware-validated GW15 package. Repository metadata such as this README and `LICENSE` are maintained separately from the validated package payload.

## Why GW15 matters

Earlier PTAR frame-generation work showed that internal synthesis and internal presentation statistics could look correct while the Windows 8.1 desktop-visible output still dropped or failed to expose generated frames at the expected cadence.

This made the presentation chain itself a research target.

The project therefore moved from internal FPS validation toward external visible-frame verification, including frame identity, REAL/GENERATED ordering, dwell time and cadence balance. GW15 is the first baseline in this progression that is currently promoted as the hardware-validated `main` state.

The practical consequence is important: **PTAR does not claim a frame-generation result merely because the engine reports one. The visible output has to be measured.**

## Historical presentation investigation

The RC17/RC18 investigation is retained because it explains why current PTAR validation is strict.

On the Windows 8.1 x64 / GTX 960M / Inquisitor - Martyr test machine, one RC17 field baseline showed approximately:

- 15.578 visible REAL frames/s
- 0.793 visible GENERATED frames/s
- 16.372 visible UNIQUE frames/s

while the internal PTAR path was reporting/presenting close to 30 frames/s.

RC18 `PRESENTDELIVERY1` then changed the presentation contract by bypassing two explicit pre-Present VBlank waits and moving the isolated display path to `Present(1, 0)`. Later work added passive DWM timing evidence, pacing verification and progressively stricter visible-delivery analysis.

Those RC17/RC18 values are **historical investigation data**, not the current GW15 performance figures.

## Next target — GW16 AUTO

The next development target is **GW16 AUTO**.

The objective is not simply to force 60 FPS at all times. The intended behaviour is adaptive:

1. target 60 FPS when the measured rendering/presentation conditions can sustain it;
2. detect when 60 FPS is no longer sustainable;
3. fall back cleanly to the validated 30 FPS cadence instead of oscillating or producing uneven delivery;
4. remain stable at 30 FPS while the higher target is unsafe;
5. return to 60 FPS only when the recovery condition is sufficiently stable.

This requires hysteresis and a real hardware gate so that the controller does not repeatedly bounce between 60 and 30 FPS.

GW16 remains experimental and is intentionally **not** on `main` until both directions of the transition have been validated on hardware: 60 -> 30 under load and 30 -> 60 when headroom returns.

## Installation / GW15 validation

The current `main` package is intended for controlled testing.

1. Close the game and run `01-INSTALL_GW15.bat`.
2. Run `02-VERIFY_INSTALL.bat` and require `VERIFY=PASS`.
3. Launch the test scene and enable frame generation with `CTRL+F6`.
4. Wait approximately 2–3 seconds for the runtime state to settle.
5. Run `03-ARM_VISIBLE_FRAME_VERIFIER.bat` and press `F5` once.
6. The verifier emits a **900 Hz start beep** and a **1400 Hz end beep**.
7. Run `04-COLLECT_RESULTS.bat` to collect the evidence package.

Frame generation starts **OFF** by default.

`05-ROLLBACK_TEST.bat` is provided for the controlled test rollback path.

`06-DESINSTALLER_PTAR_COMPLET.bat` uses the ownership/SHA-aware safe uninstall path.

The installer/uninstaller design follows a non-destructive rule: PTAR should not silently overwrite or delete unknown third-party files.

## Validation model

PTAR uses several validation layers because they answer different questions.

### Static / laboratory validation

Used to verify package structure, deterministic patches, expected binary identities, configuration consistency and regression contracts that can be reproduced without the target GPU/game environment.

### Runtime validation

Used to verify that the intended runtime configuration is active and that the expected code/configuration path is being exercised.

### Hardware field validation

Used for behaviour that cannot be proven in the laboratory, including real GPU/driver timing, DXGI/DWM presentation behaviour and externally visible frame cadence.

The hardware result takes precedence for claims about actual visible FPS.

## Visible-frame diagnostics

PTAR's external verifier exists specifically to avoid false confidence from internal counters.

The current GW15 package exposes the supported entry point as:

`03-ARM_VISIBLE_FRAME_VERIFIER.bat`

The validation flow records externally visible frame transitions and allows REAL/GENERATED delivery and dwell timing to be analysed independently from PTAR's own HUD/statistics.

Historical diagnostic tooling and validation records are retained under `diag/` where relevant.

## Repository layout

The repository deliberately separates the promoted runtime from the durable source/reproducibility project.

- **`main`** — current hardware-validated GW15 LOCK30 runtime package, installer, verifier, validation tooling and current project documentation.
- **`SOURCE`** — PTAR Project Master containing source code, integration contracts, permanent corpora, benchmarks, build tooling, hardware evidence and historical material.

Only these two long-lived branches are kept after the GW15 cleanup. Temporary laboratory and staging branches are removed once their useful state has been promoted or preserved elsewhere.

## `main` package structure

The current promoted runtime contains, among other files:

- `01-INSTALL_GW15.bat`
- `02-VERIFY_INSTALL.bat`
- `03-ARM_VISIBLE_FRAME_VERIFIER.bat`
- `04-COLLECT_RESULTS.bat`
- `05-ROLLBACK_TEST.bat`
- `06-DESINSTALLER_PTAR_COMPLET.bat`
- `payload/d3d11.dll`
- `payload/win81_nis_dx11_x64.dll`
- `payload/win81_nis.ini`
- `diag/` validation and evidence tooling
- `_PTAR_UNINSTALL/` ownership-aware uninstall data

`payload/d3d11.dll` and `payload/win81_nis_dx11_x64.dll` are the promoted GW15 runtime binaries.

## `SOURCE` branch — PTAR Project Master

The `SOURCE` branch is the durable development/reproducibility branch and contains substantially more than the integrated package on `main`.

Its scope includes:

- `src/` — PTAR engine/reference source;
- `runtime_production/` — production D3D11 PTAR-NG MoE integration module;
- `runtime_integration/` — D3D11 host contracts and integration material;
- `build/windows/` — Windows build and hardware-validation tooling;
- `benchmarks/` — benchmark infrastructure and results;
- `corpus/` — permanent regression/perceptual corpora;
- `hardware_evidence/` and `hardware_validation/` — real-machine evidence and validation tooling;
- `tests/` and `validation/` — automated regression and integrity checks;
- `history/` — recovered historical project information, including explicitly tracked missing artifacts.

The Project Master follows an explicit provenance rule: if a historical artifact is missing, it is recorded as missing rather than silently replaced with a synthetic reconstruction and then presented as original evidence.

## Source availability and reproducibility

The repository contains PTAR spatial-engine source, production PTAR-NG MoE source and the binary-patch/hotfix material used by the integrated runtime lineage.

Some historical presenter work was preserved as validated binary integration/hotfix material rather than as one canonical monolithic C/C++ source tree. The repository therefore distinguishes between:

- source-backed PTAR spatial modules;
- deterministic binary integration patches;
- immutable validation/evidence records;
- historical artifacts that are explicitly missing.

This distinction is intentional and is part of the reproducibility model.

## Development policy

PTAR development follows project-level engineering constraints:

- preserve known-good subsystems unless a change is required by the active defect;
- avoid monolithic rewrites when a smaller isolated change can be validated;
- prefer non-destructive installation, rollback and uninstall behaviour;
- keep build, validation and field evidence with the corresponding implementation;
- use deterministic hashes whenever possible;
- distinguish laboratory/static validation from hardware validation;
- debug and validate packages before requesting a new hardware test;
- request hardware testing only for behaviour that cannot be reproduced locally;
- never treat an internal FPS counter as proof of actual visible frame delivery;
- never rewrite historical evidence to make a newer implementation appear equivalent.

## Experimental software notice

PTAR targets unusual and legacy rendering configurations and includes low-level Direct3D interception, presentation and hardware-acceleration experiments.

Behaviour can differ across GPU generations, drivers, Optimus configurations, games and presentation modes. Laboratory success is therefore not automatically equivalent to field success.

Use experimental builds only on systems where rollback and game-file backups are available.

## License

PTAR is distributed under the **GNU General Public License v3.0**. See `LICENSE` for the full license text.
