#pragma once

// D3D9 adaptation of the already validated D3D11 GW12->GW16 topology:
// - game/source device never waits on display cadence;
// - a separate display device owns presentation to the game HWND;
// - source publishes completed REAL/(optional) GENERATED pairs through a
//   latest-frame mailbox;
// - display presents REAL then GENERATED with one VBlank each;
// - FG OFF keeps the presenter armed and publishes REAL_ONLY, matching the
//   D3D11 LOADSHEDSOFT1 ownership contract;
// - no PairBal2 wait, Sleep, WaitForVBlank or DwmFlush is used in the active
//   presenter path.
//
// Windows 8.1 provides D3D9Ex FlipEx and Vista+ D3D9 shared resources. The
// producer resources are created on the regular D3D9 game device and opened by
// the isolated D3D9Ex presenter device through the existing pSharedHandle API.

#define PTAR_D3D9_ISO_SLOT_COUNT 3
#define PTAR_D3D9_ISO_FREE 0
#define PTAR_D3D9_ISO_WRITING 1
#define PTAR_D3D9_ISO_READY 2
#define PTAR_D3D9_ISO_DISPLAYING 3

struct PTARD3D9IsoSlot
{
    volatile LONG state;
    LONG sequence;
    BOOL hasGenerated;
    DWORD realMarker;
    DWORD generatedMarker;
    HANDLE realHandle;
    HANDLE generatedHandle;
    IDirect3DTexture9* realTexture;
    IDirect3DSurface9* realSurface;
    IDirect3DTexture9* generatedTexture;
    IDirect3DSurface9* generatedSurface;
    IDirect3DQuery9* producerFence;
};

struct PTARD3D9IsoPresenter
{
    HANDLE readyEvent;
    HANDLE initEvent;
    HANDLE thread;
    HWND hwnd;
    UINT width;
    UINT height;
    D3DFORMAT format;
    volatile LONG stopRequested;
    volatile LONG active;
    volatile LONG healthy;
    volatile LONG writeSequence;
    volatile LONG mailboxDrops;
    volatile LONG presentFailures;
    volatile LONG flipExActive;
    PTARD3D9IsoSlot slots[PTAR_D3D9_ISO_SLOT_COUNT];
};

static PTARD3D9IsoPresenter g_ptarD3D9Iso={};
static HWND g_ptarD3D9IsoPreferredWindow=0;

static void PtD3D9IsoReleaseProducerSlot(PTARD3D9IsoSlot* slot)
{
    if(!slot) return;
    if(slot->producerFence){slot->producerFence->Release();slot->producerFence=0;}
    if(slot->generatedSurface){slot->generatedSurface->Release();slot->generatedSurface=0;}
    if(slot->generatedTexture){slot->generatedTexture->Release();slot->generatedTexture=0;}
    if(slot->realSurface){slot->realSurface->Release();slot->realSurface=0;}
    if(slot->realTexture){slot->realTexture->Release();slot->realTexture=0;}
    slot->realHandle=0;
    slot->generatedHandle=0;
    slot->hasGenerated=FALSE;
    slot->realMarker=0;
    slot->generatedMarker=0;
    slot->sequence=0;
    InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);
}

static HRESULT PtD3D9IsoCreateSharedTexture(
    IDirect3DDevice9* dev,
    UINT width,
    UINT height,
    D3DFORMAT format,
    IDirect3DTexture9** texture,
    IDirect3DSurface9** surface,
    HANDLE* sharedHandle)
{
    if(!dev || !texture || !surface || !sharedHandle)
        return D3DERR_INVALIDCALL;

    *texture=0;
    *surface=0;
    *sharedHandle=0;

    HRESULT hr=dev->CreateTexture(
        width,height,1,D3DUSAGE_RENDERTARGET,
        format,D3DPOOL_DEFAULT,texture,sharedHandle);
    if(FAILED(hr) || !*texture || !*sharedHandle)
    {
        if(*texture){(*texture)->Release();*texture=0;}
        *sharedHandle=0;
        return FAILED(hr)?hr:E_FAIL;
    }

    hr=(*texture)->GetSurfaceLevel(0,surface);
    if(FAILED(hr) || !*surface)
    {
        (*texture)->Release();
        *texture=0;
        *sharedHandle=0;
        return FAILED(hr)?hr:E_FAIL;
    }
    return S_OK;
}

