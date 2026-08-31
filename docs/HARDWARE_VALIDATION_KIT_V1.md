# PTAR EDGE-NG v03 K185 — Hardware Validation Kit v1

Target: Windows 8.1 + NVIDIA GTX 960M.

## One-command validation

Open a Visual Studio Developer Command Prompt that also exposes `fxc.exe`, then run:

`build\windows\BUILD_AND_RUN_K185_HARDWARE_VALIDATION.bat`

The script is non-destructive: it creates a fresh random output directory and contains no delete step.

## What it verifies

1. FXC compiles VS/PS as Shader Model 5.0 with `/O3 /Ges /WX`.
2. DXBC audit confirms the K185 pixel shader path: 1 gather4, 4 sample_l, no UAV.
3. Native C++ validator selects an NVIDIA adapter (`VendorId 0x10DE`) and requires D3D feature level 11.0.
4. WIC loads all 42 Protocol-B LR PNGs and the 42 float32 semantic references.
5. GPU renders K185 to `R8G8B8A8_UNORM`.
6. RGB parity gate: maximum absolute error <= 1 LSB for every case.
7. Every GPU output is preserved as PNG.
8. Performance test runs 1280x720 -> 1920x1080.
9. Warm-up: 300 draws. Valid measurements: 512.
10. Timing uses the existing 8-slot `D3D11_QUERY_TIMESTAMP_DISJOINT` ring and `D3D11_ASYNC_GETDATA_DONOTFLUSH`; no explicit `Flush()` is added.
11. A one-sample bilinear control pass is measured separately so the median delta can be reported.

## Output

The fresh `hardware_builds\run_*\results` directory contains:

- `parity.csv`
- `timing.csv`
- `hardware_summary.txt`
- `gpu_outputs\*.png` (42 files)

## Exit codes

- 0: all parity cases pass and timing completes
- 30: NVIDIA/FL11.0 device unavailable
- 40: at least one parity case exceeds 1 LSB
- 50: non-blocking timing could not collect 512 valid samples
- 10-23 / 30 build-stage: missing tool or compile/audit failure

## Important boundary

This standalone validator measures the shader pass, not final integration inside the P1FG device-C presenter. A passing standalone result is required before presenter integration but is not a substitute for the later in-runtime test.
