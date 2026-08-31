# PTAR v0.12.0 — Toolchain removal gate

Do **not** uninstall MSVC / Windows 8.1 SDK until the autonomous bundle has
successfully completed on the real GTX 960M.

v0.12.0 changes the runtime dependency model:

- shaders are precompiled DXBC copied from the validated v0.11.0 run;
- runtime does not call D3DCompileFromFile;
- runtime does not link D3DCompiler;
- host is built `/MT`;
- runtime imports are audited once with dumpbin;
- shader files are SHA-256 checked by the executable through Windows BCrypt;
- runtime-only test launches with a sanitized PATH containing no cl.exe/fxc.exe.

No uninstall/delete command is included in v0.12.0. This is intentional.
The removal tool will only be produced after this autonomous gate passes.
