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
  - wall-clock REAL and visible FPS (QPC rolling window; hitches lower the displayed value)
  - F6 ManualFilter
  - CTRL+F6 frame-generation toggle
  - F7 Benchmark shortcut contract
  - F8 Status
  - CTRL+F8 FG quality/profile control
  - F9 Capture shortcut contract
  - CTRL+F9 VideoRecord shortcut contract
  - F10 TogglePresenter shortcut contract
  - CTRL+F11 HUD toggle
  - F12 FilterNext
  - generic GPU frame generation using the production GW16I architecture:
      * Direct3DCreate9 compatibility surface backed by a D3D9Ex source device
      * legacy game still receives the base IDirect3D9 / IDirect3DDevice9 API
      * isolated D3D9Ex presenter device and thread
      * GPU-shared REAL ring between source and presenter
      * CPU readback/upload bridge retained only as a fallback transport
      * previous/current REAL history owned by the isolated presenter
      * coarse motion estimation at /4
      * refinement at /2
      * midpoint interpolation
      * GENERATED work completely outside the game Present critical path
      * explicit GENERATED -> REAL visible ordering on the isolated presenter
      * Sync1 physical presentation isolated from source rendering
      * load-shed drops GENERATED work before REAL when the presenter falls behind
      * GW16I source governor: FG ON up to 30 REAL for ~30+30 visible; FG OFF up to 60 REAL
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
  F6        ManualFilter (PTAR MoE <-> bilinear reference)
  CTRL+F6   Frame generation ON/OFF
  F7        Benchmark
  F8        Status (temporary runtime notice, independent from permanent HUD)
  CTRL+F8   FG quality/profile control
  F9        Capture (post-overlay BMP from final D3D9 presenter backbuffer)
  CTRL+F9   VideoRecord
  F10       TogglePresenter (isolated presenter ON/OFF)
  CTRL+F11  HUD ON/OFF
  F12       FilterNext

HUD / status / capture
----------------------
Plain F8 follows the production Status contract: it shows a temporary runtime
status notice without changing the permanent HUD state. Therefore F8 remains
visible even when CTRL+F11 has hidden the permanent HUD.

Plain F9 follows the production Capture contract: the request is consumed on
the presenter path after PTAR/HUD/status rendering and before Present. The file
is written beside the injected runtime as win81_nis_capture_N.bmp.

The FPS fields no longer use an EMA of instantaneous 1/delta values. REAL and
VISIBLE/DISPLAY rates use a recent wall-clock QPC window so pacing gaps and
hitches reduce the reported rate. The runtime also writes one
FPS_WALLCLOCK_SAMPLE line per second for log-side verification.

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

Frame-generation / presenter architecture
-----------------------------------------
The x86/D3D9 path now follows the same architectural rules as the supplied
production D3D11/GW16I model instead of presenting GENERATED and REAL frames
synchronously from the game's Present hook.

The game/source device reconstructs the current REAL frame, performs a short
handoff, and returns. Physical display Present, REAL-history ownership, motion
estimation, interpolation, HUD composition and GENERATED/REAL ordering all live
on a separate D3D9Ex presenter device/thread.

This separation matters because D3D9 Present can occupy the calling runtime for
roughly a VBlank even when called from another thread on the same device. The
isolated presenter is therefore allowed to block at Sync1 without reducing the
game's REAL source throughput.

D3D9-specific transport:
  - Direct3DCreate9 now prefers a Direct3DCreate9Ex factory while returning its
    legacy IDirect3D9 base interface to the application;
  - the resulting source device is exposed to the game as IDirect3DDevice9 but
    is internally share-capable through IDirect3DDevice9Ex;
  - completed REAL output is copied GPU-to-GPU into a shared ring and opened by
    the isolated presenter through shared D3D9Ex handles;
  - producer completion uses EVENT queries; waiting happens on the isolated
    presenter thread, never in the game's Present path;
  - CI currently measures the GPU-shared handoff around 0.06-0.10 ms in the
    integrated path, versus the older CPU bridge's substantially higher cost;
  - the preallocated CPU readback/upload ring remains a fallback if an Ex source
    device cannot be obtained.

