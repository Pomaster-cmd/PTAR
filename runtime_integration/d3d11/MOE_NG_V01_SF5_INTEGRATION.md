# PTAR-NG MoE v01 SF5 — D3D11 integration contract

Target:
- Windows 8.1
- Direct3D 11
- Shader Model 5.0
- feature level 11_0 path

One pass:
- source SRV t0
- linear clamp sampler s0
- constants b0: input/output sizes
- fullscreen triangle VS
- MoE pixel shader

The complete MoE deliberately retains the K185 logical texture budget:
`1 gather4 + 4 sample_l`.

The Windows hardware validator compiles both the MoE and K185 control shaders,
audits each DXBC for 1 gather4 / 4 sample_l / 0 UAV, validates 42 GPU outputs
against the permanent float32 semantic images, persists 42 non-zero forensic
PNGs, then runs 512 alternating MoE/K185 timing pairs.

No explicit D3D11 Flush is used. The v0.10.6 TDR-safe serialized timing model
and v0.10.7 COM lifetime fix are retained.
