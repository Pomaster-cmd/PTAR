from pathlib import Path
import hashlib

pp=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp')
if not pp.is_file():
    raise SystemExit('patched FASTBLIT2 proxy source missing')
s=pp.read_text(encoding='utf-8')

# This patch MUST be applied after apply_hf6_visible60_fastblit2.py.
for marker in (
    'HF6 VISIBLE60 FASTBLIT2',
    'PtRefreshQueuedRealBackBuffer',
    'PtFastBlitSurfaceToBackBuffer',
    'actual->BackBufferCount=2',
    'actual->PresentationInterval=D3DPRESENT_INTERVAL_ONE',
):
    if marker not in s:
        raise SystemExit('FASTBLIT2 prerequisite missing: '+marker)

# Add the shared deterministic grid used by runtime + live lab harness.
inc='#include "ptar_fg_pacer.h"'
if s.count(inc)!=1:
    raise SystemExit('pacer include anchor mismatch')
s=s.replace(inc,inc+'\n#include "ptar_temporal_grid.h"',1)

# Presenter state is deliberately same-device.  No D3D9Ex, no second device,
# no additional swapchain.  The only cross-thread D3D calls are StretchRect,
# GetBackBuffer and Present on a device explicitly created MULTITHREADED.
ctx='static PTARContext g_ptar={};'
state=r'''static PTARContext g_ptar={};

#define PTAR_TEMPORAL_SLOT_COUNT 3
#define PTAR_TEMPORAL_FREE 0
#define PTAR_TEMPORAL_WRITING 1
#define PTAR_TEMPORAL_READY 2
#define PTAR_TEMPORAL_DISPLAYING 3

struct PTARTemporalPairSlot
{
    volatile LONG state;
    LONG sequence;
    IDirect3DTexture9* generatedTexture;
    IDirect3DSurface9* generatedSurface;
    IDirect3DTexture9* realTexture;
    IDirect3DSurface9* realSurface;
    DWORD generatedMarker;
    DWORD realMarker;
    HWND hwnd;
};

struct PTARTemporalPresenter
{
    IDirect3DDevice9* device;
    HANDLE readyEvent;
    HANDLE thread;
    volatile LONG stopRequested;
    volatile LONG active;
    volatile LONG healthy;
    volatile LONG sequence;
    volatile LONG mailboxDrops;
    volatile LONG starvationSlots;
    volatile LONG presentFailures;
    volatile LONG presentedGenerated;
    volatile LONG presentedReal;
    PTARTemporalGrid grid;
    PTARTemporalPairSlot slots[PTAR_TEMPORAL_SLOT_COUNT];
};

static PTARTemporalPresenter g_ptarTemporal={};
static void PtTemporalShutdown();'''
if s.count(ctx)!=1:
    raise SystemExit('PTAR context anchor mismatch')
s=s.replace(ctx,state,1)

# Stop the display worker before any source texture/backbuffer is released.
rel='''static void ReleasePTARResources()\n{\n    g_ptar.active=false;'''
rel2='''static void ReleasePTARResources()\n{\n    PtTemporalShutdown();\n    g_ptar.active=false;'''
if s.count(rel)!=1:
    raise SystemExit('ReleasePTARResources anchor mismatch')
s=s.replace(rel,rel2,1)