Legacy D3DPOOL_MANAGED compatibility:
  - D3D9Ex rejects D3DPOOL_MANAGED directly;
  - PTAR translates managed textures, cube textures and volume textures to
    native DEFAULT/DYNAMIC D3D9Ex resources;
  - managed vertex/index buffers are translated to DEFAULT/DYNAMIC with a
    DEFAULT compatibility retry;
  - Lock/Unlock, binding, multiple mip levels, AUTOGEN level-0 writes, base
    Reset and post-Reset content/lifetime behavior are laboratory-gated;
  - resource GetDesc/GetLevelDesc is virtualized back to the original
    D3DPOOL_MANAGED pool and original Usage flags;
  - EvictManagedResources keeps the legacy S_OK contract for translated
    resources.

Swapchain ownership:
  - the source device uses a private hidden window and an IMMEDIATE producer
    swapchain;
  - only the isolated presenter owns the game's visible HWND;
  - this separation eliminated the transient source Clear(E_FAIL) race observed
    when both D3D9Ex devices targeted the same visible HWND;
  - the complete create/present/reset/recreate lifecycle is repeated three times
    in CI to catch this race rather than accepting a one-off successful run.

Production-derived cadence policy:
  FG OFF:
    presenter remains armed, REAL_ONLY, source target up to 60 REAL/s.

  FG ON:
    HIGH / target60 semantics, source target up to 30 REAL/s, midpoint
    GENERATED frames presented before their matching REAL frames.

  Under presenter pressure:
    GENERATED work is load-shed before REAL. Historical deadlines are not
    replayed and no VBlank wait is inserted into the game Present hook.

The interpolation math remains the D3D9 shader-ME adaptation. NVENC-specific
motion-vector assumptions from the x64/D3D11 backend are not copied where the
underlying motion source differs.

Resolution independence / native 1:1
---------------------------------------
The D3D9 backend keeps PTAR attached across D3D9 Reset/resolution changes.

With the default output target of 1920x1080:
  - 1280x720 uses the exact x1.5 PTAR reconstruction;
  - other game resolutions below the configured output use the universal PTAR
    reconstruction with aspect-fit geometry;
  - 1920x1080 uses native 1:1 while HUD/presenter/FG remain active;
  - resolutions at or above the output that are not valid spatial inputs fail
    soft to native 1:1 rather than disabling PTAR.

Examples validated by the resolution-policy/GPU gates include 1280x720,
1600x900, 1024x576 and 4:3 aspect-fit input. Returning to another supported
resolution on Reset rebuilds the spatial and presenter resources without
requiring reinjection.

Windowed modes with zero requested BackBuffer dimensions are resolved from the
actual created backbuffer instead of disabling the backend.

Spatial A/B comparison
----------------------
When the configured x1.5 spatial path is active, F6 keeps the same source and
output geometry and changes only the reconstruction:

  PTAR MOE
    PTAR-NG MoE spatial reconstruction.

  BILINEAR REF
    Plain bilinear reference.

This allows direct visual comparison without changing the scene or output size.
In native 1:1 mode the spatial stage is bypassed; F6 does not disable the D3D9
backend and FG remains independently available.

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
  - spatial passes and isolated REAL handoff
  - isolated presenter startup/shutdown and bridge timing
  - GENERATED / REAL presentation stages
  - mailbox/load-shed counters and visible wall-clock cadence
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
  - resolution-policy regression validates 720p->1080p spatial and native
    1080p FG-only operation;
  - live D3D9 Reset sequence preserves PTAR across native/spatial/native
    resolution transitions;
  - D3D9 FG GPU pipeline produces non-empty generated output and non-zero motion;
  - generic separate-folder install passes;
  - generic in-place install passes;
  - diagnostic collector passes;
  - same-device Present blocking is explicitly rejected by laboratory probes;
  - separate-device presenter isolation proves source throughput is preserved;
  - D3D9Ex source compatibility probes validate legacy MANAGED resource
    translation, descriptor fidelity, Reset persistence and native binding;
  - GPU-shared source-to-presenter transport is required by the integrated
    runtime gate, with CPU readback retained only as fallback;
  - the isolated proxy create/present/reset lifecycle is repeated three times
    with no source Clear or state-restore failure;
  - CPU fallback readback remains benchmarked at 720p, 900p and 1080p;
  - source-side legacy pacer helpers contain no waits/resync/late-skip path;
  - wall-clock FPS regression proves a synthetic 200 ms hitch lowers the rate;
  - F9 capture smoke creates and validates a 320x180 post-overlay BMP;
  - generic package is produced.

Final product rule
------------------
After the x86/D3D9 FG backend is field-validated, the final deliverable must be
rebuilt from the production main baseline, preserving the validated x64/D3D11
runtime and adding the generic x86/D3D9 backend plus unified install/verify/
uninstall logic. It must not become a Splinter Cell-specific fork.
