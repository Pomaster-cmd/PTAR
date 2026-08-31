# PTAR v0.12.3 — Windows 8.1 autonomous execution audit

## v0.12.2 failure

The autonomous C++ host compiled successfully on the real Windows 8.1 system
and its import audit passed. The EXE was then not found by the launcher.

Cause: v0.12.2 canonicalized `BUNDLE` without a trailing separator but several
child paths still used `%BUNDLE%bin`, `%BUNDLE%shaders` and `%BUNDLE%runs`.

v0.12.3 uses one invariant everywhere:
- canonical ROOT/BUNDLE variables have no trailing separator;
- every child path adds `\` explicitly.

## C++ source

The autonomous C++ source is byte-for-byte unchanged from v0.12.2. That exact
source already compiled and linked successfully with VS2019 Build Tools on the
real Windows 8.1 x64 machine.

## Windows 8.1 API surface

The autonomous source is restricted to:
- base D3D11;
- DXGI 1.1 / CreateDXGIFactory1;
- WIC;
- COM;
- BCrypt/CNG SHA-256;
- GetTickCount64;
- normal Win32 file/string APIs.

Forbidden/newer families are absent:
- D3D12;
- D3D11.1+ headers;
- DXGI 1.2+ headers;
- CreateDXGIFactory2;
- D3DCompileFromFile / runtime D3DCompiler.

One-time host build:
- WINVER=0x0603;
- _WIN32_WINNT=0x0603;
- /SUBSYSTEM:CONSOLE,6.03;
- /MT;
- base libraries d3d11/dxgi/windowscodecs/ole32/bcrypt.

Import audit rejects:
D3DCOMPILER, VCRUNTIME, MSVCP, UCRTBASE and API-MS-WIN-CRT.

## New execution gates

Preflight:
- host EXE exists;
- 3 CSOs exist and are non-empty;
- 42 LR inputs;
- 42 expected outputs.

Postflight:
- exit 0;
- hardware_summary/parity/timing/timing_pairs present;
- 42 parity cases, 0 failed, max 1 LSB;
- 512 paired samples;
- 42 non-zero GPU PNG files;
- precompiled DXBC mode;
- Visual Studio/SDK/D3DCompiler runtime requirement = NO.

The compiler `.obj` is now emitted inside the unique build stage and no longer
pollutes the project root.

No uninstall or deletion is performed by v0.12.3.