# Runtime implementation goes immediately before RotateRealHistory: at this
# point all copy/HUD helpers are defined, while HookPresent follows later.
anchor='''static void RotateRealHistory()\n{'''
impl=r'''static void PtTemporalReleaseSlotResources(PTARTemporalPairSlot* slot)
{
    if(!slot) return;
    if(slot->generatedSurface){slot->generatedSurface->Release();slot->generatedSurface=0;}
    if(slot->generatedTexture){slot->generatedTexture->Release();slot->generatedTexture=0;}
    if(slot->realSurface){slot->realSurface->Release();slot->realSurface=0;}
    if(slot->realTexture){slot->realTexture->Release();slot->realTexture=0;}
    slot->generatedMarker=0;
    slot->realMarker=0;
    slot->sequence=0;
    slot->hwnd=0;
    InterlockedExchange(&slot->state,PTAR_TEMPORAL_FREE);
}

static int PtTemporalAcquireWriteSlot()
{
    for(int i=0;i<PTAR_TEMPORAL_SLOT_COUNT;++i)
    {
        if(InterlockedCompareExchange(
            &g_ptarTemporal.slots[i].state,
            PTAR_TEMPORAL_WRITING,
            PTAR_TEMPORAL_FREE)==PTAR_TEMPORAL_FREE)
            return i;
    }

    // Bounded latency: when production temporarily outruns the 60-Hz worker,
    // recycle the oldest READY pair. Never steal the pair being displayed.
    int oldest=-1;
    LONG oldestSeq=0x7fffffff;
    for(int i=0;i<PTAR_TEMPORAL_SLOT_COUNT;++i)
    {
        PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[i];
        if(slot->state==PTAR_TEMPORAL_READY && slot->sequence<oldestSeq)
        {
            oldest=i;
            oldestSeq=slot->sequence;
        }
    }
    if(oldest>=0 && InterlockedCompareExchange(
        &g_ptarTemporal.slots[oldest].state,
        PTAR_TEMPORAL_WRITING,
        PTAR_TEMPORAL_READY)==PTAR_TEMPORAL_READY)
    {
        InterlockedIncrement(&g_ptarTemporal.mailboxDrops);
        return oldest;
    }
    return -1;
}

static int PtTemporalClaimOldestReady()
{
    for(;;)
    {
        int oldest=-1;
        LONG oldestSeq=0x7fffffff;
        for(int i=0;i<PTAR_TEMPORAL_SLOT_COUNT;++i)
        {
            PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[i];
            if(slot->state==PTAR_TEMPORAL_READY && slot->sequence<oldestSeq)
            {
                oldest=i;
                oldestSeq=slot->sequence;
            }
        }
        if(oldest<0)
            return -1;
        if(InterlockedCompareExchange(
            &g_ptarTemporal.slots[oldest].state,
            PTAR_TEMPORAL_DISPLAYING,
            PTAR_TEMPORAL_READY)==PTAR_TEMPORAL_READY)
            return oldest;
    }
}

static HRESULT PtTemporalRenderHudToSurface(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    bool generated,
    DWORD* markerOut)
{
    if(markerOut) *markerOut=0;
    if(!dev || !target)
        return D3DERR_INVALIDCALL;

    // RenderGW16IProductionHud targets g_ptar.realBackBuffer. During source
    // staging the worker never touches this global pointer, so redirect it
    // transiently to the offscreen mailbox surface while the game state block
    // is already captured by HookPresent.
    IDirect3DSurface9* saved=g_ptar.realBackBuffer;
    g_ptar.realBackBuffer=target;
    HRESULT hr=RenderGW16IProductionHud(dev,generated,true);
    if(SUCCEEDED(hr) && markerOut)
        *markerOut=(DWORD)(g_ptarHudFrameSequence&4095ul);
    g_ptar.realBackBuffer=saved;
    return hr;
}

static HRESULT PtTemporalStageFrame(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* sourceTexture,
    IDirect3DSurface9* sourceSurface,
    IDirect3DSurface9* targetSurface,
    bool generated,
    DWORD* markerOut)
{
    if(!dev || !sourceTexture || !sourceSurface || !targetSurface)
        return D3DERR_INVALIDCALL;

    HRESULT hr=dev->StretchRect(
        sourceSurface,0,targetSurface,0,D3DTEXF_NONE);
    if(FAILED(hr))
    {
        // Exact HF6 copy fallback. This is source-side staging only; it does
        // not alter ME/refine/interpolation or the temporal grid.
        PtDiagLogA(
            "TEMPORAL_STAGE_STRETCHRECT_FALLBACK hr=0x%08lX frame=%s",
            (unsigned long)hr,generated?"GENERATED":"REAL");
        hr=DrawFullscreenPass(
            dev,targetSurface,
            g_ptar.outputW,g_ptar.outputH,
            g_ptar.bilinearShader,
            sourceTexture,0,0,0);
    }
    if(FAILED(hr))
        return hr;

    HRESULT hudHr=PtTemporalRenderHudToSurface(
        dev,targetSurface,generated,markerOut);
    if(FAILED(hudHr))
    {
        // HUD remains fail-open exactly like the proven synchronous path.
        PtDiagLogA(
            "TEMPORAL_HUD_FAILOPEN hr=0x%08lX frame=%s",
            (unsigned long)hudHr,generated?"GENERATED":"REAL");
    }
    return S_OK;
}

static HRESULT PtTemporalPresentOne(
    PTARTemporalPairSlot* slot,
    bool generated,
    LONGLONG* callStartOut)
{
    if(callStartOut) *callStartOut=0;
    if(!slot || !g_ptarTemporal.device || !g_realGetBackBuffer || !g_realPresent)
        return D3DERR_INVALIDCALL;

    IDirect3DSurface9* source=
        generated?slot->generatedSurface:slot->realSurface;
    const DWORD marker=
        generated?slot->generatedMarker:slot->realMarker;
    if(!source)
        return D3DERR_INVALIDCALL;

    IDirect3DSurface9* back=0;
    HRESULT hr=g_realGetBackBuffer(
        g_ptarTemporal.device,0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(FAILED(hr) || !back)
    {
        if(back) back->Release();
        return FAILED(hr)?hr:E_FAIL;
    }

    hr=g_ptarTemporal.device->StretchRect(
        source,0,back,0,D3DTEXF_NONE);

    // F9 remains post-overlay: the mailbox surface already contains the HUD.
    if(SUCCEEDED(hr) && PtCaptureConsumeRequest())
    {
        wchar_t saved[MAX_PATH]={0};
        HRESULT captureHr=PtCaptureSavePostOverlayBmp(
            g_ptarTemporal.device,back,g_self,
            saved,_countof(saved));
        if(SUCCEEDED(captureHr))
        {
            PtHudNotifyCaptureSaved();
            PtDiagLogA(
                "OK: F9 temporal D3D9 screenshot saved path=%ls frame=%s",
                saved,generated?"GENERATED":"REAL");
        }
        else
        {
            PtDiagLogA(
                "ERROR: F9 temporal D3D9 screenshot failed hr=0x%08lX",
                (unsigned long)captureHr);
        }
    }
    back->Release();
    if(FAILED(hr))
        return hr;

    const LONGLONG callStart=PtTemporalGridNow();
    if(callStartOut) *callStartOut=callStart;

    hr=g_realPresent(
        g_ptarTemporal.device,0,0,slot->hwnd,0);
    if(SUCCEEDED(hr))
    {
        PtFgPacerRecordVisible(generated);
        PtD3D9DiagRecordPresent(
            generated,
            marker,
            g_ptarFgPacer.lastVisibleQpc,
            PtFgPacerRealCount(),
            PtFgPacerGeneratedCount(),
            g_ptarFgPacer.resyncs,
            PtFgPacerLateSkipCount());
        if(generated)
            InterlockedIncrement(&g_ptarTemporal.presentedGenerated);
        else
            InterlockedIncrement(&g_ptarTemporal.presentedReal);
    }
    return hr;
}

static DWORD WINAPI PtTemporalPresenterThread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
    PtTemporalGridReset(&g_ptarTemporal.grid);

    int current=-1;
    bool generatedPhase=true;
    bool starved=true;

    PtDiagLogA(
        "TEMPORAL_WORKER_START policy=60HZ_GRID prebuffer=1slot order=G_R");

    while(InterlockedCompareExchange(
        &g_ptarTemporal.stopRequested,0,0)==0)
    {
        if(current<0)
        {
            current=PtTemporalClaimOldestReady();
            if(current<0)
            {
                starved=true;
                WaitForSingleObject(g_ptarTemporal.readyEvent,2);
                continue;
            }

            const LONGLONG now=PtTemporalGridNow();
            if(!g_ptarTemporal.grid.armed)
                PtTemporalGridArmWithOneSlotPrebuffer(
                    &g_ptarTemporal.grid,now);
            else if(starved)
                PtTemporalGridResumeAfterStarvation(
                    &g_ptarTemporal.grid,now);
            generatedPhase=true;
            starved=false;
        }

        PtTemporalGridWaitUntilDeadline(
            &g_ptarTemporal.grid,
            &g_ptarTemporal.stopRequested);
        if(InterlockedCompareExchange(
            &g_ptarTemporal.stopRequested,0,0)!=0)
            break;

        PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[current];
        LONGLONG callStart=0;
        HRESULT hr=PtTemporalPresentOne(
            slot,generatedPhase,&callStart);

        // Grid progression uses Present CALL START, not return time. If a
        // Sync1 Present blocks until VBlank, the next call is therefore issued
        // immediately when its grid slot has arrived instead of accidentally
        // adding a second 16.67-ms wait.
        if(callStart<=0)
            callStart=PtTemporalGridNow();
        PtTemporalGridRecordSubmission(
            &g_ptarTemporal.grid,callStart);

        if(FAILED(hr))
        {
            InterlockedIncrement(&g_ptarTemporal.presentFailures);
            PtDiagLogA(
                "TEMPORAL_PRESENT_FAIL hr=0x%08lX phase=%s failures=%ld",
                (unsigned long)hr,
                generatedPhase?"GENERATED":"REAL",
                g_ptarTemporal.presentFailures);

            // Never retry immediately: the grid already advanced one slot.
            // Device-lost or repeated failures force source-side fail-open on
            // the next HookPresent.
            if(hr==D3DERR_DEVICELOST || g_ptarTemporal.presentFailures>=3)
                InterlockedExchange(&g_ptarTemporal.healthy,0);
        }

        if(generatedPhase)
        {
            generatedPhase=false;
        }
        else
        {
            InterlockedExchange(&slot->state,PTAR_TEMPORAL_FREE);
            current=-1;
            generatedPhase=true;
        }
    }

    if(current>=0)
        InterlockedExchange(
            &g_ptarTemporal.slots[current].state,
            PTAR_TEMPORAL_FREE);

    PtDiagLogA(
        "TEMPORAL_WORKER_STOP G=%ld R=%ld drops=%ld failures=%ld skipped_grid=%lu yields=%lu",
        g_ptarTemporal.presentedGenerated,
        g_ptarTemporal.presentedReal,
        g_ptarTemporal.mailboxDrops,
        g_ptarTemporal.presentFailures,
        g_ptarTemporal.grid.skippedGridSlots,
        g_ptarTemporal.grid.waitYields);
    return 0;
}

static void PtTemporalShutdown()
{
    if(g_ptarTemporal.thread)
    {
        InterlockedExchange(&g_ptarTemporal.stopRequested,1);
        if(g_ptarTemporal.readyEvent)
            SetEvent(g_ptarTemporal.readyEvent);
        WaitForSingleObject(g_ptarTemporal.thread,2000);
        CloseHandle(g_ptarTemporal.thread);
        g_ptarTemporal.thread=0;
    }

    if(g_ptarTemporal.readyEvent)
    {
        CloseHandle(g_ptarTemporal.readyEvent);
        g_ptarTemporal.readyEvent=0;
    }

    for(int i=0;i<PTAR_TEMPORAL_SLOT_COUNT;++i)
        PtTemporalReleaseSlotResources(&g_ptarTemporal.slots[i]);

    if(g_ptarTemporal.device)
    {
        g_ptarTemporal.device->Release();
        g_ptarTemporal.device=0;
    }

    ZeroMemory(&g_ptarTemporal,sizeof(g_ptarTemporal));
}

static bool PtTemporalEnsure(IDirect3DDevice9* dev)
{
    if(!dev)
        return false;

    if(g_ptarTemporal.active)
        return g_ptarTemporal.device==dev &&
            InterlockedCompareExchange(&g_ptarTemporal.healthy,0,0)!=0;

    ZeroMemory(&g_ptarTemporal,sizeof(g_ptarTemporal));
    g_ptarTemporal.device=dev;
    dev->AddRef();

    for(int i=0;i<PTAR_TEMPORAL_SLOT_COUNT;++i)
    {
        PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[i];
        HRESULT hr=CreateRenderTexture(
            dev,g_ptar.outputW,g_ptar.outputH,g_ptar.outputFormat,
            &slot->generatedTexture,&slot->generatedSurface);
        if(FAILED(hr))
        {
            PtDiagLogA(
                "TEMPORAL_SLOT_CREATE_G_FAIL slot=%d hr=0x%08lX",
                i,(unsigned long)hr);
            PtTemporalShutdown();
            return false;
        }
        hr=CreateRenderTexture(
            dev,g_ptar.outputW,g_ptar.outputH,g_ptar.outputFormat,
            &slot->realTexture,&slot->realSurface);
        if(FAILED(hr))
        {
            PtDiagLogA(
                "TEMPORAL_SLOT_CREATE_R_FAIL slot=%d hr=0x%08lX",
                i,(unsigned long)hr);
            PtTemporalShutdown();
            return false;
        }
        InterlockedExchange(&slot->state,PTAR_TEMPORAL_FREE);
    }

    g_ptarTemporal.readyEvent=CreateEventW(0,FALSE,FALSE,0);
    if(!g_ptarTemporal.readyEvent)
    {
        PtDiagLogA(
            "TEMPORAL_READY_EVENT_FAIL gle=%lu",
            (unsigned long)GetLastError());
        PtTemporalShutdown();
        return false;
    }

    InterlockedExchange(&g_ptarTemporal.stopRequested,0);
    InterlockedExchange(&g_ptarTemporal.healthy,1);
    InterlockedExchange(&g_ptarTemporal.active,1);

    g_ptarTemporal.thread=CreateThread(
        0,0,&PtTemporalPresenterThread,0,0,0);
    if(!g_ptarTemporal.thread)
    {
        PtDiagLogA(
            "TEMPORAL_THREAD_CREATE_FAIL gle=%lu",
            (unsigned long)GetLastError());
        PtTemporalShutdown();
        return false;
    }

    PtDiagLogA(
        "TEMPORAL_PRESENTER_READY device=%p slots=%d policy=60HZ_GRID_SYNC1_SAME_DEVICE",
        dev,PTAR_TEMPORAL_SLOT_COUNT);
    return true;
}

static HRESULT PtTemporalPublishPair(
    IDirect3DDevice9* dev,
    HWND hwnd)
{
    if(!PtTemporalEnsure(dev))
        return E_FAIL;
    if(InterlockedCompareExchange(&g_ptarTemporal.healthy,0,0)==0)
        return E_FAIL;

    const int index=PtTemporalAcquireWriteSlot();
    if(index<0)
    {
        InterlockedIncrement(&g_ptarTemporal.mailboxDrops);
        PtDiagLogA(
            "TEMPORAL_MAILBOX_DROP reason=NO_WRITABLE_SLOT drops=%ld",
            g_ptarTemporal.mailboxDrops);
        return S_FALSE;
    }

    PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[index];
    HRESULT hr=PtTemporalStageFrame(
        dev,
        g_ptar.generatedTexture,g_ptar.generatedSurface,
        slot->generatedSurface,true,
        &slot->generatedMarker);
    if(FAILED(hr))
        goto fail;

    hr=PtTemporalStageFrame(
        dev,
        g_ptar.currentRealTexture,g_ptar.currentRealSurface,
        slot->realSurface,false,
        &slot->realMarker);
    if(FAILED(hr))
        goto fail;

    slot->hwnd=hwnd;
    slot->sequence=InterlockedIncrement(&g_ptarTemporal.sequence);
    InterlockedExchange(&slot->state,PTAR_TEMPORAL_READY);
    SetEvent(g_ptarTemporal.readyEvent);

    PtDiagLogA(
        "TEMPORAL_PAIR_PUBLISHED seq=%ld slot=%d gmarker=%lu rmarker=%lu drops=%ld",
        slot->sequence,index,
        (unsigned long)slot->generatedMarker,
        (unsigned long)slot->realMarker,
        g_ptarTemporal.mailboxDrops);
    return S_OK;

fail:
    PtDiagLogA(
        "TEMPORAL_PAIR_STAGE_FAIL slot=%d hr=0x%08lX",
        index,(unsigned long)hr);
    InterlockedExchange(&slot->state,PTAR_TEMPORAL_FREE);
    return hr;
}

static void RotateRealHistory()
{'''
if s.count(anchor)!=1:
    raise SystemExit('RotateRealHistory insertion anchor mismatch')
