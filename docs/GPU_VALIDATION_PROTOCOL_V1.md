# PTAR GPU Validation Protocol v1

Target: Windows 8.1 + GTX 960M.

1. Run `build\windows\BUILD_EDGE_NG_V03_K185_SM5.bat`.
2. Require VS/PS SM5 compile PASS and DXBC audit: 1 gather4, 4 sample_l, no UAV.
3. Render all 42 `PTAR-PERCEPTUAL-V1-B-GRID` cases on GPU.
4. Compare against `edge_ng_v03_shader_semantic.py`; record max LSB error and mismatch fraction.
5. Use `PTARD3D11GpuTimerRing.h`: no Flush, warmup >=300 frames, collect >=512 valid non-disjoint samples.
6. Report median/P90/P95/P99/max scaler-pass milliseconds and bypass delta.
7. Promote only if compile, parity, timing and presenter stability all pass.
