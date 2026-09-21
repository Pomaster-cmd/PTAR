PTAR X86/D3D9 SPATIAL1 — FIRST HARDWARE GATE
================================================

Purpose
-------
VTABLEFIX1 replaces the earlier truncated COM-vtable cloning with in-place\npatching of only the intercepted D3D9 slots. This preserves all private/internal\nvtable entries used by legacy D3D9 runtimes and engines such as Conviction.\n\nFirst PTAR compatibility runtime for 32-bit Direct3D 9 games.
Primary test target: Tom Clancy's Splinter Cell: Conviction.

This build validates the x86/D3D9 frontend and PTAR-NG MoE spatial path.
It DOES NOT yet include the x64 production frame-generation / recorder stack.

Expected Conviction executable:
  src\system\conviction_game.exe

Test geometry
-------------
For the first gate, configure Conviction to:
  Fullscreen
  1280x720
  MSAA/antialiasing disabled if the game exposes it

PTAR then creates a 1920x1080 physical backbuffer while keeping the game-facing
render target at 1280x720 and runs PTAR-NG MoE before Present.

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

Crash diagnostics
-----------------
This build writes PTAR_X86_D3D9.log from DLL_PROCESS_ATTACH onward, before the
real Direct3D 9 runtime or PTAR resources are initialized.

The log includes:
  - ordered STAGE markers,
  - Direct3DCreate9 / CreateDevice parameters and return values,
  - real backbuffer / texture / depth / shader / state-block creation results,
  - D3D9 object and device vtable hook stages,
  - serious Win32 exceptions observed by a vectored exception handler,
  - exception code/address/module/module offset,
  - x86 EIP/ESP/EBP/EAX/EBX/ECX/EDX/ESI/EDI/EFLAGS,
  - access-violation operation and target address when available.

After a General Protection Fault or other crash, run:
  03-COLLECT_CRASH_DIAGNOSTICS.bat

It creates PTAR_X86_D3D9_DIAG_YYYYMMDD_HHMMSS.zip with the PTAR log, install
state, executable/DLL hashes and PE type, GPU/OS information, recent Windows
Application Error / WER events, and recent matching Report.wer files when
accessible. Collection is read-only apart from creating its own diagnostic ZIP.

Evidence
--------
Success signature for the 720p -> 1080p path includes:
  PROXY_LOADED arch=x86 api=D3D9
  CREATEDEVICE_PTAR_TRY src=1280x720 out=1920x1080
  PTAR_ACTIVE src=1280x720 out=1920x1080 ...

If the game crashes, do not delete PTAR_X86_D3D9.log before running the
collector.