static bool PtD3D9IsoCreateProducerMailbox()
{
    IDirect3DDevice9* dev=g_ptar.device;
    if(!dev || !g_ptar.outputW || !g_ptar.outputH)
        return false;

    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
        HRESULT hr=PtD3D9IsoCreateSharedTexture(
            dev,g_ptar.outputW,g_ptar.outputH,g_ptar.outputFormat,
            &slot->realTexture,&slot->realSurface,&slot->realHandle);
        if(FAILED(hr))
        {
            PtDiagLogA("ISO_PRESENTER_SHARED_REAL_CREATE_FAIL slot=%d hr=0x%08lX",i,(unsigned long)hr);
            return false;
        }

        hr=PtD3D9IsoCreateSharedTexture(
            dev,g_ptar.outputW,g_ptar.outputH,g_ptar.outputFormat,
            &slot->generatedTexture,&slot->generatedSurface,&slot->generatedHandle);
        if(FAILED(hr))
        {
            PtDiagLogA("ISO_PRESENTER_SHARED_GEN_CREATE_FAIL slot=%d hr=0x%08lX",i,(unsigned long)hr);
            return false;
        }

        hr=dev->CreateQuery(D3DQUERYTYPE_EVENT,&slot->producerFence);
        if(FAILED(hr) || !slot->producerFence)
        {
            PtDiagLogA("ISO_PRESENTER_FENCE_CREATE_FAIL slot=%d hr=0x%08lX",i,(unsigned long)hr);
            return false;
        }
        InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);
    }
    return true;
}

static HRESULT PtD3D9IsoDrawHudToSurface(
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
    if(FAILED(hr)) return hr;

    hr=g_realSetRenderTarget(dev,0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={};
    vp.X=0;vp.Y=0;
    vp.Width=g_ptar.outputW;
    vp.Height=g_ptar.outputH;
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
        dev,
        PtHudVisible(),
        PtHudStateNotice(),
        PtHudMarkerEnabled(),
        marker,
        generatedFrame,
        PtHudDisplayFps(fgProducing),
        g_ptar.sourceW,
        g_ptar.sourceH,
        PtHudFilterId(g_ptar.spatialActive,exact15),
        PtHudFeedbackType(),
        PtHudFeedbackArgA(),
        PtHudFeedbackArgB(),
        PtHudFeedbackArgC());

    if(SUCCEEDED(hr) && markerOut)
        *markerOut=marker;

    if(PtHudConsumeStatusLogPending())
    {
        PtDiagLogA(
            "STATUS_RUNTIME fg=%s profile=%d src=%ux%u out=%ux%u "
            "filter=%d real_fps=%.3f visible_fps=%.3f "
            "real_count=%lu gen_count=%lu presenter_drops=%ld presenter_failures=%ld",
            PtHudFgEnabled()?"ON":"OFF",
            PtHudFgProfile(),
            g_ptar.sourceW,g_ptar.sourceH,
            g_ptar.outputW,g_ptar.outputH,
            PtHudFilterId(g_ptar.spatialActive,exact15),
            PtHudRealFps(),PtHudDisplayFps(fgProducing),
            PtFgPacerRealCount(),PtFgPacerGeneratedCount(),
            g_ptarD3D9Iso.mailboxDrops,g_ptarD3D9Iso.presentFailures);
    }

    const LONGLONG now=PtHudNow();
    if(g_ptarHudLastFpsLogQpc==0 ||
       now-g_ptarHudLastFpsLogQpc>=g_ptarHudFreq.QuadPart)
    {
        g_ptarHudLastFpsLogQpc=now;
        PtDiagLogA(
            "FPS_WALLCLOCK_SAMPLE real_fps=%.3f visible_fps=%.3f fg=%s "
            "presenter=ISOLATED_D3D9EX_FLIP mailbox_drops=%ld",
            PtHudRealFps(),PtHudDisplayFps(fgProducing),
            PtHudFgEnabled()?"ON":"OFF",
            g_ptarD3D9Iso.mailboxDrops);
    }

    return hr;
}

