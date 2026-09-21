PTAR X86/D3D9 FG1 GENERIC — LAB / FIELD TEST
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
      * 60-visible-Hz presentation target
      * late GENERATED frames are skipped instead of forcing bad cadence
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
  - late-generated skip counter
  - current cadence state

Frame-generation pacing
-----------------------
FG uses a generic 60-visible-Hz target.

For a healthy 60-Hz path, the intended steady-state cadence is approximately:
  30 REAL + 30 GENERATED = 60 visible frames/s.

The scheduler uses QueryPerformanceCounter and the previous REAL presentation
as the timing anchor. It targets the GENERATED frame at the midpoint and the
next REAL frame at the following half-rate slot.

If the source/FG work misses the midpoint by too much, PTAR skips that GENERATED
frame rather than presenting it late and creating severe G/R imbalance.

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
  - FG pacing late-skip events
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
  - generic package is produced.

Final product rule
------------------
After the x86/D3D9 FG backend is field-validated, the final deliverable must be
rebuilt from the production main baseline, preserving the validated x64/D3D11
runtime and adding the generic x86/D3D9 backend plus unified install/verify/
uninstall logic. It must not become a Splinter Cell-specific fork.
