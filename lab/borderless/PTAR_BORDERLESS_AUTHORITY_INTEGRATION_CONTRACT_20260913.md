# PTAR Borderless Authority — integration contract (LAB ONLY)

Status: validated architecture contract for later integration into the exact user-designated PTAR v2.0 baseline. This file is not a product release, not a runtime patch and not authorization to modify protected refs.

## 1. Non-negotiable invariant

When PTAR renders a game backbuffer at `RenderWidth x RenderHeight` and presents at a larger native output:

- game HWND client == game swapchain/backbuffer dimensions;
- native presenter HWND client == selected monitor/output dimensions;
- resizing or moving the presenter MUST NOT resize the game HWND or game backbuffer;
- the game must never observe a native-sized client merely because PTAR presents natively.

This is the authority boundary that prevents the observed pre-upscale top-left crop.

## 2. Responsibility split

### BorderlessAuthority
Owns only game-window authority and takeover state.

Responsibilities:
- activates only after preflight succeeds;
- snapshots original game style/exstyle/rect/owner-affecting state needed for rollback;
- enforces `WS_POPUP` logical-render geometry on the game HWND while takeover is active;
- clamps hostile `WM_WINDOWPOSCHANGING` / `WM_STYLECHANGING` attempts to the authoritative logical geometry;
- suppresses DXGI-owned Alt+Enter/window mutations through `DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER`;
- restores the exact pre-takeover window state on disable/failure/process shutdown.

Forbidden responsibilities:
- shader selection;
- spatial reconstruction;
- frame generation;
- pacing/VBlank policy;
- recorder/QSV/NVENC ownership.

### PresenterHost
Owns creation/lifecycle of the native presenter HWND.

Required contract:
- HWND is created on the SAME Win32 UI thread that owns the game HWND;
- D3D device/swapchain work may execute on PTAR's rendering worker;
- presenter is an owned popup of the game HWND;
- style: `WS_POPUP`;
- exstyle: `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`;
- MUST NOT use `WS_EX_TRANSPARENT`;
- MUST NOT use permanent `WS_EX_TOPMOST`;
- `WM_MOUSEACTIVATE` returns `MA_NOACTIVATE`, but a genuine presenter click may request focus back to the game HWND;
- minimize of the game hides the presenter; restore re-shows with `SW_SHOWNOACTIVATE`;
- teardown always destroys/relinquishes the presenter before releasing authority state.

### GeometryMapper
Pure deterministic mapping; no Win32 side effects.

Responsibilities:
- render/logical <-> physical/native point transform;
- aspect-fit/letterbox transform;
- clip rectangle transform;
- negative monitor origins;
- <= 1 logical-pixel round-trip error in tested quantized mappings.

The mapping implementation is independently stress-tested and must remain unit-testable without HWNDs or D3D.

### InputVirtualizer
Bounded USER32 compatibility layer. It MUST NOT rewrite raw relative input.

Presenter-routed messages:
- `WM_MOUSEMOVE`;
- left/right/middle/X button down/up/double-click;
- cursor policy via `WM_SETCURSOR` forwarding;
- `WM_MOUSELEAVE` forwarding after presenter-side tracking.

Game-WndProc-side messages:
- `WM_MOUSEWHEEL` / `WM_MOUSEHWHEEL` are remapped in the focused game path because Win32 delivers wheel messages to the focus window and lParam is screen-space.

Virtualized API semantics when takeover is active:
- absolute cursor position: logical game view <-> physical presenter view;
- `ClipCursor` / `GetClipCursor`: logical rectangle <-> physical rectangle;
- logical `SetCapture(gameHwnd)` -> physical presenter capture while game-facing `GetCapture()` still reports the game HWND;
- `ReleaseCapture` restores both physical and logical capture state;
- `TrackMouseEvent(gameHwnd, ...)` -> presenter HWND; query result is re-logicalized to game HWND;
- routed `GetMessagePos()` is scoped to the synchronous logical game callback so native presenter coordinates are not leaked.

Explicitly untouched:
- `WM_INPUT` payloads;
- DirectInput relative motion;
- raw mouse deltas;
- keyboard semantics.

### DpiGuard
Windows 8.1-specific preflight.

Rules:
- never change the game's process DPI awareness after startup;
- allow same-process native presenter takeover only when physical/native coordinate mapping is safe for the process awareness + target monitor scale;
- if not safe, fail-open BEFORE window takeover and leave the legacy presentation path untouched.

## 3. Activation transaction

Takeover is transactional. Product integration must follow this order:

1. resolve game HWND + owning thread;
2. resolve exact current game swapchain/backbuffer dimensions;
3. resolve target monitor physical rect;
4. run DPI/input/geometry/DXGI preflight;
5. snapshot rollback state;
6. install narrowly scoped game WndProc authority guard;
7. marshal presenter HWND creation onto the game's UI thread;
8. create/attach native presenter swapchain without resizing the game swapchain;
9. install bounded input virtualization;
10. enable presenter as authoritative PTAR output;
11. commit takeover state only after all previous steps succeeded.

Any failure before step 11 restores the snapshot and returns to the previous PTAR behavior.

## 4. Runtime fail-open conditions

Immediately abandon takeover and restore the old path if any required invariant cannot be maintained, including:

- game client no longer equals authoritative render/backbuffer size after a guarded update;
- presenter owner/thread contract is lost;
- presenter swapchain no longer equals selected native output;
- required input virtualization state cannot be established;
- DPI mapping becomes unsafe after monitor migration;
- device/swapchain recreation cannot rebuild both logical and native sides coherently.

No fail-open path may silently expand the game HWND to native output size.

## 5. Product integration constraints

When the exact current PTAR v2.0 baseline is supplied/designated:

- clone/copy that baseline intact first;
- preserve all unrelated files byte-for-byte wherever technically possible;
- keep the borderless authority layer modular rather than inserting a monolithic patch into FG/spatial code;
- preserve existing recorder, FG, reconstruction, diagnostics, installer, rollback and uninstall behavior;
- produce a deterministic changed-file/delta manifest;
- fail packaging when an unrelated file differs unexpectedly;
- never update repository `main` or `SOURCE` without explicit user approval.

## 6. Required gates before a field candidate

All must pass on the integrated v2.0 candidate:

- geometry stress;
- input transform stress;
- pointer event/focus parity;
- USER32 capture/tracking/GetMessagePos virtualization;
- same-thread HWND / worker-D3D affinity;
- Windows 8.1 DPI safety policy;
- lifecycle/minimize/focus/z-order stress;
- hostile game-window takeover stress;
- integrated two-HWND D3D takeover harness;
- WARP dual-swapchain stress;
- occlusion/focus/capture stress;
- existing PTAR v2.0 regression suite;
- packaging hashes/manifests/rollback/uninstall validation.

Only after these gates does a Windows 8.1 + real NVIDIA + Warhammer field test become justified.