static bool PtD3D9IsoFenceProducer(PTARD3D9IsoSlot* slot)
{
    if(!slot || !slot->producerFence)
        return false;

    HRESULT hr=slot->producerFence->Issue(D3DISSUE_END);
    if(FAILED(hr))
        return false;

    const DWORD start=GetTickCount();
    for(;;)
    {
        hr=slot->producerFence->GetData(0,0,D3DGETDATA_FLUSH);
        if(hr==S_OK)
            return true;
        if(FAILED(hr))
            return false;
        if((DWORD)(GetTickCount()-start)>=8ul)
            return false;
        SwitchToThread();
    }
}

static int PtD3D9IsoAcquireWriteSlot()
{
    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        if(InterlockedCompareExchange(
            &g_ptarD3D9Iso.slots[i].state,
            PTAR_D3D9_ISO_WRITING,
            PTAR_D3D9_ISO_FREE)==PTAR_D3D9_ISO_FREE)
            return i;
    }

    int oldest=-1;
    LONG oldestSeq=0x7fffffff;
    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
        if(slot->state==PTAR_D3D9_ISO_READY && slot->sequence<oldestSeq)
        {
            oldest=i;
            oldestSeq=slot->sequence;
        }
    }

    if(oldest>=0 &&
       InterlockedCompareExchange(
           &g_ptarD3D9Iso.slots[oldest].state,
           PTAR_D3D9_ISO_WRITING,
           PTAR_D3D9_ISO_READY)==PTAR_D3D9_ISO_READY)
    {
        InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
        return oldest;
    }

    return -1;
}

static int PtD3D9IsoClaimLatestReady()
{
    for(;;)
    {
        int best=-1;
        LONG bestSeq=-1;
        for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
        {
            PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
            if(slot->state==PTAR_D3D9_ISO_READY && slot->sequence>bestSeq)
            {
                best=i;
                bestSeq=slot->sequence;
            }
        }
        if(best<0)
            return -1;

        if(InterlockedCompareExchange(
            &g_ptarD3D9Iso.slots[best].state,
            PTAR_D3D9_ISO_DISPLAYING,
            PTAR_D3D9_ISO_READY)==PTAR_D3D9_ISO_READY)
        {
            for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
            {
                if(i==best) continue;
                PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
                if(slot->state==PTAR_D3D9_ISO_READY && slot->sequence<bestSeq)
                {
                    if(InterlockedCompareExchange(
                        &slot->state,
                        PTAR_D3D9_ISO_FREE,
                        PTAR_D3D9_ISO_READY)==PTAR_D3D9_ISO_READY)
                        InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
                }
            }
            return best;
        }
    }
}

static HRESULT PtD3D9IsoOpenSharedTexture(
    IDirect3DDevice9Ex* dev,
    HANDLE sharedHandle,
    IDirect3DTexture9** texture,
    IDirect3DSurface9** surface)
{
    if(!dev || !sharedHandle || !texture || !surface)
        return D3DERR_INVALIDCALL;
    *texture=0;
    *surface=0;
    HANDLE openHandle=sharedHandle;
    HRESULT hr=dev->CreateTexture(
        g_ptarD3D9Iso.width,
        g_ptarD3D9Iso.height,
        1,D3DUSAGE_RENDERTARGET,
        g_ptarD3D9Iso.format,
        D3DPOOL_DEFAULT,
        texture,&openHandle);
    if(FAILED(hr) || !*texture)
        return FAILED(hr)?hr:E_FAIL;
    hr=(*texture)->GetSurfaceLevel(0,surface);
    if(FAILED(hr) || !*surface)
    {
        (*texture)->Release();
        *texture=0;
        return FAILED(hr)?hr:E_FAIL;
    }
    return S_OK;
}

static HRESULT PtD3D9IsoPresentOne(
    IDirect3DDevice9Ex* dev,
    IDirect3DSurface9* source,
    bool generated,
    DWORD marker)
{
    if(!dev || !source)
        return D3DERR_INVALIDCALL;

    IDirect3DSurface9* back=0;
    HRESULT hr=dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(FAILED(hr) || !back)
        return FAILED(hr)?hr:E_FAIL;

    hr=dev->StretchRect(source,0,back,0,D3DTEXF_NONE);
    back->Release();
    if(FAILED(hr))
        return hr;

    hr=dev->PresentEx(0,0,0,0,0);
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
    }
    return hr;
}

