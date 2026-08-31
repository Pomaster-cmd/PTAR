# PTAR v0.10.3 — D3DCompiler hardware path

## Real Windows 8.1 result that motivated this change

v0.10.2 successfully:
- installed Visual Studio 2019 C++ Build Tools;
- imported `vcvars64.bat`;
- downloaded and ran the official Windows 8.1 SDK setup.

The SDK setup returned success but `fxc.exe` was still not exposed.

## Decision

FXC is no longer a hard prerequisite for the PTAR hardware gate.

Microsoft supports HLSL compilation through `D3DCompileFromFile`. The native
hardware validator now compiles:
- fullscreen VS as `vs_5_0`;
- K185 PS as `ps_5_0`;
- bilinear control PS as `ps_5_0`.

Compile flags:
- strictness;
- warnings as errors;
- optimization level 3.

The K185 bytecode is then disassembled with `D3DDisassemble`, saved to disk,
and audited before the shaders are created.

Required audit:
- exactly one `gather4`;
- exactly four `sample_l`;
- zero UAV declarations/loads/stores.

The compiled `.cso` files and K185 `.asm` are preserved in the unique hardware
run directory.

This is still Direct3D 11 / Shader Model 5.0 and does not change the K185
algorithm.

FXC can still be detected and logged if present, but it is optional.
