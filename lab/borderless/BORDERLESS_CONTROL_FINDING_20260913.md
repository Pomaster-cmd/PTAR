# PTAR borderless control research finding — 2026-09-13

Status: LAB-ONLY architecture finding. NOT a product release and NOT a product baseline.

## Field defect isolated

The failed 1280x720 -> 1920x1080 path violated a fundamental geometry invariant:

- real game swapchain/backbuffer: 1280x720;
- logical game presentation: 1280x720;
- separate PTAR presenter: 1920x1080;
- but the game HWND physical input client was silently restored to 1920x1080.

That lets a game derive 1920x1080 viewport/UI/input geometry while writing into a 1280x720 render target. The result is a top-left crop before PTAR reconstruction. Upscaling cannot recover content never rendered into the low-resolution backbuffer.

The old compromise was coupled to a transparent/click-through native presenter: because the presenter did not own normal pointer interaction, the underlying game HWND had to cover the native screen. That geometry is incompatible with a low-resolution authoritative game client for engines that derive layout from HWND client size.

## Viable architecture

Use two HWNDs with strict separation of responsibilities.

### Game/render HWND

- remains the logical render, keyboard, focus and raw-input owner;
- windowed DXGI swapchain;
- borderless `WS_POPUP` while takeover is active;
- client dimensions MUST always equal real game swapchain/backbuffer dimensions;
- positioned at the target monitor origin so logical screen coordinates stay coherent;
- never silently expanded to native output dimensions;
- game attempts to change style/geometry while takeover is active are clamped in the game WndProc (`WM_WINDOWPOSCHANGING`, `WM_STYLECHANGING`) rather than by broad global API hooks.

### Native presenter HWND

- separate windowed DXGI swapchain at physical/native output dimensions;
- `WS_POPUP`;
- `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`;
- owned by the game HWND;
- deliberately NOT `WS_EX_TRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- receives normal pointer interaction and routes it into the game's logical coordinate space;
- resizing/moving this window or its swapchain MUST NOT resize the game HWND or game backbuffer.

### DXGI policy

- no exclusive-fullscreen takeover is required;
- both swapchains remain windowed;
- `IDXGIFactory::MakeWindowAssociation(gameHwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER)` prevents DXGI from independently mutating the game window or handling Alt+Enter;
- PTAR owns the native presentation path;
- output/presenter Present semantics are authoritative for the proxy path; underlying render-HWND occlusion must never be allowed to stop the game loop.

### Input virtualization

- keyboard and foreground raw input remain on the game HWND;
- presenter forwards normal absolute pointer messages after ASPECT-FIT physical->logical mapping;
- wheel/hwheel screen coordinates are translated into the virtual logical screen space;
- absolute cursor APIs (`GetCursorPos`, `SetCursorPos`) are virtualized for the game when needed;
- `ClipCursor` / `GetClipCursor` rectangles are transformed between logical and physical spaces;
- game logical mouse capture is represented physically by capture on the presenter while game-facing capture ownership remains logically the game HWND;
- relative/raw input is not spatially remapped.

## Laboratory validation

All current harnesses are compiled x64 with `_WIN32_WINNT=0x0603`, `WINVER=0x0603`, static CRT and PE subsystem 6.03.

Latest successful Windows/WARP gate:

- geometry stress: 1,500,000 point cases + 1,500,000 clip cases PASS;
- mixed aspect ratios and negative monitor origins covered;
- maximum logical point round-trip error: 1 pixel;
- maximum clip-rectangle round-trip error: 1 pixel;
- input virtualization stress: 2,000,000 each of cursor/warp/wheel/clip/capture cases PASS;
- real Win32 takeover stress: 20,000 hostile game window requests PASS;
- 4,000 output-only presenter geometry changes caused zero invariant failures;
- 1,177 hostile style changes clamped successfully;
- 21 minimize/restore cycles PASS;
- D3D11 WARP dual-swapchain stress: 5,988 game resizes + 6,012 presenter resizes + 12,000 presenter Present(TEST) operations PASS;
- output-only changes never changed the game backbuffer;
- occlusion/focus/capture stress: 2,000 Present cycles PASS;
- 154 no-activate/focus checks PASS;
- 118 physical-presenter capture checks PASS;
- 9 minimize/restore cycles PASS.

The lab runner returned 2,000/2,000 S_OK for both render and presenter swapchains in the tested composed session. Production code must nevertheless treat `DXGI_STATUS_OCCLUDED` as a valid possibility because DXGI documents it as a possible windowed Present status.

## Integration gate

Do not package this research directly over repository `main`.

Before any user-facing candidate:

1. resolve the exact newest PTAR v2.0 package/runtime that the user designates as current;
2. copy that package intact as the product baseline;
3. integrate only the borderless authority/input layer required by this finding;
4. preserve all unrelated v2.0 features, recorder, FG, spatial reconstruction, diagnostics, collection, rollback and safe uninstall paths;
5. add a deterministic delta manifest and fail packaging if unrelated v2.0 files differ;
6. rerun the full borderless lab gates plus the v2.0 regression suite;
7. only then produce a field candidate.

No additional user hardware test is justified before those gates are complete.
