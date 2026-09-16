# PTAR

**PTAR** is an experimental Windows 8.1 / Direct3D 11 spatial reconstruction and frame-generation research project.

PTAR combines the PTAR-NG MoE spatial reconstruction path with an asynchronous frame-generation pipeline using NVIDIA NVENC motion-estimation capabilities, a custom Direct3D 11 presentation path and an integrated QSV recorder.

> **Current validated runtime:** **GW16H UNIFIEDREC3 — SAFEPOINT11 / FUSEDDETAIL1 / HUDREC1** on Windows 8.1 x64 / GTX 960M.
> Runtime SHA-256: `e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429`
> Validated package SHA-256: `74de9ef43b012bdbf2175946fcabdc157fe9594401eb01922b79761248794b0a`

## Current baseline — SAFEPOINT11 / FUSEDDETAIL1 / HUDREC1

HUDREC1 is the current hardware-validated delivery baseline. It keeps the validated SAFEPOINT11/FUSEDDETAIL1 spatial and frame-generation algorithms and changes only the native FG-OFF recorder source so the encoded MP4 contains the final PTAR HUD, including the live FPS value.

The original SAFEPOINT11/FUSEDDETAIL1 runtime remains the direct ancestor:

`864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c`

The HUDREC1 runtime is:

`e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429`

Hardware validation on the Windows 8.1 / GTX 960M reference machine confirmed that the integrated recorder now includes the PTAR HUD/FPS in recorded video.

## What HUDREC1 changes

- **FG ON:** unchanged recorder route; the recorder already consumes the final isolated REAL + GENERATED presenter backbuffer after HUD composition.
- **FG OFF:** the recorder now consumes `BackBuffer0` from the final visible PTAR presenter instead of the game swapchain pre-HUD backbuffer.
- With `Overlay=1`, the encoded MP4 contains the visible PTAR HUD, including FPS.
- The FG algorithm, FUSEDDETAIL1 shader, quality profiles, NVENC ME policy, QSV conversion path and presentation cadence are otherwise unchanged.

The functional DLL delta from SAFEPOINT11 is intentionally minimal: one functional byte at the native recorder swapchain source selection, plus the PE checksum update.

## FG cadence marker (blinking squares)

The small blinking squares visible while FG is active are the **VBlank visible cadence marker**. They are independent from the main PTAR HUD/FPS.

Use:

`diag\FG_MARKER_VISIBILITY.bat`

The tool provides **STATUS / ON / OFF** control and changes only `VBlankDiagnostics=0/1`. Turning the marker OFF does **not** disable the FPS display or the rest of the HUD. The installed INI is backed up before modification, and `02-VERIFY_INSTALL.bat` accepts this supported setting difference while continuing to reject unrelated INI modifications.

## Usage modes

### PTAR with Frame Generation OFF

This is the default startup mode (`FrameGeneration=0`).

- PTAR spatial reconstruction remains active.
- Only REAL frames are presented.
- The integrated recorder remains available.
- HUDREC1 records the final PTAR/native REAL presenter stream **with HUD/FPS** when `Overlay=1`.
- The full FG profile cycle can be selected while FG is OFF.

Use **`CTRL+F6`** to enable frame generation.

### PTAR with Frame Generation ON

- Spatial reconstruction remains active.
- REAL and GENERATED frames are explicitly ordered by the presenter.
- The validated FG target is 60 FPS.
- The old automatic 30-FPS fallback is disabled.
- The recorder captures the final REAL + GENERATED presenter stream, including HUD/FPS when `Overlay=1`.

Use **`CTRL+F6`** again to disable FG.

## FG quality profiles

| ID | Profile | ME tier | Behaviour |
|---:|---|---|---|
| 0 | **LEGACY** | /3 | Historical /3 ME path + Guard65 |
| 1 | **BALANCED** | /3 | /3 ME + Guard50 |
| 2 | **QUALITY** | /2 | /2 ME + Guard35; FUSEDDETAIL1 bypassed |
| 3 | **CONSERVATIVE / FUSEDDETAIL1** | /2 | /2 ME + Guard25, compressed motion trust and FUSEDDETAIL1 temporal-detail stabilizer |

The configured default is **QUALITY (2)**.

`CTRL+F8` behaviour:

- first press shows the current profile without changing it;
- press again while the notice is visible to select the next permitted profile;
- FG OFF: full `0 -> 1 -> 2 -> 3` cycle;
- FG ON: QUALITYSAFE1 keeps changes inside the current ME tier (`0 <-> 1` or `2 <-> 3`).

To cross from `/2` to `/3` or vice versa, disable FG first, select the desired profile, then re-enable FG.

## Screenshot capture — F9

Press **`F9`** to capture a BMP through PTAR's integrated capture path.

Files use the naming scheme:

`win81_nis_capture_N.bmp`

F9 is independent from the MP4 recorder and can be used while video recording is active.

## Video recording with PTAR HUD / FPS — CTRL+F9

Press **`CTRL+F9`** to start recording and press it again to stop/finalize the MP4.

HUDREC1 records the visible PTAR output:

- **FG OFF:** final native PTAR presenter + HUD/FPS;
- **FG ON:** final REAL + GENERATED presenter + HUD/FPS.

### Video recording profiles

`VideoRecordProfile` in `payload/win81_nis.ini` selects the recorder profile:

