# PTAR-NG MoE v02 LAB25 software admission

Status: SOFTWARE ADMITTED FOR FINAL R8G8B8A8_UNORM OUTPUT; PHYSICAL GTX 960M NOT YET EXECUTED.

## Candidate

LAB25 removes only the final `clamp(h,min(f0,f1),max(f0,f1))` from the hardware-selected LAB22 phase-fused MC shader. The MC slope limiter, direction selection, phase logic, texture accesses and exact x1.5 mapping are unchanged.

Frozen DXBC:

- LAB22 SHA-256: `148a26f453a49fa319ea7aa7a448ef30b01dece508fccc6bd21f4fdae10926b8`
- LAB25 SHA-256: `ee882f82dd017c470a5003cd522f2cb5ec31ce4b31ae341ec75f3bb737642e72`

## Static cost delta

Identical FXC `/T ps_5_0 /E main /O3 /Ges /WX` compilation:

- LAB22: 66 executable instructions, 3032-byte CSO
- LAB25: 62 executable instructions, 2920-byte CSO
- delta: **-4 instructions**, **-112 bytes**
- texture contract unchanged: 1 GatherGreen + 4 SampleLevel + 0 UAV
- removed opcode work: 2 `min` + 2 `max`

## Shape-bound proof

For a monotone interval `d=f1-f0`, the retained MC limiter constrains normalized endpoint slopes `alpha=m0/d` and `beta=m1/d` to `[0,2]`.

At exact x1.5 phase `t=1/3`:

`(h-f0)/d = (7 + 4*alpha - 2*beta)/27`, whose range is `[3/27,15/27]`.

At phase `t=2/3`:

`(h-f0)/d = (20 + 2*alpha - 4*beta)/27`, whose range is `[12/27,24/27]`.

Both ranges are strictly inside `[0,1]`; for `d=0`, the two retained slopes are zero and `h=f0=f1`. The shape clamp is therefore algebraically redundant under the retained LAB22 MC contract.

A separate float32 sweep over 1,000,000 random intervals found zero cases where the clamp changed the result.

## WARP floating-point equivalence

- canonical 24x24 -> 36x36 case: LAB22/LAB25 bit-exact, 5184/5184 values identical
- constant stress: bit-exact
- steps stress: bit-exact
- affine stress: 185 float values differ, maximum `5.9604645e-08`, all differences exactly 1 ULP
- high-frequency/noise stress: 1 float value differs by `1.4901161e-08`, exactly 1 ULP

These internal float differences are rounding-level effects from removing an otherwise inactive min/max clamp sequence.

## Final runtime-format admission

LAB25b run `34340959701` executed LAB22 and LAB25 through D3D11 WARP into the actual runtime target format `DXGI_FORMAT_R8G8B8A8_UNORM`.

Canonical 42-case corpus:

- cases: 42
- exact cases: **42/42**
- compared output bytes: **13,934,592**
- different bytes: **0**

Synthetic UNORM8 stress:

- ramps: byte-exact
- steps: byte-exact
- checker: byte-exact
- five deterministic random RGBA8 probes: byte-exact

Therefore the observed 1-ULP internal float drift does not alter final 8-bit runtime output in the canonical corpus or the additional deterministic stress set.

## Admission decision

LAB25 is admitted to a targeted GTX 960M hardware comparison against LAB22. It is not promoted yet. Hardware admission requires:

1. full 42-case GTX parity <=1 LSB;
2. persistent 42 non-zero GPU PNGs;
3. same-run balanced LAB22/LAB25/K185 timing;
4. a repeatable performance gain large enough to justify replacing LAB22.

Until those gates pass, LAB22 remains the selected v02 baseline.
