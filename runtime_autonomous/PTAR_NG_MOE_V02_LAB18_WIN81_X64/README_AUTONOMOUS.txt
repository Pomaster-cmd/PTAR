PTAR-NG MoE v02 LAB18 - AUTONOMOUS WINDOWS 8.1 X64 BENCH
===========================================================

Purpose
-------
This bundle is a targeted hardware-validation bench for the frozen PTAR-NG
MoE v02 LAB18 spatial reconstruction candidate. It compares three precompiled
D3D11 Shader Model 5.0 pixel shaders on the same input/output resources:

  1. PTAR-NG MoE v02 LAB18
  2. PTAR-NG MoE v01 SF5
  3. EDGE-NG v03 K185 control

The historical v01 autonomous bundle is not modified by this experiment.
This bundle is additive and opt-in.

Runtime contract
----------------
- Windows 8.1 x64 target
- Direct3D 11 Feature Level 11_0
- exact x1.5 spatial reconstruction
- precompiled DXBC only at runtime
- no D3DCompiler runtime dependency
- no Visual Studio runtime requirement
- no Windows SDK runtime requirement
- no NIS
- no frame-generation or NVENC logic in this bench

LAB18 shader texture footprint
------------------------------
The frozen candidate is compiled and audited for:
- exactly 1 GatherGreen
- exactly 4 SampleLevel/sample operations
- 0 UAV
- no intermediate texture

Hardware run
------------
Run:

  RUN_AUTONOMOUS_ONLY.bat

The launcher creates a new results directory and executes a balanced,
rotating-order triple-shader benchmark at 1280x720 -> 1920x1080.
The host refuses to continue if any packaged shader SHA-256 does not match the
hashes embedded at build time.

Expected outputs
----------------
- runtime_summary.txt
- timing.csv
- timing_pairs.csv

The bench also performs one render/readback smoke hash for each shader before
timing begins. A timing result is not accepted if D3D11 reports a device error
or a bounded timer query cannot resolve.

Interpretation
--------------
Only physical-GPU results may be used for GTX 960M performance claims.
The GitHub Actions WARP execution is a software/runtime smoke gate only and is
not a performance measurement.
