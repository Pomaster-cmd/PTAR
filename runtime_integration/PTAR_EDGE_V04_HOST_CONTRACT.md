# PTAR EDGE v04 Runtime Host Contract

Status: integration contract, not an EDGE v03 reconstruction.

## Recovered runtime evidence

The recent P1FG7N/B18D runtime demonstrated a proprietary spatial scaler path with:
- isolated display device C;
- one pixel pass;
- five source loads;
- no UAV;
- no intermediate render target required by the scaler;
- no additional flush required by the scaler;
- real and generated frames both eligible;
- NVIDIA NIS disabled during this FG scaler path.

These properties are treated as an integration target only.
B18D is not renamed or reclassified as PTAR EDGE v03.

## PTAR EDGE v04 host requirements

The host must provide:
1. one source SRV containing the low-resolution frame;
2. one output render target owned by the presenter/display path;
3. input and output dimensions;
4. source-to-output coordinate mapping;
5. EDGE v03 baseline value if testing mode 0/3 against the historical expert;
6. existing EDGE confidence if mode 3 is used;
7. four directional samples and their luminance values;
8. fixed x1.5 phase identification (1/3 or 2/3);
9. epsilon supplied by the experiment configuration.

The PTAR STEP-WENO helper:
- performs no texture access itself;
- allocates no resources;
- creates no pass;
- performs no synchronization;
- contains no NIS dependency.

## Shader model

Target runtime: Direct3D 11 / Shader Model 5.0 / Windows 8.1.

The experimental build matrix is compile-time:
- mode 0: EDGE v03 baseline pass-through;
- mode 1: SW1;
- mode 2: SW2;
- mode 3: SW3.

No production build should pay for a dynamic mode switch.

## Promotion boundary

A host integration is not accepted as PTAR V1 until:
- an exact current EDGE baseline implementation is available;
- the permanent PTAR corpus is rendered by all candidate modes;
- non-regression is demonstrated;
- redundant EDGE correctors are actually removed;
- GTX 960M timings are measured.
