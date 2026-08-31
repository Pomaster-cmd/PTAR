# PTAR

**PTAR** is an experimental Windows 8.1 / Direct3D 11 spatial reconstruction and frame-generation research project.

The current integrated runtime combines the **PTAR-NG MoE v01** spatial reconstruction path with an asynchronous NVIDIA NVENC-based motion-estimation / frame-generation pipeline. The project is focused on extending useful rendering capabilities on older Windows and GPU configurations while keeping the runtime non-destructive, measurable and reproducible.

> **Project status:** active research / laboratory build. The current `main` branch is not a stable end-user release.

## Repository layout

This repository deliberately separates the current integrated runtime from the recovered and reproducible PTAR source project:

- **`main`** — current integrated Windows 8.1 runtime package, installer, verifier, diagnostics, validation records and binary hotfix sources.
- **`SOURCE`** — PTAR Project Master: spatial-engine source code, permanent corpora, benchmarks, Windows build/validation tooling, hardware evidence, runtime integration contracts and the production PTAR-NG MoE module.

The `SOURCE` branch is preserved separately so historical and research material is not silently rewritten to match later integrated binaries.

## Current integrated runtime

Current package identity:

`PTAR_DISPLAY_DELIVERY_LAB_DWMPHASE2_AUTOCOLLECT1_RC18_BASE_VERIFYFIX5`

Current runtime family:

`P1FG7N-VBLANK2-PTARFG1B18K18`

Key properties:

- Windows 8.1 x64 target.
- Direct3D 11 presenter/proxy architecture.
- NVIDIA NVENC motion-estimation path used by the frame-generation system.
- Asynchronous frame-generation worker and isolated display path.
- PTAR-NG MoE v01 spatial reconstruction for exact x1.5 scaling.
- Bilinear fail-open path when PTAR geometry is unsupported.
- NIS is not used as a PTAR runtime fallback.
- Four selectable frame-generation quality profiles.
- Optional Intel Quick Sync H.264 recorder path.
- Non-destructive installer and ownership-aware uninstall system.
- External visible-frame verifier and DWM phase diagnostics.

The common PTAR spatial case is:

`1280x720 -> 1920x1080`

which is an exact x1.5 reconstruction in both dimensions.

## PTAR-NG MoE production core

The production spatial module is available in the `SOURCE` branch under:

`runtime_production/ptar_moe_v01_core/`

It contains the embedded validated DXBC, the D3D11 runtime wrapper and the integration contract used to insert PTAR into the integrated presenter without redesigning frame generation or the recorder.

The integration contract explicitly keeps the existing FG-selected REAL/GENERATED source ordering and applies PTAR only at the spatial reconstruction stage.

## Current RC18 presentation investigation

Image synthesis and internal FG scheduling are functional, but the current research blocker is **visible delivery of generated frames through the DXGI/DWM presentation chain** on the Windows 8.1 test platform.

The RC17 field baseline showed a large difference between internally reported output and externally observed desktop contents. On the Windows 8.1 x64 / GTX 960M / Inquisitor - Martyr test machine, an external marker verifier measured approximately:

- 15.578 visible REAL frames/s
- 0.793 visible GENERATED frames/s
- 16.372 visible UNIQUE frames/s

while the internal PTAR path was reporting/presenting close to 30 frames/s.

RC18 `PRESENTDELIVERY1` therefore changed only the presentation contract: the two explicit pre-Present VBlank waits were bypassed and the isolated display path moved to `Present(1, 0)`. The current `DWMPHASE2 AUTOCOLLECT1` build adds passive DWM timing evidence collection without redesigning the FG engine.

The success criterion for this work is **externally verified unique/generated visible frame rate and cadence**, not the internal HUD counter.

## Validation state

The current DWMPHASE2 package has a deterministic static-validation record of:

`84 / 84 PASS`

Current reference hashes:

- Runtime DLL SHA-256: `3d4d777c943ced0f475df1371d3a2f9eeb5eeb80c66e9fb217c4d91057f32453`
- RC18 presentation base SHA-256: `2b6fafabeedc89857dbb6d5a318ca143ce8e30ec912050116f70a4a58ad55ad3`
- Visible verifier SHA-256: `67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305`