| Value | Name | Resolution / FPS | Bitrate | Intended use |
|---:|---|---|---:|---|
| 1 | **QUALITY** | 1920x1080 / 30 FPS | 16000 kbps | Maximum spatial detail |
| 2 | **MOTION** | 1600x900 / 60 FPS | 17000 kbps | Motion/FG demonstrations with lower capture load |
| 3 | **COMBINED** | 1920x1080 / 60 FPS | 22000 kbps | Full spatial detail + 60 FPS; heaviest mode |

The validated package uses **profile 3 — COMBINED**. Invalid values safely fall back to profile 1.

## Runtime hotkeys

| Shortcut | Runtime action |
|---|---|
| **F6** | Manual filter action |
| **CTRL+F6** | Toggle Frame Generation ON / OFF |
| **F7** | Integrated benchmark (3 s warm-up + 10 s measurement in the current config) |
| **F8** | Show PTAR runtime status |
| **CTRL+F8** | Show/change FG quality profile |
| **F9** | Capture BMP screenshot |
| **CTRL+F9** | Start/stop integrated MP4 video recording |
| **F10** | Toggle PTAR presenter |
| **CTRL+F11** | Toggle PTAR HUD/overlay |
| **F12** | Select next filter |

`CTRL+F6` and `CTRL+F8` retain explicit hotkey-isolation fixes so their base-key actions do not leak into plain F6/F8 handling.

## QUALITY versus CONSERVATIVE

**QUALITY** remains the highest-fidelity FG profile. FUSEDDETAIL1 is mathematically bypassed at its `G=.35` gate. A historical field defect remains documented: motion-trail/ghost-like persistence can appear around the moving character with FG active.

**CONSERVATIVE / FUSEDDETAIL1** prioritizes temporal stability on difficult thin/repetitive structures and materially reduces localized shimmer/detail oscillation on the validated problem areas.

## Spatial reconstruction — PTAR-NG MoE

The spatial path remains **PTAR-NG MoE v01**, commonly used at:

`1280x720 -> 1920x1080`

for exact x1.5 reconstruction in both dimensions. Spatial reconstruction and frame generation remain separate subsystems.

## FUSEDDETAIL1

FUSEDDETAIL1 is active only on profile 3 CONSERVATIVE. It is a temporal-detail stabilizer, not a sharpening filter.

Design constraints retained from SAFEPOINT11:

- no new texture resource;
- no new UAV;
- no new resource binding;
- no new Dispatch;
- source-level `.Load()` count remains 7;
- REAL frames remain untouched;
- only a small ALU/min/max/lerp tail is added to GENERATED.

## Recorder and state-safety lineage

HUDREC1 retains the validated recorder work from SAFEPOINT11:

- one original B18K18/QSV recorder path;
- `CTRL+F9` recording with FG ON or OFF;
- recorder-local fullscreen VS/rasterizer state tied to the active recorder device;
- native immediate-context state save/restore around conversion;
- QSV encode/pipe scheduling at NORMAL;
- GPU readback worker at BELOW_NORMAL;
- safe finalization and evidence collection.

HUDREC1 changes only the FG-OFF source swapchain used for the recorder feed.

## Installation / validation

1. Close the game.
2. Run `01-INSTALL_GW16.bat`.
3. Run `02-VERIFY_INSTALL.bat` and require `VERIFY=PASS`.
4. Launch the game.
5. PTAR starts with FG OFF.
6. `CTRL+F6` toggles FG.
7. `CTRL+F8` selects the supported FG profile.
8. `F9` captures a BMP.
9. `CTRL+F9` starts/stops MP4 recording.
10. `diag\FG_MARKER_VISIBILITY.bat` controls only the blinking FG cadence marker.
11. `04-COLLECT_RESULTS.bat` collects diagnostics when needed.

`05-ROLLBACK_TEST.bat` provides controlled rollback. `06-DESINSTALLER_PTAR_COMPLET.bat` uses the ownership/SHA-aware safe uninstall path.

## Validation status

HUDREC1 V2 passed the package laboratory gates before field testing:

- recorder HUD gate: **38/38 PASS**;
- FG marker control gate: **17/17 PASS**;
- package gate: **87/87 PASS**;
- SHA ledger: **115/115 PASS**;
- ownership ledger: **116/116 PASS**;
- deterministic ZIP rebuild: PASS;
- ZIP CRC: PASS.

The final Windows 8.1 / GTX 960M field validation then confirmed the requested behaviour: recorded video contains the PTAR HUD/FPS.

## Repository layout

The maintained branches are intentionally limited to:

- **`main`** — current hardware-validated SAFEPOINT11/FUSEDDETAIL1/HUDREC1 delivery baseline, installer/rollback/recorder tooling, diagnostics and documentation;
- **`SOURCE`** — durable PTAR Project Master containing source, integration material, corpora, benchmarks, build tooling, validation and historical evidence.

Experimental Windowed/Borderless RC43-RC66 branches were abandoned and removed.

## Development policy

- preserve validated subsystems unless the active defect requires a change;
- promote only after the relevant laboratory and hardware gates;
- prefer non-destructive installation, rollback and uninstall behaviour;
- keep deterministic hashes and evidence with each validated implementation;
- perform all reproducible laboratory tests before requesting hardware tests;
- never treat an internal FPS counter as proof of visible output;
- preserve Windows 8.1 compatibility as an absolute constraint.

## License

PTAR is distributed under the **GNU General Public License v3.0**. See `LICENSE`.
