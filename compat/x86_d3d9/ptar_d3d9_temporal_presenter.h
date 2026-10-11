#pragma once

#include "ptar_temporal_pair_scheduler.h"

// TEMPORALPAIR1: presentation-only adaptation on top of the proven HF6 FG.
// The source/game thread renders exactly the HF6 REAL/GENERATED images and,
// after EndScene/HookPresent, pre-fills a pair-specific SAME-DEVICE additional
// swap chain: backbuffer 0 = GENERATED, backbuffer 1 = matching REAL.
//
// The worker never renders and never calls StretchRect. It only calls Present
// on an additional swap chain, which is the D3D9-safe cross-thread operation
// for a backbuffer that is not the game's current render target. Pair cadence
// is an absolute 60-Hz QPC timeline with a hard no-catch-up rule: after any
// late tick, the next Present is at least one complete refresh period later.
// This explicitly prevents the field 31 ms / 0.5 ms G/R microburst pattern.
//
// Failure policy is deliberately fail-open: temporal presentation can NEVER
// turn FG off. If setup is unavailable, HookPresent uses the exact FASTBLIT2
// synchronous path that is already field-proven to keep FG functional.

#define PTAR_TEMPORAL_PAIR_SLOT_COUNT 3
#define PTAR_TEMPORAL_SLOT_FREE 0
#define PTAR_TEMPORAL_SLOT_WRITING 1
#define PTAR_TEMPORAL_SLOT_READY 2
#define PTAR_TEMPORAL_SLOT_DISPLAYING 3
#define PTAR_TEMPORAL_PRIME_DEPTH 2

struct PTARTemporalPairSlot
{
    volatile LONG state;
    LONG sequence;
    IDirect3DSwapChain9* swapChain;
    DWORD generatedMarker;
    DWORD realMarker;
};

struct PTARTemporalPresenterState
{
    PTARTemporalPairSlot slots[PTAR_TEMPORAL_PAIR_SLOT_COUNT];
    HANDLE readyEvent;
    HANDLE stopEvent;
    HANDLE thread;
    HWND hwnd;
    UINT width;
    UINT height;
    D3DFORMAT format;
    volatile LONG active;
    volatile LONG healthy;
    volatile LONG stopRequested;
    volatile LONG startupAttempted;
    volatile LONG writeSequence;
    volatile LONG wholePairDrops;
    volatile LONG underflowTicks;
    volatile LONG presentFailures;
    volatile LONG presentedGenerated;
    volatile LONG presentedReal;
    PTARTemporalPairScheduler scheduler;
};

static PTARTemporalPresenterState g_ptarTemporal={};
static HWND g_ptarTemporalPreferredWindow=0;

