# PTAR

**PTAR** is an experimental Windows 8.1 / Direct3D 11 spatial reconstruction and frame-generation research project.

PTAR combines the PTAR-NG MoE spatial reconstruction path with an asynchronous frame-generation pipeline using NVIDIA NVENC motion-estimation capabilities and a custom Direct3D 11 presentation path. The project targets measurable, reproducible graphics improvements on legacy Windows/GPU configurations, with external visible-frame validation and non-destructive install/rollback tooling.

> **Current validated runtime target:** **GW16H UNIFIEDREC3 — SAFEPOINT11 / FUSEDDETAIL1** on Windows 8.1 x64 / GTX 960M. The validated runtime SHA-256 is `864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c`.

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

## Usage modes

PTAR spatial reconstruction and frame generation are separate features. The validated configuration uses a 1280x720 render surface reconstructed to a 1920x1080 output, while frame generation starts **OFF** by default (`FrameGeneration=0`).

### PTAR with frame generation OFF

This is the default startup mode.

- PTAR spatial reconstruction remains active.
- Only REAL frames are presented; no generated intermediate frame is inserted.
- The integrated recorder remains available.
- With FG OFF, the recorder captures the final PTAR/native REAL presenter stream from the native swapchain BackBuffer0 path.
- All four FG quality profiles can be selected while FG is OFF; the selected ME tier is applied the next time FG is enabled.

Use **`CTRL+F6`** to enable frame generation.

### PTAR with frame generation ON

With FG enabled, PTAR keeps the spatial reconstruction path and activates the asynchronous REAL + GENERATED presentation path.

- REAL and GENERATED frames are explicitly ordered by the presenter.
- The current validated FG target is 60 FPS.
- Automatic fallback to the old 30-FPS clamp is disabled in this baseline.
- The integrated recorder captures the final REAL + GENERATED presenter stream.

Use **`CTRL+F6`** again to disable frame generation.

### FG quality profiles

The runtime exposes four frame-generation profiles:

| ID | Profile | ME tier | Behaviour |
|---:|---|---|---|
| 0 | **LEGACY** | /3 | Historical /3 ME path + Guard65 |
| 1 | **BALANCED** | /3 | /3 ME + Guard50 |
| 2 | **QUALITY** | /2 | /2 ME + Guard35; highest-fidelity baseline, FUSEDDETAIL1 bypassed |
| 3 | **CONSERVATIVE / FUSEDDETAIL1** | /2 | /2 ME + Guard25, compressed motion trust and FUSEDDETAIL1 temporal-detail stabilizer |

The configured default is profile **2 — QUALITY**.

Profile selection uses **`CTRL+F8`**:

- first press: displays the current profile name without changing it;
- press again while the notice is still visible: selects the next permitted profile;
- after the notice disappears, the next press is display-only again;
- with FG **OFF**, the full `0 -> 1 -> 2 -> 3` cycle is available;
- with FG **ON**, QUALITYSAFE1 keeps live changes inside the current ME tier only: `0 <-> 1` or `2 <-> 3`. This avoids a live NVENC ME-ring resize/presenter recreation.

To switch from the `/2` tier to `/3`, or vice versa, disable FG first, select the desired profile, then enable FG again.

## Screenshot capture — F9

Press **`F9`** to capture a still frame through PTAR's integrated capture path.

Captured BMP files use the runtime naming scheme:

`win81_nis_capture_N.bmp`

F9 is independent from the MP4 recorder and can be used while the recorder is running.

## Integrated video recording — CTRL+F9

Press **`CTRL+F9`** to start the integrated B18K18/QSV MP4 recorder and press **`CTRL+F9`** again to stop/finalize the recording.

The same production recorder is used in both runtime modes:

- **FG ON:** records the final REAL + GENERATED presenter stream;
- **FG OFF:** records the final PTAR/native REAL presenter stream.

The recorder includes the SAFEPOINT state-safety fixes: recorder-local GPU state, native D3D11 immediate-context save/restore, QSV scheduling fixes and safe finalization.

### Video recording profiles

`VideoRecordProfile` in `payload/win81_nis.ini` selects the recording profile:

| Value | Name | Resolution / FPS | Bitrate | Intended use |
|---:|---|---|---:|---|
| 1 | **QUALITY** | 1920x1080 / 30 FPS | 16000 kbps | Maximum spatial detail for image-quality demonstrations |
| 2 | **MOTION** | 1600x900 / 60 FPS | 17000 kbps | Motion/FG demonstrations with lower capture load |
| 3 | **COMBINED** | 1920x1080 / 60 FPS | 22000 kbps | Full spatial detail + full 60 FPS; heaviest recording mode |

The validated package currently uses **profile 3 — COMBINED**. Invalid values fall back safely to profile 1.

## Runtime hotkeys

These are the hotkeys defined by the validated SAFEPOINT11/FUSEDDETAIL1 configuration:

| Shortcut | Runtime action |
|---|---|
| **F6** | Manual filter action (`ManualFilter`) |
| **CTRL+F6** | Toggle Frame Generation ON / OFF |
| **F7** | Run the integrated benchmark; current config uses 3 s warm-up + 10 s measurement |
| **F8** | Show runtime status |
| **CTRL+F8** | Show/change FG quality profile using the QUALITYSAFE1 rules described above |
| **F9** | Capture a BMP screenshot (`win81_nis_capture_N.bmp`) |
| **CTRL+F9** | Start/stop integrated MP4 video recording |
| **F10** | Toggle the PTAR presenter |
| **CTRL+F11** | Toggle the PTAR HUD/overlay |
| **F12** | Select the next filter (`FilterNext`) |

`CTRL+F6` and `CTRL+F8` have explicit hotkey-isolation fixes so their base-key actions do not leak into plain F6/F8 handling.

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

Spatial reconstruction and frame generation remain distinct subsystems.

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
4. Launch the game/test scene.
5. PTAR starts with frame generation OFF.
6. Use `CTRL+F6` to toggle FG when wanted.
7. Use `CTRL+F8` for supported live FG profile selection.
8. Use `F9` for still captures.
9. Use `CTRL+F9` to start/stop the integrated MP4 recorder.
10. Run `04-COLLECT_RESULTS.bat` when a diagnostic package is needed.

`05-ROLLBACK_TEST.bat` provides the controlled rollback path. `06-DESINSTALLER_PTAR_COMPLET.bat` uses the ownership/SHA-aware safe uninstall mechanism.

## Validation model

PTAR separates three validation levels:

- **laboratory/static validation** — package structure, deterministic patches, binary identities, shader/source invariants and regression contracts;
- **runtime validation** — confirmation that the intended runtime/configuration path is active;
- **hardware field validation** — real GPU/driver timing, visible frame delivery and perceptual behaviour that cannot be proven offline.

Hardware claims are made only from actual hardware evidence.

## Repository layout

The maintained branches are:

- **`main`** — promoted hardware-validated SAFEPOINT11/FUSEDDETAIL1 runtime baseline, installer/rollback/recorder tooling, diagnostic evidence and current documentation;
- **`SOURCE`** — durable PTAR Project Master containing source, integration material, corpora, benchmarks, build tooling, validation and historical evidence.

Experimental development branches are intentionally not part of the maintained branch set.

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
