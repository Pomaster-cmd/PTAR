PTAR-NG MoE v01 SF5 — AUTONOMOUS VALIDATION BUNDLE
====================================================

Purpose
-------
Prove that the validated PTAR shader/runtime path works without Visual Studio,
FXC, D3DCompileFromFile or the Windows SDK at runtime.

The three CSO files in /shaders are copied byte-for-byte from the real
v0.11.0 GTX 960M hardware validation result. The executable verifies their
SHA-256 hashes before creating D3D11 shaders.

One-time sequence BEFORE uninstalling the development tools
-------------------------------------------------------------
1. From the project root run RUN_PTAR_AUTO.bat.
2. MSVC is used one final time to build only the native host.
3. The host is compiled /MT and does NOT link d3dcompiler.lib.
4. dumpbin rejects D3DCompiler/VCRUNTIME/MSVCP/UCRTBASE imports.
5. RUN_AUTONOMOUS_ONLY.bat then clears the toolchain from PATH and performs
   the complete 42-case GPU parity + timing run from precompiled CSOs.

Only after that run is confirmed should the development tools be removed.

After toolchain removal
-----------------------
Run VERIFY_AFTER_TOOLCHAIN_REMOVAL.bat.

A valid result requires:
- shader SHA-256 PASS;
- 42/42 GPU parity PASS;
- max error <= 1 RGB8 LSB;
- 42 non-zero GPU PNG files;
- timing completed;
- runtime exit code 0;
- no crash/TDR.

This is an autonomous VALIDATION runtime. Game/proxy integration comes next.
