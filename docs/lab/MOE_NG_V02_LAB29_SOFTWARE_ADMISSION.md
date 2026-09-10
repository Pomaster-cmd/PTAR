# PTAR-NG MoE v02 LAB29 software admission

Status: SOFTWARE ADMITTED; TARGETED GTX 960M HARDWARE COMPARISON REQUIRED BEFORE PROMOTION.

## Candidate

LAB29 rewrites the monotonic-cubic limiter so the two slope values are carried at twice their historical magnitude and the corresponding reconstruction coefficients are halved. This removes the explicit 0.5 multiply from both limiter evaluations without changing the selected reconstruction.

Baseline: hardware-selected LAB28.

- LAB28 DXBC SHA-256: `fb77c785fd79891c321a4abf5d517367a7d873574c04046fe6c44b45a0df9a77`
- LAB29 DXBC SHA-256: `6a407b37938ec6a0bf89d13cf84fa4ae23c6f712a947fc6926bbfde9725028ed`
- LAB28: 59 DXBC instructions, 2788 bytes
- LAB29: 57 DXBC instructions, 2732 bytes
- Delta: -2 instructions, -56 bytes
- Texture contract unchanged: 1 GatherGreen + 4 SampleLevel + 0 UAV

The DXBC delta is specifically two fewer `add` instructions; all measured texture operations and other instruction classes are unchanged.

## Multi-resolution WARP parity

LAB29 is bit-exact to LAB28 at all six tested resolutions:

- 24x24 -> 36x36
- 64x64 -> 96x96
- 192x192 -> 288x288
- 320x180 -> 480x270
- 640x360 -> 960x540
- 1280x720 -> 1920x1080

Across these runs: 0 different float values and max absolute difference 0.0.

## Full software gate

The full software admission workflow completed successfully.

- CPU/D3D11 WARP parity: PASS
- Canonical float equivalence LAB28 vs LAB29: bit-exact
- High-frequency float equivalence LAB28 vs LAB29: bit-exact
- Canonical 42-case R8G8B8A8 UNORM corpus: 42/42 byte-exact
- Corpus different bytes: 0 / 13,934,592
- Synthetic UNORM8 stress probes: ramps, steps, checker and five deterministic random probes all byte-exact

The inherited CPU validator reports max absolute error `1.1920928955078125e-07` against its D3D11 filter reference, below the configured `2e-06` tolerance, with zero pixels over tolerance.

## Audited GTX 960M pack preflight

GitHub Actions run `34451550090` completed successfully for the dedicated LAB29 GTX960M pack.

The workflow verified:

- frozen shader SHA-256 values
- DXBC magic and expected static cost
- Win81 x64 comparison host built with `/MT`
- PE subsystem version 6.03
- imports limited to `d3d11.dll`, `dxgi.dll`, `bcrypt.dll`, and `KERNEL32.dll`
- no D3DCompiler, VCRUNTIME, MSVCP, UCRTBASE or API-MS-WIN-CRT dependency
- WARP smoke successfully loaded and drew LAB28, LAB29 and K185
- internal SHA-256 manifest generated for the hardware pack

The GitHub Actions artifact digest is `sha256:740469166d702a1e88babe79a0620c105710b9c5108a386593b18833ed5e31ff`.

An independently repacked clean delivery archive was also verified locally after download. Its SHA-256 is `5a9f2de083d1b53c83827722a110d33618315dcfbec410676a6050a7eb55b658`.

## Hardware protocol

The only remaining admission step is a targeted Windows 8.1 + GTX 960M run:

- LAB28 / LAB29 / K185 in one execution
- 60 warm-up triads
- 600 measured triads
- six rotating permutations, balanced 100 times each
- 1280x720 -> 1920x1080
- GPU timestamp queries
- K185 retained as a same-run power-state control

LAB29 is promoted only if its paired hardware gain over LAB28 is reproducible. Neutral or negative hardware behavior retains LAB28.

No production runtime is modified by LAB29 at this stage.