static DWORD WINAPI PtD3D9IsoPresenterThread(LPVOID)
{
    IDirect3D9Ex* d3d=0;
    IDirect3DDevice9Ex* dev=0;
    IDirect3DTexture9* realTex[PTAR_D3D9_ISO_SLOT_COUNT]={};
    IDirect3DSurface9* realSurf[PTAR_D3D9_ISO_SLOT_COUNT]={};
    IDirect3DTexture9* genTex[PTAR_D3D9_ISO_SLOT_COUNT]={};
    IDirect3DSurface9* genSurf[PTAR_D3D9_ISO_SLOT_COUNT]={};

    HRESULT hr=g_sysDirect3DCreate9Ex?
        g_sysDirect3DCreate9Ex(D3D_SDK_VERSION,&d3d):E_NOTIMPL;
    if(FAILED(hr) || !d3d)
    {
        PtDiagLogA("ISO_PRESENTER_CREATE9EX_FAIL hr=0x%08lX",(unsigned long)hr);
        SetEvent(g_ptarD3D9Iso.initEvent);
        return 1;
    }

    D3DDISPLAYMODE dm={};
    hr=d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&dm);
    if(FAILED(hr))
        dm.Format=D3DFMT_X8R8G8B8;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=g_ptarD3D9Iso.width;
    pp.BackBufferHeight=g_ptarD3D9Iso.height;
    pp.BackBufferFormat=dm.Format;
    pp.BackBufferCount=3;
    pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality=0;
    pp.SwapEffect=D3DSWAPEFFECT_FLIPEX;
    pp.hDeviceWindow=g_ptarD3D9Iso.hwnd;
    pp.Windowed=TRUE;
    pp.EnableAutoDepthStencil=FALSE;
    pp.Flags=0;
    pp.FullScreen_RefreshRateInHz=0;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

    DWORD behavior=
        D3DCREATE_HARDWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED|
        D3DCREATE_FPU_PRESERVE;

    hr=d3d->CreateDeviceEx(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,
        g_ptarD3D9Iso.hwnd,behavior,&pp,0,&dev);

    if(FAILED(hr) || !dev)
    {
        PtDiagLogA("ISO_PRESENTER_FLIPEX_CREATE_FAIL hr=0x%08lX retry=DISCARD",(unsigned long)hr);
        pp.BackBufferCount=1;
        pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        behavior=
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|
            D3DCREATE_MULTITHREADED|
            D3DCREATE_FPU_PRESERVE;
        hr=d3d->CreateDeviceEx(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,
            g_ptarD3D9Iso.hwnd,behavior,&pp,0,&dev);
        InterlockedExchange(&g_ptarD3D9Iso.flipExActive,0);
    }
    else
    {
        InterlockedExchange(&g_ptarD3D9Iso.flipExActive,1);
    }

    if(FAILED(hr) || !dev)
    {
        PtDiagLogA("ISO_PRESENTER_DEVICE_CREATE_FAIL hr=0x%08lX",(unsigned long)hr);
        d3d->Release();
        SetEvent(g_ptarD3D9Iso.initEvent);
        return 2;
    }

    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        hr=PtD3D9IsoOpenSharedTexture(
            dev,g_ptarD3D9Iso.slots[i].realHandle,
            &realTex[i],&realSurf[i]);
        if(FAILED(hr))
            break;
        hr=PtD3D9IsoOpenSharedTexture(
            dev,g_ptarD3D9Iso.slots[i].generatedHandle,
            &genTex[i],&genSurf[i]);
        if(FAILED(hr))
            break;
    }

    if(FAILED(hr))
    {
        PtDiagLogA("ISO_PRESENTER_SHARED_OPEN_FAIL hr=0x%08lX",(unsigned long)hr);
        for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
        {
            if(genSurf[i]) genSurf[i]->Release();
            if(genTex[i]) genTex[i]->Release();
            if(realSurf[i]) realSurf[i]->Release();
            if(realTex[i]) realTex[i]->Release();
        }
        dev->Release();
        d3d->Release();
        SetEvent(g_ptarD3D9Iso.initEvent);
        return 3;
    }

    InterlockedExchange(&g_ptarD3D9Iso.healthy,1);
    InterlockedExchange(&g_ptarD3D9Iso.active,1);
    PtDiagLogA(
        "ISO_PRESENTER_READY api=D3D9EX mode=%s interval=ONE size=%ux%u mailbox=%d order=REAL_THEN_GENERATED",
        g_ptarD3D9Iso.flipExActive?"FLIPEX":"DISCARD",
        g_ptarD3D9Iso.width,g_ptarD3D9Iso.height,
        PTAR_D3D9_ISO_SLOT_COUNT);
    SetEvent(g_ptarD3D9Iso.initEvent);

    while(InterlockedCompareExchange(&g_ptarD3D9Iso.stopRequested,0,0)==0)
    {
        int slotIndex=PtD3D9IsoClaimLatestReady();
        if(slotIndex<0)
        {
            WaitForSingleObject(g_ptarD3D9Iso.readyEvent,50);
            continue;
        }

        PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[slotIndex];
        hr=PtD3D9IsoPresentOne(
            dev,realSurf[slotIndex],false,slot->realMarker);
        if(SUCCEEDED(hr) && slot->hasGenerated)
        {
            hr=PtD3D9IsoPresentOne(
                dev,genSurf[slotIndex],true,slot->generatedMarker);
        }

        InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);

        if(FAILED(hr))
        {
            InterlockedIncrement(&g_ptarD3D9Iso.presentFailures);
            InterlockedExchange(&g_ptarD3D9Iso.healthy,0);
            PtDiagLogA("ISO_PRESENTER_PRESENT_FAIL hr=0x%08lX",(unsigned long)hr);
            break;
        }
    }

    InterlockedExchange(&g_ptarD3D9Iso.active,0);
    InterlockedExchange(&g_ptarD3D9Iso.healthy,0);

    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        if(genSurf[i]) genSurf[i]->Release();
        if(genTex[i]) genTex[i]->Release();
        if(realSurf[i]) realSurf[i]->Release();
        if(realTex[i]) realTex[i]->Release();
    }
    dev->Release();
    d3d->Release();
    PtDiagLogA("ISO_PRESENTER_THREAD_EXIT");
    return 0;
}

