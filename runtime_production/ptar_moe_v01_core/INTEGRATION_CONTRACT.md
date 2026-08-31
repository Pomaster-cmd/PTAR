# PTAR production integration contract — v0.13.0

Target integration base:
- current P1FG7N/VBLANK2 integrated presenter branch;
- B18K18 recorder branch is out of scope and must remain byte-identical;
- frame-generation modules are out of scope and must remain byte-identical.

Spatial filter policy after PTAR integration:
- POINT: existing point path;
- BILINEAR: existing bilinear path;
- PTAR: PTAR-NG MoE v01 SF5 only when the active reconstruction rectangle is
  exactly x1.5 in both dimensions;
- native 1:1: retain the existing direct CopyResource/direct-present fast path;
- any non-x1.5 scale ratio: fail open to BILINEAR, never NIS.

NVIDIA NIS contract:
- NIS is not a PTAR runtime fallback.
- NIS shader/table/NVScaler resources must not be called by the PTAR mode.
- Historical NIS code may remain only in source/reference history if the
  integration branch keeps it for comparison; production PTAR filter selection
  must not depend on it.

Expected common case:
1280x720 -> 1920x1080 = exact x1.5 => PTAR active.

Presenter ordering:
1. acquire/use final REAL or GENERATED source texture chosen by existing FG;
2. if source == native output: direct existing fast path;
3. if PTAR selected and exact x1.5: `PTARMoeRender15`;
4. otherwise POINT/BILINEAR existing path;
5. existing HUD;
6. existing recorder observes the final presenter texture exactly where it does
   in the locked B18K18 branch;
7. Present path/cadence unchanged.

No new:
- D3DCompiler runtime;
- compute dispatch;
- UAV;
- intermediate texture;
- extra D3D11 device;
- worker thread;
- Present call;
- Flush;
- blocking wait.
