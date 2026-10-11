from pathlib import Path

p=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp')
s=p.read_text(encoding='utf-8')

for token in (
    'VISIBLE60_FASTBLIT_FALLBACK',
    'PtFastBlitSurfaceToBackBuffer',
    'actual->BackBufferCount=2;',
    'actual->PresentationInterval=D3DPRESENT_INTERVAL_ONE;',
    'HRESULT generatedPresent=PresentPTARTexture(',
):
    if token not in s:
        raise SystemExit('FASTBLIT2 base gate missing: '+token)
for forbidden in ('ptar_d3d9_isolated_presenter.h','PtD3D9IsolatedPresenterStart(','g_ptarD3D9IsoDisplayDevice'):
    if forbidden in s:
        raise SystemExit('forbidden HF7+ presenter architecture in base: '+forbidden)

old='''static PTARContext g_ptar={};\n\nstatic void Log(const wchar_t* fmt,...)'''
new='''static PTARContext g_ptar={};\n\nstatic void PtTemporalPresenterStop();\n\nstatic void Log(const wchar_t* fmt,...)'''
if s.count(old)!=1: raise SystemExit('forward declaration anchor mismatch')
s=s.replace(old,new,1)

old='''static void ReleasePTARResources()\n{\n    g_ptar.active=false;'''
new='''static void ReleasePTARResources()\n{\n    // Temporal presenter owns additional swap chains on the same device. Stop\n    // it before Reset/resource teardown. FG itself remains the HF6 path.\n    PtTemporalPresenterStop();\n    g_ptar.active=false;'''
if s.count(old)!=1: raise SystemExit('ReleasePTARResources anchor mismatch')
s=s.replace(old,new,1)

old='''    return S_OK;\n}\n\nstatic void RotateRealHistory()'''
new='''    return S_OK;\n}\n\n#include "ptar_d3d9_temporal_presenter.h"\n\nstatic void RotateRealHistory()'''
if s.count(old)!=1: raise SystemExit('temporal header include anchor mismatch')
s=s.replace(old,new,1)

old='''    g_ptar.inPresent=true;\n    PtHudFrameTick();\n\n    HRESULT result=S_OK;'''
new='''    g_ptar.inPresent=true;\n    PtHudFrameTick();\n\n    if(!PtHudFgEnabled() && PtTemporalOwnsDisplay())\n    {\n        PtDiagLogA("TEMPORAL_PRESENTER_STOP fg=OFF");\n        PtTemporalPresenterStop();\n    }\n\n    HRESULT result=S_OK;'''
if s.count(old)!=1: raise SystemExit('HookPresent prelude anchor mismatch')
s=s.replace(old,new,1)

old_block='''    {\n        bool fgProducing=false;\n        bool fgPipelineReady=false;\n        const bool fgSession=\n            PtHudFgEnabled() && g_ptar.previousRealValid;\n\n        if(fgSession)\n        {\n            HRESULT fgHr=RenderFGMotionAndIntermediate(self);\n            if(SUCCEEDED(fgHr))\n            {\n                fgPipelineReady=true;\n\n                // Build FG first, then let the production-port local-grid\n                // pacer place GENERATED/REAL without historical skip storms.\n                if(PtFgPacerPrepareGenerated())\n                {\n                    PtDiagStage("FG_PRESENT_GENERATED");\n                    HRESULT generatedPresent=PresentPTARTexture(\n                        self,g_ptar.generatedTexture,g_ptar.generatedSurface,\n                        hwnd,dirty,true,true);\n                    if(SUCCEEDED(generatedPresent))\n                    {\n                        fgProducing=true;\n                    }\n                    else\n                    {\n                        PtDiagLogA(\n                            "FG_PRESENT_GENERATED_FAIL hr=0x%08lX",\n                            (unsigned long)generatedPresent);\n                    }\n                }\n            }\n            else\n            {\n                PtDiagLogA(\n                    "FG_PIPELINE_FALLBACK_REAL hr=0x%08lX",\n                    (unsigned long)fgHr);\n            }\n        }\n\n        // Preserve the production-port GENERATED -> REAL local cadence when\n        // the FG pipeline is healthy. If it fails, return to REAL-only.\n        PtFgPacerPrepareReal(fgSession && fgPipelineReady);\n\n        PtDiagStage("PRESENT_REAL");\n        result=PresentPTARTexture(\n            self,g_ptar.currentRealTexture,g_ptar.currentRealSurface,\n            hwnd,dirty,false,fgProducing);\n\n        if(SUCCEEDED(result))\n            RotateRealHistory();\n    }'''
new_block='''    {\n        bool fgProducing=false;\n        bool fgPipelineReady=false;\n        const bool fgSession=\n            PtHudFgEnabled() && g_ptar.previousRealValid;\n\n        if(fgSession)\n        {\n            HRESULT fgHr=RenderFGMotionAndIntermediate(self);\n            if(SUCCEEDED(fgHr))\n            {\n                fgPipelineReady=true;\n                PtFgPacerPrepareGenerated();\n\n                // TEMPORALPAIR1 changes ONLY display transport. The exact HF6\n                // G/R images are copied into a complete pair on the source\n                // thread; the worker then Presents G and matching R on two\n                // separate 60-Hz deadlines. Presenter setup is fail-open and\n                // is never allowed to turn FG off.\n                if(!PtTemporalOwnsDisplay())\n                {\n                    HWND presenterHwnd=hwnd?hwnd:g_ptarTemporalPreferredWindow;\n                    PtTemporalPresenterStart(presenterHwnd);\n                }\n\n                if(PtTemporalOwnsDisplay() &&\n                   PtTemporalSubmitPair(\n                       g_ptar.currentRealTexture,g_ptar.currentRealSurface,\n                       g_ptar.generatedTexture,g_ptar.generatedSurface))\n                {\n                    PtFgPacerPrepareReal(true);\n                    result=S_OK;\n                    RotateRealHistory();\n                    goto temporal_pair_done;\n                }\n\n                // Unsupported temporal transport must preserve the existing\n                // field-functional FASTBLIT2 FG rather than disabling FG.\n                if(PtFgPacerPrepareGenerated())\n                {\n                    PtDiagStage("FG_PRESENT_GENERATED_FALLBACK");\n                    HRESULT generatedPresent=PresentPTARTexture(\n                        self,g_ptar.generatedTexture,g_ptar.generatedSurface,\n                        hwnd,dirty,true,true);\n                    if(SUCCEEDED(generatedPresent))\n                        fgProducing=true;\n                    else\n                        PtDiagLogA(\n                            "FG_PRESENT_GENERATED_FALLBACK_FAIL hr=0x%08lX",\n                            (unsigned long)generatedPresent);\n                }\n            }\n            else\n            {\n                PtDiagLogA(\n                    "FG_PIPELINE_FALLBACK_REAL hr=0x%08lX",\n                    (unsigned long)fgHr);\n            }\n        }\n\n        PtFgPacerPrepareReal(fgSession && fgPipelineReady);\n        PtDiagStage("PRESENT_REAL");\n        result=PresentPTARTexture(\n            self,g_ptar.currentRealTexture,g_ptar.currentRealSurface,\n            hwnd,dirty,false,fgProducing);\n\n        if(SUCCEEDED(result))\n            RotateRealHistory();\n\ntemporal_pair_done:\n        ;\n    }'''
if s.count(old_block)!=1: raise SystemExit('HookPresent FG block mismatch')
s=s.replace(old_block,new_block,1)

