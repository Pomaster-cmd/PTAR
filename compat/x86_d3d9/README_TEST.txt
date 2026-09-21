PTAR X86/D3D9 PRODPORT1 GENERIC — LAB / FIELD TEST
=============================================

Purpose
-------
Generic PTAR backend for 32-bit Direct3D 9 applications.

This package is NOT tied to a specific game. It accepts any x86 executable
intended to use Direct3D 9. Game names belong only to validation evidence or,
when unavoidable, isolated compatibility profiles.

Current backend
---------------
Architecture:
  x86 / PE machine 0x014C

Graphics API:
  Direct3D 9

Included:
  - PTAR-NG MoE spatial reconstruction
  - bilinear A/B reference at the same output resolution
  - native D3D9 HUD
  - source/output resolution display
  - measured REAL and visible FPS
  - F6 spatial A/B toggle
  - CTRL+F6 frame-generation toggle
  - F8 HUD toggle
  - generic GPU frame generation:
      * previous/current reconstructed REAL history
      * coarse motion estimation at /4
      * refinement at /2
      * midpoint interpolation
      * production-derived fail-soft confidence/clamp rules adapted to D3D9 shader ME
      * 60-visible-Hz presentation target
      * local GENERATED -> REAL pacing grid
      * stall resynchronisation without historical late-skip cascades
  - VTABLEFIX2 generic D3D9 COM compatibility model
  - early crash diagnostics and collector
  - generic install / uninstall workflow

Not included in this x86/D3D9 backend yet:
  - recorder parity with the production x64/D3D11 backend
  - every quality/profile feature of the production x64/D3D11 runtime
  - x86/D3D11 backend
  - x64/D3D9 backend

Hotkeys
-------
  F6        PTAR MoE <-> bilinear reference
  CTRL+F6   Frame generation ON/OFF
  F8        HUD ON/OFF

HUD
---
The HUD displays:
  - PTAR backend identity
  - spatial mode
  - source and output resolution
  - REAL FPS
  - measured visible FPS
  - FG ON/OFF
  - motion-estimation tier
  - REAL / GENERATED counters
  - late-generated skip counter (kept for continuity; expected to remain 0 in PRODPORT1)
  - current cadence state

Frame-generation pacing
-----------------------
FG uses a generic 60-visible-Hz target.

For a healthy 60-Hz path, the intended steady-state cadence is approximately:
  30 REAL + 30 GENERATED = 60 visible frames/s.

PRODPORT1 uses QueryPerformanceCounter but no longer compares a generated frame
that is already ready against a historical midpoint which may have elapsed while
CURRENT reconstruction and motion estimation were running.

Once a GENERATED frame is ready, the pacer anchors or resumes a local visible
grid, presents GENERATED, then places REAL on the following visible slot. The
next GENERATED slot is one visible period after REAL.

If the source, driver or game stalls long enough that the local grid becomes
stale, PTAR performs a one-step resynchronisation to the current time. It does
not replay missed deadlines and does not create a cascade of late-generated
skips.

The interpolation policy also differs intentionally from the production
D3D11/NVENC-ME backend: D3D9 currently estimates motion in shaders, so low
motion confidence is allowed to reject a vector completely. Production-derived
fail-soft clamping is retained, but the NVENC-specific minimum trust floor is
not copied blindly.

Spatial A/B comparison
----------------------
F6 keeps the same source and output geometry and changes only the reconstruction:

  PTAR MOE
    PTAR-NG MoE spatial reconstruction.

  BILINEAR REF
    Plain bilinear reference.

This allows direct visual comparison without changing the scene or output size.

Install
-------
Run:
  01-INSTALL_PTAR_X86_D3D9.bat

Then drag the target x86 executable onto the BAT, provide it as an argument, or
enter its full path when prompted.

Example:
  01-INSTALL_PTAR_X86_D3D9.bat "C:\Games\Example\game.exe"

The installer:
  - verifies PE machine 0x014C;
  - never silently destroys an existing d3d9.dll;
  - creates a backup when appropriate;
  - supports packages extracted directly beside the target EXE;
  - writes PTAR_X86_D3D9_INSTALL_STATE.txt.

Uninstall
---------
Run:
  02-UNINSTALL_PTAR_X86_D3D9.bat

The uninstaller removes PTAR only when the installed DLL still matches the hash
recorded by the installer and restores an owned backup when present.

Diagnostics
-----------
The runtime writes:
  PTAR_X86_D3D9.log

Logging starts at DLL_PROCESS_ATTACH and includes:
  - Direct3DCreate9 / CreateDevice stages
  - PTAR resource creation
  - vtable patching
  - spatial / FG passes
  - GENERATED / REAL presentation stages
  - FG pacing resynchronisation events and continuity counters
  - hotkey mode changes
  - serious Win32 exceptions and x86 registers

After a crash run:
  03-COLLECT_CRASH_DIAGNOSTICS.bat

The collector is target-agnostic. It uses the supplied EXE or the local install
state and packages PTAR logs, hashes, PE type, GPU/OS information and recent
matching Windows/WER evidence.

Safety / compatibility
----------------------
- Windows 8.1 compatible target subsystem.
- No dynamic VCRUNTIME/MSVCP/UCRT dependency.
- Generic runtime logic contains no game-name branch.
- D3D9 vtables are patched in place only at required slots.
- Existing main x64/D3D11 production runtime is not modified during this work.
- Final product integration must start from the current production main and add
  this backend through a generic multi-backend dispatcher.

Field-validation evidence
-------------------------
Tom Clancy's Splinter Cell: Conviction has already validated the generic
x86/D3D9 frontend, VTABLEFIX2, spatial path, HUD and A/B controls on real
hardware.

That game is a validation target only. It is not an activation condition and
does not define the product architecture.

Current FG gate
---------------
Before any new hardware request, CI must pass:
  - both spatial shaders compile;
  - all three FG shaders compile;
  - x86 proxy builds with Win8.1 subsystem;
  - PE/export/dependency checks pass;
  - multi-object Direct3DCreate9/CreateDevice regression passes;
  - D3D9 FG GPU pipeline produces non-empty generated output and non-zero motion;
  - generic separate-folder install passes;
  - generic in-place install passes;
  - diagnostic collector passes;
  - PRODPORT1 pacing test verifies GENERATED -> REAL slot ordering;
  - a simulated long stall is accepted through local-grid resynchronisation;
  - historical LATE_SKIP remains zero;
  - generic package is produced.

Final product rule
------------------
After the x86/D3D9 FG backend is field-validated, the final deliverable must be
rebuilt from the production main baseline, preserving the validated x64/D3D11
runtime and adding the generic x86/D3D9 backend plus unified install/verify/
uninstall logic. It must not become a Splinter Cell-specific fork.
