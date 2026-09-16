
## Universal game targeting — UNIVERSAL1

PTAR is **not tied to Warhammer**. Warhammer/Inquisitor is only one historical hardware-validation title.

The current runtime DLL is game-agnostic: the exact HUDREC1 binary `e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429` contains no `Warhammer`, `Inquisitor` or `NeoCore` literal. `TargetExe` is selected at installation time and written into the installed INI.

Supported targeting workflow:

- extract PTAR beside the real x64 game executable and run `01-INSTALL_GW16.bat`;
- if exactly one suitable x64 EXE is present, it is selected automatically;
- if several are present, the installer asks which one to target;
- for games whose renderer lives elsewhere (for example `Binaries\Win64`), run:
  `01-INSTALL_GW16.bat "C:\full\path\Game.exe"`
- the exact executable and its directory are stored in `win81_nis_install_exe.txt`, `win81_nis_install_target.txt` and the installation state;
- verify, collection, visible-frame diagnostics, marker control, rollback and uninstall all reuse that recorded target;
- no game-specific registry key is modified.

The current production binary is x64 D3D11. Games using another graphics API or a 32-bit process require a corresponding PTAR runtime and are outside this build's compatibility envelope.


# PTAR

**PTAR** is an experimental Windows 8.1 / Direct3D 11 spatial reconstruction and frame-generation research project.

PTAR combines the PTAR-NG MoE spatial reconstruction path with an asynchronous frame-generation pipeline using NVIDIA NVENC motion-estimation capabilities and a custom Direct3D 11 presentation path. The project targets measurable, reproducible graphics improvements on legacy Windows/GPU configurations, with external visible-frame validation and non-destructive install/rollback tooling.

> **Current runtime:** **SAFEPOINT11 / FUSEDDETAIL1 / HUDREC1 UNIVERSAL1** — game-agnostic x64 Direct3D 11 targeting. HUDREC1 runtime bytes remain `e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429`.

## Current baseline — SAFEPOINT11 / FUSEDDETAIL1

SAFEPOINT11 keeps the validated GW16H presenter, recorder, QSV scheduling, hotkey and native-state fixes, and branches the FG quality work from SAFEPOINT8/TDETAIL4. SAFEPOINT9/10 parameter experiments are deliberately excluded from this lineage.

The main visual change is **FUSEDDETAIL1**, fused directly into the existing generated-frame compute shader for profile 3 **CONSERVATIVE**.

Its design constraints are cost-first:

- no new texture resource;
- no new UAV;
- no new resource binding;
- no new Dispatch;
- source-level `.Load()` count remains **7**, identical to TDETAIL4;
- REAL frames are untouched;
- only a small ALU/min/max/lerp tail is added to the GENERATED path.

FUSEDDETAIL1 reuses the four bilinear texels already loaded from each REAL endpoint, estimates a local 2x2 green-channel range and limits only excessive GENERATED deviation from the unwarped REAL blend.

It is a temporal-detail stabilizer, **not a sharpening filter**.

## Hardware validation status

The SAFEPOINT11/FUSEDDETAIL1 hardware gate was run on the Windows 8.1 / GTX 960M reference machine with PTAR x1.5 and FG active, using profile 3 **CONSERVATIVE**.

The field run confirmed the intended improvement on the two localized high-frequency problem areas used during development:

- the selection circle / halo at the character feet;
- repetitive floor grilles / vents.

Compared with the TDETAIL4 reference capture, the strong high-frequency frame-to-frame excursion on aligned problem windows fell substantially. The field video also showed no corresponding presenter, FG-runtime or QSV failure signature attributable to FUSEDDETAIL1.

The validated runtime identity is:

`864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c`

The validated package supplied for promotion has SHA-256:

`dd2287c2e9d5e3ba7cc0c588821c47419ce1aaaee37a7f5a4b3b25d792786114`

Static package checks in the promoted package include:

- FUSEDDETAIL1 validation: **70/70 PASS**;
- package validation: **83/83 PASS**;
- internal SHA ledger: **103/103** entries verified before promotion.


## HUDREC1 — video recording with PTAR HUD / FPS

