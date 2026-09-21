# PTAR multi-backend integration contract

Reference production base
=========================
Final integration MUST start from main commit:
  69d44d8b61c3e28a6f3ed42b8501816ac28b5438

The current main x64/D3D11 production runtime remains authoritative. The x86
work in SOURCE is an additional backend and must not replace or silently mutate
the validated x64/D3D11 payload.

Product architecture
====================
PTAR is game-agnostic and backend-oriented, not game-oriented.

A final complete deliverable must dispatch by executable architecture and
graphics API/capability, not by game name.

Current backend matrix:

1. x64 / D3D11
   Status: production baseline from main.
   Runtime: existing main payload.
   Existing profiles/compatibility behavior remain valid.

2. x86 / D3D9
   Status: development in SOURCE.
   Runtime: generic d3d9 proxy frontend + PTAR spatial/HUD/FG work.
   Splinter Cell Conviction is only a field-validation target.

Future-compatible slots:
- x86 / D3D11
- x64 / D3D9
- other graphics frontends only when intentionally implemented and validated.

Hard rules
==========
- No core runtime behavior may branch on "Conviction", "Splinter Cell" or a
  specific game name.
- Game names may appear only in validation evidence, regression fixtures, or
  optional compatibility profiles when a genuinely game-specific activation
  override is necessary.
- Generic x86/D3D9 installer/runtime must accept any x86 executable intended to
  use Direct3D 9.
- Any game-specific workaround must be data/profile driven, exact-match guarded,
  and absent from the generic algorithm unless independently justified.
- main is not modified during x86/D3D9 laboratory development.
- Final product promotion is selective: start from main, add the generic
  multi-backend dispatcher and x86/D3D9 payload, preserve current x64/D3D11
  bytes unless a separately validated production change is intentional.
- Final package must contain one coherent install/verify/uninstall workflow.
- Final package must remain Windows 8.1 compatible.
- No forced resolution/fullscreen policy in the general product. Test geometry
  may be recommended for validation, but the product must not be tied to it.
- Hardware tests are requested only after laboratory/CI coverage is exhausted.

Current field validation
========================
Tom Clancy's Splinter Cell: Conviction:
- x86 / D3D9 frontend launch: PASS
- VTABLEFIX2: PASS
- spatial PTAR path: PASS
- native HUD: PASS
- FPS/resolution display: PASS
- MoE <-> bilinear A/B toggle: PASS

This proves one real x86/D3D9 title. It is evidence for the backend, not a
claim of universal compatibility.

Next gate
=========
Port frame generation generically into the x86/D3D9 backend. After field
validation, produce a new complete PTAR deliverable by integrating the backend
into the main production package through the multi-backend dispatcher.
