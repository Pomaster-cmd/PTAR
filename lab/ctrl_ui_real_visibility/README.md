# PTAR CTRL UI real-visibility lab

This isolated lab validates the D3D9 CTRL+F1 / CTRL+F5 diagnostic feedback path.

The production fix does not rely on a WinForms overlay being composited above an exclusive/fullscreen game. The x64 controller bridge writes `PTAR_D3D9_DIAG_UI.ini`; the x86 D3D9 runtime polls that file and rasterizes the notice into the game's own presenter backbuffer with the existing `Clear(rects)` HUD renderer.

Validation gates:

- rendering/FG/pacer/shader core unchanged from baseline `0066c7d4...`;
- x86 D3D9 runtime builds with Windows 8.1 subsystem target;
- existing GW16I HUD still produces visible GPU pixels;
- every CTRL+F5 and CTRL+F1 diagnostic state produces visible GPU pixels;
- bridge state expires and yields back to normal HUD feedback;
- x64 bridge writer compiles and its INI protocol self-test passes;
- x64 external WinForms controller remains fallback only.
