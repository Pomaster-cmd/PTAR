PTAR X86/D3D9 SPATIAL2 HUDCOMPARE1 — CONVICTION TEST
======================================================

Purpose
-------
This is the x86 / Direct3D 9 compatibility runtime for testing PTAR on legacy
32-bit games such as Tom Clancy's Splinter Cell: Conviction.

This revision keeps the validated VTABLEFIX2 compatibility fixes and adds a
native on-screen HUD plus an instant A/B comparison mode so the spatial
reconstruction can actually be judged in-game.

Current scope
-------------
Included:
  - x86 PE runtime (I386 / 0x014C)
  - Direct3D 9 proxy frontend
  - PTAR-NG MoE spatial reconstruction, x1.5 output
  - bilinear reference path at the same output resolution
  - native D3D9 HUD
  - FPS display
  - source/output resolution display
  - F6 instant A/B toggle: PTAR MoE <-> bilinear reference
  - F8 HUD show/hide
  - early crash diagnostics and collection tooling
  - VTABLEFIX2 for Conviction's multi-object D3D9 behavior

Not included yet:
  - production x64 frame generation stack
  - production recorder stack
  - production x64 HUD feature parity

Why the A/B mode matters
------------------------
Both modes use the same game render resolution and the same physical output
resolution. Pressing F6 changes only the reconstruction method:

  PTAR MOE
    PTAR-NG MoE spatial reconstruction.

  BILINEAR REF
    Plain bilinear upscale reference.

This makes the visual comparison meaningful: geometry, output resolution and
game state stay the same while the reconstruction algorithm changes.

HUD
---
The HUD is visible by default and shows:

  PTAR X86 D3D9 SPATIAL2
  MODE: PTAR MOE
      or
  MODE: BILINEAR REF

  SRC: 1280X720 > OUT: 1920X1080
  FPS: xx.x
  F6 A/B  F8 HUD
  FG: NOT PORTED

Hotkeys
-------
  F6  Toggle PTAR MoE / bilinear reference
  F8  Show/hide HUD

These hotkeys are polled non-destructively and do not consume the underlying
keyboard messages.

Expected Conviction executable
------------------------------
  src\system\conviction_game.exe

Recommended first test geometry
-------------------------------
For the first comparison:
  Fullscreen
  1280x720
  MSAA/antialiasing disabled if the game exposes it

PTAR then creates a 1920x1080 physical backbuffer while keeping the game-facing
render target at 1280x720.

Install
-------
You may extract the package either:
  - in a separate folder, or
  - directly next to conviction_game.exe.

Run:
  01-INSTALL_CONVICTION_PTAR_X86.bat

Or drag conviction_game.exe onto that BAT.

The installer derives its package directory internally and supports paths with
spaces/apostrophes and the in-place layout without copying d3d9.dll onto itself.

Safety
------
- x86 PE machine 0x014C is verified before install.
- Existing d3d9.dll is never silently destroyed.
- An existing d3d9.dll is backed up once as d3d9.dll.ptar_original.
- Uninstall removes PTAR only if the installed DLL hash still matches.
- If PTAR cannot create the x1.5 D3D9 presentation path, the proxy retries the
  original D3D9 device creation instead of intentionally preventing launch.
- main is not modified by this compatibility work; this remains in SOURCE.

VTABLEFIX2
----------
The original prototype cloned only the documented public COM vtable length.
Conviction/D3D9 used additional internal entries, causing an EIP=0 crash.

VTABLEFIX1 changed the design to patch only required slots in the original
system vtable, preserving all other entries.

VTABLEFIX2 additionally preserves the captured real CreateDevice pointer when
later IDirect3D9 objects see that shared vtable already patched. This fixes the
second field-observed EIP=0 crash at the first CreateDevice call.

Crash diagnostics
-----------------
This build writes PTAR_X86_D3D9.log from DLL_PROCESS_ATTACH onward, before the
real Direct3D 9 runtime or PTAR resources are initialized.

The log includes:
  - ordered STAGE markers
  - Direct3DCreate9 / CreateDevice parameters and return values
  - backbuffer / texture / depth / shader / state-block creation results
  - D3D9 object and device vtable hook stages
  - serious Win32 exceptions observed by a vectored exception handler
  - exception code/address/module/module offset
  - x86 registers
  - access-violation operation and target address when available
  - HUD A/B mode changes

After a General Protection Fault or other crash, run:
  03-COLLECT_CRASH_DIAGNOSTICS.bat

It creates:
  PTAR_X86_D3D9_DIAG_YYYYMMDD_HHMMSS.zip

The ZIP contains the PTAR log, install state, executable/DLL hashes and PE type,
GPU/OS information, recent Windows Application Error / WER events, and recent
matching Report.wer files when accessible.

Success evidence
----------------
A successful active path should include log entries similar to:

  PROXY_LOADED arch=x86 api=D3D9
  CREATEDEVICE_PTAR_TRY src=1280x720 out=1920x1080
  PTAR_ACTIVE src=1280x720 out=1920x1080 ... hud=ON compare=F6

If the HUD appears, the runtime is definitively inside the PTAR presentation
path. Use F6 to compare PTAR MoE against the bilinear reference immediately.