This package keeps the validated **SAFEPOINT11 / FUSEDDETAIL1** frame-generation and spatial-reconstruction algorithms unchanged and changes only the native FG-OFF recorder source so recorded MP4 files can contain the same PTAR HUD that is visible on screen.

- **FG ON:** the recorder continues to consume the existing final isolated presenter BackBuffer used by the validated REAL + GENERATED path; this route is unchanged.
- **FG OFF:** the recorder now consumes `BackBuffer0` from the final visible PTAR presenter instead of the game swapchain BackBuffer. The native HUD draw already occurs immediately before recorder submission.
- With `Overlay=1`, the recorded image is therefore intended to include the PTAR HUD, including the **FPS** value.
- The FG algorithm, FUSEDDETAIL1 shader, quality profiles, NVENC ME policy, QSV recorder conversion path and presentation cadence are not otherwise changed.

The HUD recording change is lab/static validated in this package. The final hardware gate is one short GTX 960M session with two ~5 s recordings: Clip A with FG OFF (the modified route) and Clip B with FG ON (the preserved route), both confirming that HUD/FPS is visible in the encoded MP4.

### FG cadence marker (blinking squares)

The small blinking squares shown while FG is active are the **VBlank visible cadence marker**. They are independent from the main HUD/FPS display.

Run:

`diag\\FG_MARKER_VISIBILITY.bat`

The menu can:

1. show the current marker state;
2. enable the blinking FG squares;
3. disable the blinking FG squares;
4. exit without changing anything.

This changes only `VBlankDiagnostics=0/1`. It does **not** disable the PTAR HUD or FPS. The tool backs up the installed INI before changing it, and `02-VERIFY_INSTALL.bat` accepts this single supported configuration difference while still rejecting any other unexpected INI modification.

## QUALITY versus CONSERVATIVE

The current profile semantics are intentionally different.

### QUALITY

QUALITY remains the highest-fidelity FG profile. FUSEDDETAIL1 is mathematically bypassed at the QUALITY gate (`G=.35`), so SAFEPOINT11 does not intentionally alter QUALITY image synthesis.

A remaining field defect has been identified in QUALITY: a visible **motion trail / ghost-like persistence around the moving character with FG active**. This is tracked separately from the FUSEDDETAIL1 work.

### CONSERVATIVE

CONSERVATIVE prioritizes temporal stability on difficult thin/repetitive structures. FUSEDDETAIL1 is active here and materially reduces the localized shimmer/detail oscillation observed in earlier profile-3 experiments.

The intention is not to make CONSERVATIVE globally softer. Stable and low-motion content should remain nearly unchanged while unstable generated high-frequency detail is selectively bounded.

## Spatial reconstruction — PTAR-NG MoE

The current spatial path remains **PTAR-NG MoE v01**, commonly used at:

`1280x720 -> 1920x1080`

for an exact x1.5 reconstruction in both dimensions.

Spatial reconstruction and frame generation remain distinct subsystems. The next major quality work therefore targets the PTAR reconstruction engine itself rather than trying to compensate for spatial limitations inside FG.

## What next — PTAR-NG MoE v02 / Native Detail Recovery

The next planned development phase is an improvement of the **PTAR engine itself**.

The objective is to move PTAR's reconstructed 1080p image materially closer to a native 1920x1080 reference while retaining the performance characteristics that make PTAR useful on hardware such as the GTX 960M.

The first v02 laboratory branch will keep the current single-pass architecture and will initially try to preserve the current texture-sampling budget. The main research directions are:

1. **x1.5 subpixel-phase reconstruction** — exploit the repeating phase structure of the fixed 720p -> 1080p mapping instead of treating every output position identically;
2. **stronger directional experts** — improve reconstruction of horizontal, vertical and diagonal structures rather than merely increasing local contrast;
3. **bounded micro-detail residual** — recover useful luminance detail while preventing halos, ringing and artificial oversharpening;
4. **flat/edge/texture discrimination** — avoid turning 720p aliasing or noise into false detail;
5. **anti-ringing / anti-shimmer constraints** — spatial quality gains must not recreate the temporal instability already observed on grilles, circles and other repetitive structures.