s=s.replace(anchor,impl,1)

# Replace only the FG-present block.  FG computation above it remains exactly
# HF6/FASTBLIT2; temporal publishing begins only after interpolation succeeded.
start=s.find('''    {\n        bool fgProducing=false;\n        bool fgPipelineReady=false;''')
if start<0:
    raise SystemExit('HookPresent FG block start not found')
end_marker='''        if(SUCCEEDED(result))\n            RotateRealHistory();\n    }'''
end=s.find(end_marker,start)
if end<0:
    raise SystemExit('HookPresent FG block end not found')
end += len(end_marker)
old_block=s[start:end]
new_block=r'''    {
        bool fgProducing=false;
        bool fgPipelineReady=false;
        const bool fgSession=
            PtHudFgEnabled() && g_ptar.previousRealValid;

        if(fgSession)
        {
            HRESULT fgHr=RenderFGMotionAndIntermediate(self);
            if(SUCCEEDED(fgHr))
            {
                fgPipelineReady=true;
                PtDiagStage("FG_TEMPORAL_PUBLISH_PAIR");
                HRESULT publishHr=PtTemporalPublishPair(self,hwnd);
                if(SUCCEEDED(publishHr))
                {
                    // S_FALSE means this source pair was deliberately dropped
                    // because all bounded mailbox slots were busy. Never fall
                    // back to an immediate Present here: that would re-create
                    // the exact micro-burst temporal defect being fixed.
                    fgProducing=true;
                    result=S_OK;
                    RotateRealHistory();
                }
                else
                {
                    PtDiagLogA(
                        "TEMPORAL_PUBLISH_FAIL hr=0x%08lX fallback=SYNC_FASTBLIT2",
                        (unsigned long)publishHr);
                    PtTemporalShutdown();
                    fgPipelineReady=false;
                }
            }
            else
            {
                PtDiagLogA(
                    "FG_PIPELINE_FALLBACK_REAL hr=0x%08lX",
                    (unsigned long)fgHr);
                PtTemporalShutdown();
            }
        }

        if(!(fgSession && fgPipelineReady && fgProducing))
        {
            if(!PtHudFgEnabled())
                PtTemporalShutdown();

            // Exact proven FASTBLIT2 fail-open path. No temporal worker and no
            // FG-core change is allowed to make the game unpresentable.
            PtFgPacerPrepareReal(false);
            PtDiagStage("PRESENT_REAL");
            result=PresentPTARTexture(
                self,g_ptar.currentRealTexture,g_ptar.currentRealSurface,
                hwnd,dirty,false,false);
            if(SUCCEEDED(result))
                RotateRealHistory();
        }
    }'''