static void PtTemporalWriteState(const char* stage,HRESULT hr)
{
    if(!stage) stage="UNKNOWN";
    wchar_t path[MAX_PATH]={0};
    DWORD n=GetModuleFileNameW(g_self,path,(DWORD)_countof(path));
    if(!n || n>=_countof(path)) return;
    wchar_t* slash=wcsrchr(path,L'\\');
    if(!slash) return;
    *(slash+1)=0;
    if(wcslen(path)+wcslen(L"PTAR_D3D9_TEMPORAL_PAIR_STATE.txt")+1>=_countof(path)) return;
    wcscat_s(path,L"PTAR_D3D9_TEMPORAL_PAIR_STATE.txt");

    char body[1400]={0};
    _snprintf_s(
        body,sizeof(body),_TRUNCATE,
        "PTAR_D3D9_TEMPORAL_PAIR_STATE=1\r\n"
        "STAGE=%s\r\nHRESULT=0x%08lX\r\n"
        "ACTIVE=%ld\r\nHEALTHY=%ld\r\n"
        "WHOLE_PAIR_DROPS=%ld\r\nUNDERFLOW_TICKS=%ld\r\n"
        "PRESENT_FAILURES=%ld\r\nPRESENTED_G=%ld\r\nPRESENTED_R=%ld\r\n"
        "LATE_RESYNCS=%lu\r\n"
        "POLICY=60HZ_ABSOLUTE_QPC_NO_CATCHUP\r\n"
        "TRANSPORT=SAME_DEVICE_PAIR_SWAPCHAIN_PRESENT_ONLY_WORKER\r\n",
        stage,(unsigned long)hr,
        InterlockedCompareExchange(&g_ptarTemporal.active,0,0),
        InterlockedCompareExchange(&g_ptarTemporal.healthy,0,0),
        InterlockedCompareExchange(&g_ptarTemporal.wholePairDrops,0,0),
        InterlockedCompareExchange(&g_ptarTemporal.underflowTicks,0,0),
        InterlockedCompareExchange(&g_ptarTemporal.presentFailures,0,0),
        InterlockedCompareExchange(&g_ptarTemporal.presentedGenerated,0,0),
        InterlockedCompareExchange(&g_ptarTemporal.presentedReal,0,0),
        g_ptarTemporal.scheduler.lateResyncs);

    HANDLE f=CreateFileW(
        path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(f==INVALID_HANDLE_VALUE) return;
    DWORD written=0;
    WriteFile(f,body,(DWORD)strlen(body),&written,0);
    CloseHandle(f);
}

static bool PtTemporalOwnsDisplay()
{
    return
        InterlockedCompareExchange(&g_ptarTemporal.active,0,0)!=0 &&
        InterlockedCompareExchange(&g_ptarTemporal.healthy,0,0)!=0;
}

static int PtTemporalReadyCount()
{
    int count=0;
    for(int i=0;i<PTAR_TEMPORAL_PAIR_SLOT_COUNT;++i)
    {
        if(InterlockedCompareExchange(
            &g_ptarTemporal.slots[i].state,
            PTAR_TEMPORAL_SLOT_READY,
            PTAR_TEMPORAL_SLOT_READY)==PTAR_TEMPORAL_SLOT_READY)
            ++count;
    }
    return count;
}

static int PtTemporalAcquireWriteSlot()
{
    for(int i=0;i<PTAR_TEMPORAL_PAIR_SLOT_COUNT;++i)
    {
        if(InterlockedCompareExchange(
            &g_ptarTemporal.slots[i].state,
            PTAR_TEMPORAL_SLOT_WRITING,
            PTAR_TEMPORAL_SLOT_FREE)==PTAR_TEMPORAL_SLOT_FREE)
            return i;
    }

    int oldest=-1;
    LONG oldestSeq=0x7fffffff;
    for(int i=0;i<PTAR_TEMPORAL_PAIR_SLOT_COUNT;++i)
    {
        PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[i];
        if(InterlockedCompareExchange(
            &slot->state,
            PTAR_TEMPORAL_SLOT_READY,
            PTAR_TEMPORAL_SLOT_READY)==PTAR_TEMPORAL_SLOT_READY &&
           slot->sequence<oldestSeq)
        {
            oldest=i;
            oldestSeq=slot->sequence;
        }
    }

    if(oldest>=0 &&
       InterlockedCompareExchange(
           &g_ptarTemporal.slots[oldest].state,
           PTAR_TEMPORAL_SLOT_WRITING,
           PTAR_TEMPORAL_SLOT_READY)==PTAR_TEMPORAL_SLOT_READY)
    {
        InterlockedIncrement(&g_ptarTemporal.wholePairDrops);
        return oldest;
    }
    return -1;
}

static int PtTemporalClaimOldestReady()
{
    for(;;)
    {
        int best=-1;
        LONG bestSeq=0x7fffffff;
        for(int i=0;i<PTAR_TEMPORAL_PAIR_SLOT_COUNT;++i)
        {
            PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[i];
            if(InterlockedCompareExchange(
                &slot->state,
                PTAR_TEMPORAL_SLOT_READY,
                PTAR_TEMPORAL_SLOT_READY)==PTAR_TEMPORAL_SLOT_READY &&
               slot->sequence<bestSeq)
            {
                best=i;
                bestSeq=slot->sequence;
            }
        }
        if(best<0) return -1;
        if(InterlockedCompareExchange(
            &g_ptarTemporal.slots[best].state,
            PTAR_TEMPORAL_SLOT_DISPLAYING,
            PTAR_TEMPORAL_SLOT_READY)==PTAR_TEMPORAL_SLOT_READY)
            return best;
    }
}

static bool PtTemporalWaitUntil(LONGLONG deadlineQpc)
{
    LARGE_INTEGER now={};
    for(;;)
    {
        if(g_ptarTemporal.stopEvent &&
           WaitForSingleObject(g_ptarTemporal.stopEvent,0)==WAIT_OBJECT_0)
            return false;

        QueryPerformanceCounter(&now);
        if(now.QuadPart>=deadlineQpc)
            return true;

        SwitchToThread();
    }
}

static HRESULT PtTemporalCopyFrameToBackBuffer(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* texture,
    IDirect3DSurface9* surface,
    IDirect3DSurface9* target)
{
    if(!dev || !texture || !surface || !target)
        return D3DERR_INVALIDCALL;

    D3DSURFACE_DESC src={};
    D3DSURFACE_DESC dst={};
    HRESULT hr=surface->GetDesc(&src);
    if(SUCCEEDED(hr)) hr=target->GetDesc(&dst);
    if(SUCCEEDED(hr) &&
       src.Width==dst.Width && src.Height==dst.Height &&
       src.Format==dst.Format &&
       src.MultiSampleType==D3DMULTISAMPLE_NONE &&
       dst.MultiSampleType==D3DMULTISAMPLE_NONE)
    {
        hr=dev->StretchRect(surface,0,target,0,D3DTEXF_NONE);
        if(SUCCEEDED(hr)) return hr;
    }

    return DrawFullscreenPass(
        dev,target,g_ptar.outputW,g_ptar.outputH,
        g_ptar.bilinearShader,texture,0,0,0);
}

static HRESULT PtTemporalDrawHudToSurface(
    IDirect3DSurface9* target,
    bool generatedFrame,
    bool fgProducing,
    DWORD* markerOut)
{
    if(markerOut) *markerOut=0;
    if(!g_ptar.device || !target)
        return D3DERR_INVALIDCALL;

    IDirect3DDevice9* dev=g_ptar.device;
    HRESULT hr=dev->SetDepthStencilSurface(0);
    if(SUCCEEDED(hr)) hr=g_realSetRenderTarget(dev,0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={};
    vp.X=0;vp.Y=0;vp.Width=g_ptar.outputW;vp.Height=g_ptar.outputH;
    vp.MinZ=0.0f;vp.MaxZ=1.0f;
    hr=dev->SetViewport(&vp);
    if(FAILED(hr)) return hr;

    const PTARPresentationRect fit=PtResolutionAspectFit(
        g_ptar.sourceW,g_ptar.sourceH,g_ptar.outputW,g_ptar.outputH);
    const bool exact15=
        g_ptar.spatialActive &&
        PtResolutionExactScale15(
            g_ptar.sourceW,g_ptar.sourceH,fit.width,fit.height);

    ++g_ptarHudFrameSequence;
    const DWORD marker=(DWORD)(g_ptarHudFrameSequence&4095ul);
    hr=PtGw16RenderExactHudD3D9(
        dev,PtHudVisible(),PtHudStateNotice(),PtHudMarkerEnabled(),marker,
        generatedFrame,PtHudDisplayFps(fgProducing),
        g_ptar.sourceW,g_ptar.sourceH,
        PtHudFilterId(g_ptar.spatialActive,exact15),
        PtHudFeedbackType(),PtHudFeedbackArgA(),
        PtHudFeedbackArgB(),PtHudFeedbackArgC());
    if(SUCCEEDED(hr) && markerOut) *markerOut=marker;
    return hr;
}

static void PtTemporalReleaseSlot(PTARTemporalPairSlot* slot)
{
    if(!slot) return;
    if(slot->swapChain){slot->swapChain->Release();slot->swapChain=0;}
    slot->sequence=0;
    slot->generatedMarker=0;
    slot->realMarker=0;
    InterlockedExchange(&slot->state,PTAR_TEMPORAL_SLOT_FREE);
}

static void PtTemporalPresenterStop()
{
    InterlockedExchange(&g_ptarTemporal.stopRequested,1);
    if(g_ptarTemporal.stopEvent) SetEvent(g_ptarTemporal.stopEvent);
    if(g_ptarTemporal.readyEvent) SetEvent(g_ptarTemporal.readyEvent);

    if(g_ptarTemporal.thread)
    {
        DWORD wr=WaitForSingleObject(g_ptarTemporal.thread,3000);
        if(wr==WAIT_OBJECT_0)
        {
            CloseHandle(g_ptarTemporal.thread);
            g_ptarTemporal.thread=0;
        }
        else
        {
            PtDiagLogA("TEMPORAL_PRESENTER_STOP_TIMEOUT wait=%lu",(unsigned long)wr);
            return;
        }
    }

    for(int i=0;i<PTAR_TEMPORAL_PAIR_SLOT_COUNT;++i)
        PtTemporalReleaseSlot(&g_ptarTemporal.slots[i]);

    if(g_ptarTemporal.readyEvent){CloseHandle(g_ptarTemporal.readyEvent);g_ptarTemporal.readyEvent=0;}
    if(g_ptarTemporal.stopEvent){CloseHandle(g_ptarTemporal.stopEvent);g_ptarTemporal.stopEvent=0;}

    PtTemporalSchedulerStop(&g_ptarTemporal.scheduler);
    InterlockedExchange(&g_ptarTemporal.active,0);
    InterlockedExchange(&g_ptarTemporal.healthy,0);
    InterlockedExchange(&g_ptarTemporal.stopRequested,0);
    InterlockedExchange(&g_ptarTemporal.startupAttempted,0);
    InterlockedExchange(&g_ptarTemporal.writeSequence,0);
    InterlockedExchange(&g_ptarTemporal.wholePairDrops,0);
    InterlockedExchange(&g_ptarTemporal.underflowTicks,0);
    InterlockedExchange(&g_ptarTemporal.presentFailures,0);
    InterlockedExchange(&g_ptarTemporal.presentedGenerated,0);
    InterlockedExchange(&g_ptarTemporal.presentedReal,0);
    g_ptarTemporal.hwnd=0;
    g_ptarTemporal.width=0;
    g_ptarTemporal.height=0;
    g_ptarTemporal.format=D3DFMT_UNKNOWN;
}

static HRESULT PtTemporalCreatePairSwapChain(PTARTemporalPairSlot* slot)
{
    if(!slot || !g_ptar.device || !g_ptarTemporal.hwnd)
        return D3DERR_INVALIDCALL;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=g_ptar.outputW;
    pp.BackBufferHeight=g_ptar.outputH;
    pp.BackBufferFormat=g_ptar.outputFormat;
    pp.BackBufferCount=2;
    pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality=0;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=g_ptarTemporal.hwnd;
    pp.Windowed=TRUE;
    pp.EnableAutoDepthStencil=FALSE;
    pp.Flags=0;
    pp.FullScreen_RefreshRateInHz=0;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    return g_ptar.device->CreateAdditionalSwapChain(&pp,&slot->swapChain);
}

static DWORD WINAPI PtTemporalPresenterThread(LPVOID)
{
    LARGE_INTEGER freq={};
    QueryPerformanceFrequency(&freq);
    PtTemporalSchedulerInit(&g_ptarTemporal.scheduler,freq.QuadPart,60);

    while(InterlockedCompareExchange(&g_ptarTemporal.stopRequested,0,0)==0 &&
          PtTemporalReadyCount()<PTAR_TEMPORAL_PRIME_DEPTH)
    {
        if(g_ptarTemporal.readyEvent)
        {
            HANDLE waits[2]={g_ptarTemporal.stopEvent,g_ptarTemporal.readyEvent};
            DWORD wr=WaitForMultipleObjects(2,waits,FALSE,50);
            if(wr==WAIT_OBJECT_0) return 0;
        }
        else
        {
            SwitchToThread();
        }
    }

    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    PtTemporalSchedulerStart(&g_ptarTemporal.scheduler,now.QuadPart);
    PtTemporalWriteState("RUNNING_PRIMED",S_OK);

    int current=-1;
    int phase=0;

    while(InterlockedCompareExchange(&g_ptarTemporal.stopRequested,0,0)==0)
    {
        if(!PtTemporalWaitUntil(g_ptarTemporal.scheduler.nextDeadlineQpc))
            break;

        if(current<0)
        {
            current=PtTemporalClaimOldestReady();
            phase=0;
        }

        if(current<0)
        {
            InterlockedIncrement(&g_ptarTemporal.underflowTicks);
            QueryPerformanceCounter(&now);
            PtTemporalSchedulerAdvanceAfterTick(&g_ptarTemporal.scheduler,now.QuadPart);
            continue;
        }

        PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[current];
        const bool generated=(phase==0);
        const DWORD marker=generated?slot->generatedMarker:slot->realMarker;

        HRESULT hr=slot->swapChain->Present(0,0,0,0,0);
        QueryPerformanceCounter(&now);
        if(FAILED(hr))
        {
            InterlockedIncrement(&g_ptarTemporal.presentFailures);
            InterlockedExchange(&g_ptarTemporal.healthy,0);
            InterlockedExchange(&g_ptarTemporal.active,0);
            PtDiagLogA("TEMPORAL_PRESENT_FAIL phase=%s hr=0x%08lX",
                generated?"G":"R",(unsigned long)hr);
            PtTemporalWriteState("PRESENT_FAIL",hr);
            InterlockedExchange(&slot->state,PTAR_TEMPORAL_SLOT_FREE);
            break;
        }

        PtFgPacerRecordVisible(generated);
        PtD3D9DiagRecordPresent(
            generated,marker,g_ptarFgPacer.lastVisibleQpc,
            PtFgPacerRealCount(),PtFgPacerGeneratedCount(),
            g_ptarFgPacer.resyncs,PtFgPacerLateSkipCount());
        if(generated)
            InterlockedIncrement(&g_ptarTemporal.presentedGenerated);
        else
            InterlockedIncrement(&g_ptarTemporal.presentedReal);

        PtTemporalSchedulerAdvanceAfterTick(&g_ptarTemporal.scheduler,now.QuadPart);

        if(phase==0)
        {
            phase=1;
        }
        else
        {
            InterlockedExchange(&slot->state,PTAR_TEMPORAL_SLOT_FREE);
            current=-1;
            phase=0;
        }
    }

    InterlockedExchange(&g_ptarTemporal.active,0);
    InterlockedExchange(&g_ptarTemporal.healthy,0);
    PtTemporalWriteState("WORKER_EXIT",S_OK);
    return 0;
}

static bool PtTemporalPresenterStart(HWND hwnd)
{
    if(PtTemporalOwnsDisplay())
        return true;
    if(InterlockedCompareExchange(&g_ptarTemporal.startupAttempted,1,0)!=0)
        return false;

    if(hwnd) g_ptarTemporalPreferredWindow=hwnd;
    if(!g_ptar.device || !g_ptarTemporalPreferredWindow)
    {
        PtTemporalWriteState("START_UNAVAILABLE",D3DERR_INVALIDCALL);
        return false;
    }

    g_ptarTemporal.hwnd=g_ptarTemporalPreferredWindow;
    g_ptarTemporal.width=g_ptar.outputW;
    g_ptarTemporal.height=g_ptar.outputH;
    g_ptarTemporal.format=g_ptar.outputFormat;
    g_ptarTemporal.readyEvent=CreateEventW(0,FALSE,FALSE,0);
    g_ptarTemporal.stopEvent=CreateEventW(0,TRUE,FALSE,0);
    if(!g_ptarTemporal.readyEvent || !g_ptarTemporal.stopEvent)
    {
        PtTemporalWriteState("EVENT_CREATE_FAIL",HRESULT_FROM_WIN32(GetLastError()));
        PtTemporalPresenterStop();
        InterlockedExchange(&g_ptarTemporal.startupAttempted,1);
        return false;
    }

    for(int i=0;i<PTAR_TEMPORAL_PAIR_SLOT_COUNT;++i)
    {
        HRESULT hr=PtTemporalCreatePairSwapChain(&g_ptarTemporal.slots[i]);
        if(FAILED(hr) || !g_ptarTemporal.slots[i].swapChain)
        {
            PtDiagLogA("TEMPORAL_SWAPCHAIN_CREATE_FAIL slot=%d hr=0x%08lX",i,(unsigned long)hr);
            PtTemporalWriteState("SWAPCHAIN_CREATE_FAIL",hr);
            PtTemporalPresenterStop();
            InterlockedExchange(&g_ptarTemporal.startupAttempted,1);
            return false;
        }
        InterlockedExchange(&g_ptarTemporal.slots[i].state,PTAR_TEMPORAL_SLOT_FREE);
    }

    InterlockedExchange(&g_ptarTemporal.healthy,1);
    InterlockedExchange(&g_ptarTemporal.active,1);
    g_ptarTemporal.thread=CreateThread(0,0,PtTemporalPresenterThread,0,0,0);
    if(!g_ptarTemporal.thread)
    {
        const HRESULT hr=HRESULT_FROM_WIN32(GetLastError());
        PtTemporalWriteState("THREAD_CREATE_FAIL",hr);
        InterlockedExchange(&g_ptarTemporal.active,0);
        InterlockedExchange(&g_ptarTemporal.healthy,0);
        PtTemporalPresenterStop();
        InterlockedExchange(&g_ptarTemporal.startupAttempted,1);
        return false;
    }

    PtDiagLogA(
        "TEMPORAL_PRESENTER_START pair_slots=%d pair_buffers=G,R cadence=60Hz no_catchup=1 transport=additional_swapchain_present_only",
        PTAR_TEMPORAL_PAIR_SLOT_COUNT);
    PtTemporalWriteState("START_OK",S_OK);
    return true;
}

static bool PtTemporalSubmitPair(
    IDirect3DTexture9* realTexture,
    IDirect3DSurface9* realSurface,
    IDirect3DTexture9* generatedTexture,
    IDirect3DSurface9* generatedSurface)
{
    if(!PtTemporalOwnsDisplay()) return false;
    if(!realTexture || !realSurface || !generatedTexture || !generatedSurface)
        return false;

    const int i=PtTemporalAcquireWriteSlot();
    if(i<0)
    {
        InterlockedIncrement(&g_ptarTemporal.wholePairDrops);
        return true;
    }

    PTARTemporalPairSlot* slot=&g_ptarTemporal.slots[i];
    IDirect3DSurface9* backG=0;
    IDirect3DSurface9* backR=0;
    HRESULT hr=slot->swapChain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&backG);
    if(SUCCEEDED(hr))
        hr=slot->swapChain->GetBackBuffer(1,D3DBACKBUFFER_TYPE_MONO,&backR);

    if(SUCCEEDED(hr))
        hr=PtTemporalCopyFrameToBackBuffer(
            g_ptar.device,generatedTexture,generatedSurface,backG);
    if(SUCCEEDED(hr))
        hr=PtTemporalDrawHudToSurface(backG,true,true,&slot->generatedMarker);
    if(SUCCEEDED(hr))
        hr=PtTemporalCopyFrameToBackBuffer(
            g_ptar.device,realTexture,realSurface,backR);
    if(SUCCEEDED(hr))
        hr=PtTemporalDrawHudToSurface(backR,false,true,&slot->realMarker);

    if(SUCCEEDED(hr) && PtCaptureConsumeRequest())
    {
        wchar_t saved[MAX_PATH]={0};
        HRESULT captureHr=PtCaptureSavePostOverlayBmp(
            g_ptar.device,backR,g_self,saved,_countof(saved));
        if(SUCCEEDED(captureHr))
        {
            PtHudNotifyCaptureSaved();
            PtDiagLogA("OK: F9 temporal-pair screenshot saved path=%ls",saved);
        }
        else
        {
            PtDiagLogA("ERROR: F9 temporal-pair screenshot failed hr=0x%08lX",(unsigned long)captureHr);
        }
    }

    if(backR) backR->Release();
    if(backG) backG->Release();

    if(FAILED(hr))
    {
        InterlockedExchange(&slot->state,PTAR_TEMPORAL_SLOT_FREE);
        InterlockedIncrement(&g_ptarTemporal.wholePairDrops);
        PtDiagLogA("TEMPORAL_PAIR_BUILD_FAIL slot=%d hr=0x%08lX",i,(unsigned long)hr);
        return true;
    }

    slot->sequence=InterlockedIncrement(&g_ptarTemporal.writeSequence);
    MemoryBarrier();
    InterlockedExchange(&slot->state,PTAR_TEMPORAL_SLOT_READY);
    if(g_ptarTemporal.readyEvent) SetEvent(g_ptarTemporal.readyEvent);
    return true;
}