old='''    if(!pp)\n        return D3DERR_INVALIDCALL;\n\n    const D3DPRESENT_PARAMETERS original=*pp;'''
new='''    if(!pp)\n        return D3DERR_INVALIDCALL;\n\n    const D3DPRESENT_PARAMETERS original=*pp;\n    if(original.hDeviceWindow)\n        g_ptarTemporalPreferredWindow=original.hDeviceWindow;'''
if s.count(old)!=1: raise SystemExit('Reset hwnd anchor mismatch')
s=s.replace(old,new,1)

old='''    const D3DPRESENT_PARAMETERS original=*pp;\n    PtDiagLogA(\n        "CREATEDEVICE_PP'''
new='''    const D3DPRESENT_PARAMETERS original=*pp;\n    if(original.hDeviceWindow)\n        g_ptarTemporalPreferredWindow=original.hDeviceWindow;\n    else if(focus)\n        g_ptarTemporalPreferredWindow=focus;\n    PtDiagLogA(\n        "CREATEDEVICE_PP'''
if s.count(old)!=1: raise SystemExit('CreateDevice hwnd anchor mismatch')
s=s.replace(old,new,1)

old='''    if(!g_ptarResolutionPolicy.enabled)\n    {\n        PtDiagLogA("CREATEDEVICE_PTAR_DISABLED_BY_CONFIG");\n        return g_realCreateDevice(self,adapter,type,focus,flags,pp,out);\n    }\n\n    PTARResolutionPlan plan={};'''
new='''    if(!g_ptarResolutionPolicy.enabled)\n    {\n        PtDiagLogA("CREATEDEVICE_PTAR_DISABLED_BY_CONFIG");\n        return g_realCreateDevice(self,adapter,type,focus,flags,pp,out);\n    }\n\n    // Presentation-only support: worker Present calls use additional swap\n    // chains on this SAME D3D9 device. MULTITHREADED is required by D3D9 for\n    // cross-thread device/swap-chain access; no FG algorithm changes result.\n    const DWORD ptarCreateFlags=flags|D3DCREATE_MULTITHREADED;\n    PtDiagLogA(\n        "CREATEDEVICE_TEMPORAL_FLAGS original=0x%08lX actual=0x%08lX",\n        (unsigned long)flags,(unsigned long)ptarCreateFlags);\n\n    PTARResolutionPlan plan={};'''
if s.count(old)!=1: raise SystemExit('CreateDevice flag anchor mismatch')
s=s.replace(old,new,1)

old='''    HRESULT hr=g_realCreateDevice(\n        self,adapter,type,focus,flags,&actual,out);'''
new='''    HRESULT hr=g_realCreateDevice(\n        self,adapter,type,focus,ptarCreateFlags,&actual,out);'''
if s.count(old)!=1: raise SystemExit('primary CreateDevice anchor mismatch')
s=s.replace(old,new,1)
old='''        hr=g_realCreateDevice(\n            self,adapter,type,focus,flags,&actual,out);'''
new='''        hr=g_realCreateDevice(\n            self,adapter,type,focus,ptarCreateFlags,&actual,out);'''
if s.count(old)!=1: raise SystemExit('retry CreateDevice anchor mismatch')
s=s.replace(old,new,1)

p.write_text(s,encoding='utf-8',newline='\n')

print('HF6_TEMPORALPAIR1_PATCH=PASS')
print('FG_ALGORITHM=UNCHANGED_HF6')
print('PRESENTATION_TIMING=60HZ_QPC_NO_CATCHUP')
print('PAIR_TRANSPORT=SOURCE_PREFILL_G_R__WORKER_PRESENT_ONLY')
print('PAIR_PRIME_DEPTH=2')
print('PAIR_SLOT_COUNT=3')
print('TEMPORAL_FAILURE_POLICY=FASTBLIT2_SYNC_FALLBACK_FG_STAYS_ON')
print('SECOND_DEVICE=NONE')
print('D3D9EX=NONE')