s=s[:start]+new_block+s[end:]

# The same device is now legitimately used by the game/source thread and by
# the presentation worker.  D3D9 requires MULTITHREADED for that contract.
needle='''    HRESULT hr=g_realCreateDevice(\n        self,adapter,type,focus,flags,&actual,out);'''
replacement='''    const DWORD ptarDeviceFlags=flags|D3DCREATE_MULTITHREADED;\n    HRESULT hr=g_realCreateDevice(\n        self,adapter,type,focus,ptarDeviceFlags,&actual,out);'''
if s.count(needle)!=1:
    raise SystemExit('primary CreateDevice anchor mismatch')
s=s.replace(needle,replacement,1)

retry='''        hr=g_realCreateDevice(\n            self,adapter,type,focus,flags,&actual,out);'''
retry2='''        hr=g_realCreateDevice(\n            self,adapter,type,focus,ptarDeviceFlags,&actual,out);'''
if s.count(retry)!=1:
    raise SystemExit('native PTAR retry CreateDevice anchor mismatch')
s=s.replace(retry,retry2,1)

# Static safety gates: timing-only same-device design.
for bad in (
    'Direct3DCreate9Ex(',
    'IDirect3DDevice9Ex* g_ptarD3D9IsoDisplayDevice',
    'CreateAdditionalSwapChain(',
    'PtD3D9IsolatedPresenterStart(',
):
    if bad in s:
        raise SystemExit('forbidden presenter topology token: '+bad)