The native 1080p field capture will be used as ground truth in the laboratory:

`native 1080p -> controlled downsample to 720p -> PTAR candidate -> comparison with original native 1080p`

Candidates will be evaluated before any new hardware request. The initial goal is to gain fidelity mainly through better computation/weights rather than additional bandwidth. A higher-cost variant with one extra texture access will only be considered if the sampling-constant v02 path does not close enough of the native-quality gap.

Only after the spatial engine has improved sufficiently will the remaining QUALITY FG motion-trail defect be revisited on top of the stronger spatial baseline.

## Frame-generation architecture

PTAR's frame-generation pipeline includes:

- Direct3D 11 proxy/presenter interception;
- asynchronous generated-frame work;
- NVIDIA NVENC-based motion-estimation support;
- explicit REAL / GENERATED ordering;
- isolated display/presentation handling;
- visible cadence diagnostics;
- external visible-frame verification;
- live FG quality-profile infrastructure;
- original B18K18/QSV recorder integration with the SAFEPOINT recorder/state fixes.

A generated frame counts as successful only when it is actually delivered through the visible DXGI/DWM presentation chain. Internal counters are diagnostic data, not proof of visible frame rate.

## Recorder and state-safety lineage

SAFEPOINT11 retains the validated recorder work developed before FUSEDDETAIL1, including:

- one original internal B18K18 recorder path;
- `CTRL+F9` recording with FG ON or OFF;
- native swapchain BackBuffer0 capture for FG-OFF USR paths;
- recorder-local fullscreen VS / rasterizer state tied to the active recorder device;
- native immediate-context state save/restore around the recorder conversion path;
- QSV encode/pipe scheduling at NORMAL while the GPU readback worker remains BELOW_NORMAL;
- safe finalization and evidence collection.

These subsystems were intentionally not redesigned for FUSEDDETAIL1.

## Installation / controlled validation

1. Close the game.
2. Run `01-INSTALL_GW16.bat`.
3. Run `02-VERIFY_INSTALL.bat` and require `VERIFY=PASS`.
4. Launch the test scene.
5. Enable FG with the supported hotkey path.
6. Use `CTRL+F8` for the supported live FG profile selection policy.
7. Use `CTRL+F9` to start/stop the integrated recorder when evidence is needed.
8. Run `04-COLLECT_RESULTS.bat` to collect the diagnostic package.

Frame generation starts **OFF** by default.

`05-ROLLBACK_TEST.bat` provides the controlled rollback path. `06-DESINSTALLER_PTAR_COMPLET.bat` uses the ownership/SHA-aware safe uninstall mechanism.

## Validation model

PTAR separates three validation levels:

- **laboratory/static validation** — package structure, deterministic patches, binary identities, shader/source invariants and regression contracts;
- **runtime validation** — confirmation that the intended runtime/configuration path is active;
- **hardware field validation** — real GPU/driver timing, visible frame delivery and perceptual behaviour that cannot be proven offline.

Hardware claims are made only from actual hardware evidence.

## Repository layout

- **`main`** — promoted hardware-validated runtime baseline, installer/rollback/recorder tooling, diagnostic evidence and current documentation.
- **`SOURCE`** — durable PTAR Project Master with source, integration material, corpora, benchmarks, build tooling, validation and historical evidence.

Historical evidence is retained as evidence. Missing historical source is identified as missing rather than silently reconstructed and presented as original source.

## Development policy

PTAR development follows a few strict rules:

- preserve validated subsystems unless the active defect requires a change;
- isolate experiments and promote only after the appropriate gate;
- prefer non-destructive installation, rollback and uninstall behaviour;
- keep deterministic hashes and evidence with each validated implementation;
- perform all reproducible laboratory tests before requesting a hardware test;
- request hardware testing only for behaviour that cannot be reproduced locally;
- never treat an internal FPS counter as proof of visible output;
- prioritize quality improvements that preserve the performance envelope of the target legacy hardware.

## License

PTAR is distributed under the **GNU General Public License v3.0**. See `LICENSE`.
