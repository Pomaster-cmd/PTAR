#!/usr/bin/env python3
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
cpp=(ROOT/"hardware_validation/ptar_k185_hw_validation.cpp").read_text(encoding="utf-8")
timer=(ROOT/"runtime_integration/d3d11/PTARD3D11GpuTimerRing.h").read_text(encoding="utf-8")

checks=[]
def ck(name,cond):
    checks.append((name,bool(cond)))
    if not cond: raise AssertionError(name)

ck("warmup 32","PTAR_WARMUP_FRAMES = 32u" in cpp)
ck("512 samples retained","PTAR_TIMING_SAMPLES = 512u" in cpp)
ck("query wait bounded","PTAR_QUERY_WAIT_TIMEOUT_MS = 1000u" in cpp)
ck("200000 loop removed","PTAR_TIMING_MAX_ITERATIONS" not in cpp)
ck("old feed loop removed","Keep feeding the command stream" not in cpp)
ck("serial helper exists","SubmitAndWaitTimedDraw" in cpp)
ck("wait helper exists","WaitForTimerSample" in cpp)
ck("GetData progress path uses flags 0",
   "m_context.Get(),&milliseconds,0u,&ready,&valid" in cpp)
ck("device reason checked","GetDeviceRemovedReason()" in cpp)
ck("Sleep 1 pacing","Sleep(1);" in cpp)
ck("no explicit context Flush","->Flush(" not in cpp and ".Flush(" not in cpp)
ck("failure logs device reason","deviceReason=0x" in cpp)
ck("bounded timing label","Timing D3D11 borne" in cpp)

ck("TryResolveEx exists","HRESULT TryResolveEx" in timer)
ck("resolver accepts flags","UINT flags" in timer)
ck("GetData uses caller flags","s.disjoint,&dj,sizeof(dj),flags" in timer)
ck("legacy DONOTFLUSH retained","D3D11_ASYNC_GETDATA_DONOTFLUSH" in timer)
ck("timer has no explicit Flush","->Flush(" not in timer and ".Flush(" not in timer)

submit_start=cpp.find("HRESULT SubmitAndWaitTimedDraw")
measure_start=cpp.find("HRESULT MeasureShader",submit_start)
submit_block=cpp[submit_start:measure_start]
ck("one draw in submit helper",submit_block.count("m_context->Draw(3,0);")==1)

measure_end=cpp.find("ComPtr<ID3D11Device> m_device",measure_start)
measure_block=cpp[measure_start:measure_end]
ck("MeasureShader does not directly enqueue draws","m_context->Draw(3,0);" not in measure_block)

print(f"{sum(1 for _,p in checks if p)}/{len(checks)} PASS")
for n,p in checks:
    print(("PASS" if p else "FAIL")+" - "+n)