# Make the intent machine-checkable in CI.
required=(
    '#include "ptar_temporal_grid.h"',
    'PTAR_TEMPORAL_SLOT_COUNT 3',
    'PtTemporalPresenterThread',
    'PtTemporalPublishPair(self,hwnd)',
    'D3DCREATE_MULTITHREADED',
    'TEMPORAL_PRESENTER_READY',
    'TEMPORAL_WORKER_START',
    'PtTemporalGridWaitUntilDeadline',
    'PtTemporalGridRecordSubmission',
)
for marker in required:
    if marker not in s:
        raise SystemExit('temporalpair marker missing: '+marker)

pp.write_text(s,encoding='utf-8',newline='\n')

print('HF6_TEMPORALPAIR60_PATCH=PASS')
print('FG_ALGORITHM=UNCHANGED_HF6')
print('TEMPORAL_SCOPE=PRESENTATION_ONLY')
print('DEVICE=EXISTING_GAME_D3D9_DEVICE')
print('DEVICE_THREADING=D3DCREATE_MULTITHREADED')
print('MAILBOX=3_PAIR_OFFSCREEN_RING')
print('DISPLAY_WORKER=SAME_DEVICE')
print('DISPLAY_GRID=60HZ_QPC_NO_CATCHUP')
print('DISPLAY_ORDER=GENERATED_THEN_REAL')
print('INITIAL_PREBUFFER=1_GRID_SLOT')
print('SYNC_INTERVAL=ONE_PRESERVED')
print('SECOND_DEVICE=NONE')
print('D3D9EX_PRESENTER=NONE')
print('ADDITIONAL_SWAPCHAIN=NONE')