static void PtD3D9IsolatedPresenterStop()
{
    InterlockedExchange(&g_ptarD3D9Iso.stopRequested,1);
    if(g_ptarD3D9Iso.readyEvent)
        SetEvent(g_ptarD3D9Iso.readyEvent);

    if(g_ptarD3D9Iso.thread)
    {
        DWORD wr=WaitForSingleObject(g_ptarD3D9Iso.thread,3000);
        if(wr!=WAIT_OBJECT_0)
        {
            PtDiagLogA("ISO_PRESENTER_STOP_TIMEOUT wait=%lu",(unsigned long)wr);
            return;
        }
        CloseHandle(g_ptarD3D9Iso.thread);
        g_ptarD3D9Iso.thread=0;
    }

    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
        PtD3D9IsoReleaseProducerSlot(&g_ptarD3D9Iso.slots[i]);

    if(g_ptarD3D9Iso.readyEvent){CloseHandle(g_ptarD3D9Iso.readyEvent);g_ptarD3D9Iso.readyEvent=0;}
    if(g_ptarD3D9Iso.initEvent){CloseHandle(g_ptarD3D9Iso.initEvent);g_ptarD3D9Iso.initEvent=0;}

    InterlockedExchange(&g_ptarD3D9Iso.active,0);
    InterlockedExchange(&g_ptarD3D9Iso.healthy,0);
    InterlockedExchange(&g_ptarD3D9Iso.stopRequested,0);
    InterlockedExchange(&g_ptarD3D9Iso.writeSequence,0);
    InterlockedExchange(&g_ptarD3D9Iso.mailboxDrops,0);
    InterlockedExchange(&g_ptarD3D9Iso.presentFailures,0);
    InterlockedExchange(&g_ptarD3D9Iso.flipExActive,0);
    g_ptarD3D9Iso.hwnd=0;
    g_ptarD3D9Iso.width=0;
    g_ptarD3D9Iso.height=0;
    g_ptarD3D9Iso.format=D3DFMT_UNKNOWN;
}