See `diag/` for build records, deterministic validation, field findings, hardware protocols and historical regression evidence.

## Installation — laboratory package

The current `main` package is intended for controlled testing, not general deployment.

1. Place/extract the package in the game tree where the installer can locate the real x64 rendering executable.
2. Run `01-INSTALL_FULLSTACK1.bat`.
3. Review the detected renderer before confirming installation.
4. After installation, the FULLSTACK verifier runs automatically.
5. `PTAR_VERIFY_LAST.log` records the verification result.

The installer refuses ambiguous renderer selection and unknown pre-existing `d3d11.dll` proxies instead of overwriting them.

Use `05-DESINSTALLER_PTAR.bat` for the ownership-aware PTAR uninstall path.

## Main runtime controls

Current controls include:

- `CTRL+F6` — toggle frame generation.
- `CTRL+F8` — show/cycle the FG quality profile menu.
- `F8` — runtime status; the current diagnostic build also triggers the DWM diagnostic dump after the normal status action.
- `CTRL+F11` — toggle PTAR HUD.
- `CTRL+F9` — video recorder.
- `F12` — spatial/filter profile selection according to the active runtime configuration.

Frame generation starts **disabled** by default.

## Visible-frame diagnostics

The external visible-frame verifier is located under:

`diag/visible_verifier/`

The supported diagnostic entry point is:

`diag/04-ARM_VISIBLE_FRAME_VERIFIER.bat`

The verifier accepts `F5` or `CTRL+F5` to start the measurement. `ESC` is intentionally ignored because it is used normally by games and previously caused accidental diagnostic cancellation.

`CTRL+F4` remains the manual DWM snapshot fallback in the current diagnostic runtime.

## SOURCE branch

The `SOURCE` branch is the durable PTAR Project Master and contains substantially more than the integrated package on `main`, including:

- `src/` — PTAR engine/reference source.
- `runtime_production/` — production D3D11 PTAR-NG MoE integration module.
- `runtime_integration/` — D3D11 host contracts and integration material.
- `build/windows/` — Windows build and hardware-validation tooling.
- `benchmarks/` — benchmark infrastructure and results.
- `corpus/` — versioned permanent regression/perceptual corpora.
- `hardware_evidence/` and `hardware_validation/` — real-machine evidence and validation tooling.
- `tests/` and `validation/` — automated regression and integrity checks.
- `history/` — recovered historical project information, including explicitly tracked missing artifacts.

The Project Master follows an important provenance rule: missing historical artifacts are recorded as missing rather than silently reconstructed from synthetic substitutes.

## Source availability and reproducibility

The repository now contains the PTAR spatial engine source, production PTAR-NG MoE source and the binary-patch/hotfix sources used by the current integrated branch.

Some parts of the historical P1FG7N presenter lineage were developed and preserved as validated binary integration/hotfix material rather than as one canonical monolithic C/C++ source tree. The repository therefore distinguishes between:

- source-backed PTAR spatial modules;
- deterministic binary integration patches;
- immutable validation/evidence records;
- historical artifacts that are still explicitly missing.

This distinction is intentional and should be preserved in future work.

## Development policy

PTAR development follows several project-level constraints:

- preserve known-good subsystems unless a change is required by the active defect;
- prefer non-destructive installation and rollback behavior;
- keep build, validation and field evidence with the corresponding implementation;
- use deterministic hashes whenever possible;
- distinguish laboratory/static validation from real hardware validation;
- never treat an internal FPS counter as proof of actual visible frame delivery;
- never rewrite historical evidence to make a newer implementation appear equivalent.

## License

PTAR is distributed under the **GNU General Public License v3.0**. See `LICENSE` for the full license text.

## Experimental software notice

PTAR currently targets unusual and legacy rendering configurations and includes low-level Direct3D interception, presentation and hardware-acceleration experiments. Use laboratory builds only on systems where rollback and game-file backups are available. Hardware-specific behavior can differ across GPU generations, drivers, Optimus configurations, games and presentation modes.