static bool PtD3D9IsolatedPresenterStart(HWND hwnd)
{
    PtD3D9IsolatedPresenterStop();

    if(hwnd)
        g_ptarD3D9IsoPreferredWindow=hwnd;
    if(!g_ptarD3D9IsoPreferredWindow || !g_ptar.device || !g_sysDirect3DCreate9Ex)
    {
        PtDiagLogA("ISO_PRESENTER_START_UNAVAILABLE hwnd=%p dev=%p create9ex=%p",
            g_ptarD3D9IsoPreferredWindow,g_ptar.device,(void*)g_sysDirect3DCreate9Ex);
        return false;
    }

    g_ptarD3D9Iso.hwnd=g_ptarD3D9IsoPreferredWindow;
    g_ptarD3D9Iso.width=g_ptar.outputW;
    g_ptarD3D9Iso.height=g_ptar.outputH;
    g_ptarD3D9Iso.format=g_ptar.outputFormat;

    g_ptarD3D9Iso.readyEvent=CreateEventW(0,FALSE,FALSE,0);
    g_ptarD3D9Iso.initEvent=CreateEventW(0,TRUE,FALSE,0);
    if(!g_ptarD3D9Iso.readyEvent || !g_ptarD3D9Iso.initEvent)
    {
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    if(!PtD3D9IsoCreateProducerMailbox())
    {
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    g_ptarD3D9Iso.thread=CreateThread(
        0,0,PtD3D9IsoPresenterThread,0,0,0);
    if(!g_ptarD3D9Iso.thread)
    {
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    DWORD wr=WaitForSingleObject(g_ptarD3D9Iso.initEvent,5000);
    if(wr!=WAIT_OBJECT_0 ||
       InterlockedCompareExchange(&g_ptarD3D9Iso.healthy,0,0)==0)
    {
        PtDiagLogA("ISO_PRESENTER_START_FAIL wait=%lu healthy=%ld",
            (unsigned long)wr,g_ptarD3D9Iso.healthy);
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    return true;
}

static bool PtD3D9IsolatedPresenterOwnsDisplay()
{
    return
        InterlockedCompareExchange(&g_ptarD3D9Iso.active,0,0)!=0 &&
        InterlockedCompareExchange(&g_ptarD3D9Iso.healthy,0,0)!=0;
}

static bool PtD3D9IsolatedPresenterSubmit(
    IDirect3DSurface9* realSurface,
    IDirect3DSurface9* generatedSurface,
    bool hasGenerated)
{
    if(!PtD3D9IsolatedPresenterOwnsDisplay())
        return false;
    if(!realSurface)
        return true;

    const int i=PtD3D9IsoAcquireWriteSlot();
    if(i<0)
    {
        InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
        return true;
    }

    PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
    HRESULT hr=g_ptar.device->StretchRect(
        realSurface,0,slot->realSurface,0,D3DTEXF_NONE);
    if(SUCCEEDED(hr))
        hr=PtD3D9IsoDrawHudToSurface(
            slot->realSurface,false,hasGenerated,&slot->realMarker);

    slot->hasGenerated=hasGenerated?TRUE:FALSE;
    slot->generatedMarker=0;

    if(SUCCEEDED(hr) && hasGenerated && generatedSurface)
    {
        hr=g_ptar.device->StretchRect(
            generatedSurface,0,slot->generatedSurface,0,D3DTEXF_NONE);
        if(SUCCEEDED(hr))
            hr=PtD3D9IsoDrawHudToSurface(
                slot->generatedSurface,true,true,&slot->generatedMarker);
    }

    if(SUCCEEDED(hr) && PtCaptureConsumeRequest())
    {
        wchar_t saved[MAX_PATH]={0};
        HRESULT captureHr=PtCaptureSavePostOverlayBmp(
            g_ptar.device,slot->realSurface,g_self,
            saved,_countof(saved));
        if(SUCCEEDED(captureHr))
        {
            PtHudNotifyCaptureSaved();
            PtDiagLogA("OK: F9 isolated-presenter mailbox screenshot saved path=%ls",saved);
        }
        else
        {
            PtDiagLogA("ERROR: F9 isolated-presenter mailbox screenshot failed hr=0x%08lX",(unsigned long)captureHr);
        }
    }

    if(SUCCEEDED(hr) && !PtD3D9IsoFenceProducer(slot))
    {
        PtDiagLogA("ISO_PRESENTER_PRODUCER_FENCE_TIMEOUT slot=%d",i);
        hr=E_FAIL;
    }

    if(FAILED(hr))
    {
        InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);
        InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
        return true;
    }

    slot->sequence=InterlockedIncrement(&g_ptarD3D9Iso.writeSequence);
    MemoryBarrier();
    InterlockedExchange(&slot->state,PTAR_D3D9_ISO_READY);
    SetEvent(g_ptarD3D9Iso.readyEvent);
    return true;
}
