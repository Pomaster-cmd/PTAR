#pragma once

#include <windows.h>
#include <d3d9.h>

// D3D9 implementation of the production D3D11 presenter principles:
//
//   game/source device  -> shared REAL ring -> isolated presenter device
//                                           -> async shader FG
//                                           -> GENERATED / REAL Sync1 presents
//
// The game thread never performs the physical display Present and never runs
// the FG motion/interpolation passes. This is the important GW16I invariant:
// presentation and GENERATED work live off the game's critical render path.
//
// D3D9 API-specific adaptation:
// - both devices are IDirect3DDevice9Ex because D3D9 shared handles are an Ex
//   feature;
// - the game still receives the base IDirect3DDevice9 interface;
// - the presenter owns its own swapchain/device and blocks on Sync1 there;
// - shared-resource producer completion uses D3DQUERYTYPE_EVENT + FLUSH.

typedef HRESULT (WINAPI *PTARIsoPFN_Direct3DCreate9Ex)(
    UINT,IDirect3D9Ex**);

enum PTARIsoSlotState
{
    PTAR_ISO_FREE=0,
    PTAR_ISO_WRITING=1,
    PTAR_ISO_READY=2,
    PTAR_ISO_HISTORY=3,
    PTAR_ISO_PROCESSING=4
};

struct PTARIsoSlot
{
    IDirect3DTexture9* producerTexture;
    IDirect3DSurface9* producerSurface;
    IDirect3DTexture9* presenterTexture;
    IDirect3DSurface9* presenterSurface;
    IDirect3DQuery9* producerFence;
    HANDLE sharedHandle;
    PTARIsoSlotState state;
    unsigned long sequence;
};

struct PTARIsoVertex
{
    float x,y,z,rhw,u,v;
};

struct PTARIsoPresenter
{
    IDirect3DDevice9Ex* producer;
    IDirect3D9Ex* presenterD3D;
    IDirect3DDevice9Ex* presenter;
    IDirect3DSurface9* presenterBackBuffer;

    PTARIsoSlot slots[6];
    int historySlot;

    IDirect3DPixelShader9* meCoarseShader;
    IDirect3DPixelShader9* meRefineShader;
    IDirect3DPixelShader9* interpolateShader;

    IDirect3DTexture9* motionCoarseTexture;
    IDirect3DSurface9* motionCoarseSurface;
    IDirect3DTexture9* motionFineTexture;
    IDirect3DSurface9* motionFineSurface;
    IDirect3DTexture9* generatedTexture;
    IDirect3DSurface9* generatedSurface;

    HANDLE wakeEvent;
    HANDLE stopEvent;
    HANDLE thread;

    CRITICAL_SECTION lock;
    bool lockInitialized;

    HWND visibleHwnd;
    HMODULE selfModule;

    UINT adapter;
    D3DDEVTYPE deviceType;
    UINT sourceW;
    UINT sourceH;
    UINT outputW;
    UINT outputH;
    UINT motionCoarseW;
    UINT motionCoarseH;
    UINT motionFineW;
    UINT motionFineH;
    UINT refreshHz;
    D3DFORMAT sharedFormat;
    bool spatialActive;
    bool presenterWindowed;

    volatile LONG running;
    volatile LONG enabled;

    unsigned long submittedReal;
    unsigned long presentedReal;
    unsigned long presentedGenerated;
    unsigned long mailboxDrops;
    unsigned long loadShedRealOnly;
    unsigned long fgFailures;
    unsigned long presentFailures;
    unsigned long frameMarkerSequence;
};

static PTARIsoPresenter g_ptarIso={};

static HRESULT PtIsoCreateRenderTexture(
    IDirect3DDevice9* dev,
    UINT width,
    UINT height,
    D3DFORMAT format,
    IDirect3DTexture9** texture,
    IDirect3DSurface9** surface)
{
    if(!dev || !width || !height || !texture || !surface)
        return D3DERR_INVALIDCALL;

    *texture=0;
    *surface=0;

    HRESULT hr=dev->CreateTexture(
        width,height,1,
        D3DUSAGE_RENDERTARGET,
        format,
        D3DPOOL_DEFAULT,
        texture,0);
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

static HRESULT PtIsoDrawPass(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT targetW,
    UINT targetH,
    IDirect3DPixelShader9* shader,
    IDirect3DTexture9* t0,
    IDirect3DTexture9* t1,
    IDirect3DTexture9* t2,
    const float* c0)
{
    if(!dev || !target || !targetW || !targetH || !shader || !t0)
        return D3DERR_INVALIDCALL;

    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    dev->SetTexture(2,0);

    HRESULT hr=dev->SetDepthStencilSurface(0);
    if(FAILED(hr)) return hr;

    hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={};
    vp.X=0; vp.Y=0;
    vp.Width=targetW;
    vp.Height=targetH;
    vp.MinZ=0.0f;
    vp.MaxZ=1.0f;
    hr=dev->SetViewport(&vp);
    if(FAILED(hr)) return hr;

    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE,FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    dev->SetRenderState(D3DRS_CLIPPING,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetRenderState(
        D3DRS_COLORWRITEENABLE,
        D3DCOLORWRITEENABLE_RED|
        D3DCOLORWRITEENABLE_GREEN|
        D3DCOLORWRITEENABLE_BLUE|
        D3DCOLORWRITEENABLE_ALPHA);

    for(DWORD sampler=0;sampler<3;++sampler)
    {
        dev->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        dev->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    }

    if(c0)
        dev->SetPixelShaderConstantF(0,c0,1);

    dev->SetPixelShader(shader);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetTexture(0,t0);
    dev->SetTexture(1,t1);
    dev->SetTexture(2,t2);

    hr=dev->BeginScene();
    if(FAILED(hr))
    {
        dev->SetTexture(0,0);
        dev->SetTexture(1,0);
        dev->SetTexture(2,0);
        return hr;
    }

    const float w=(float)targetW;
    const float h=(float)targetH;
    PTARIsoVertex q[4]={
        {-0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f},
        {w-0.5f,-0.5f,0.0f,1.0f,1.0f,0.0f},
        {-0.5f,h-0.5f,0.0f,1.0f,0.0f,1.0f},
        {w-0.5f,h-0.5f,0.0f,1.0f,1.0f,1.0f}
    };

    hr=dev->DrawPrimitiveUP(
        D3DPT_TRIANGLESTRIP,2,q,sizeof(PTARIsoVertex));

    HRESULT endHr=dev->EndScene();

    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    dev->SetTexture(2,0);

    if(FAILED(hr)) return hr;
    return endHr;
}

static HRESULT PtIsoRenderGenerated(
    IDirect3DTexture9* previous,
    IDirect3DTexture9* current)
{
    PTARIsoPresenter& p=g_ptarIso;
    if(!p.presenter || !previous || !current ||
       !p.meCoarseShader || !p.meRefineShader ||
       !p.interpolateShader)
        return D3DERR_INVALIDCALL;

    float output[4]={
        (float)p.outputW,
        (float)p.outputH,
        1.0f/(float)p.outputW,
        1.0f/(float)p.outputH};

    HRESULT hr=PtIsoDrawPass(
        p.presenter,
        p.motionCoarseSurface,
        p.motionCoarseW,p.motionCoarseH,
        p.meCoarseShader,
        previous,current,0,
        output);
    if(FAILED(hr))
        return hr;

    hr=PtIsoDrawPass(
        p.presenter,
        p.motionFineSurface,
        p.motionFineW,p.motionFineH,
        p.meRefineShader,
        previous,current,p.motionCoarseTexture,
        output);
    if(FAILED(hr))
        return hr;

    return PtIsoDrawPass(
        p.presenter,
        p.generatedSurface,
        p.outputW,p.outputH,
        p.interpolateShader,
        previous,current,p.motionFineTexture,
        output);
}

static int PtIsoFilterId()
{
    const PTARPresentationRect fit=PtResolutionAspectFit(
        g_ptarIso.sourceW,g_ptarIso.sourceH,
        g_ptarIso.outputW,g_ptarIso.outputH);

    const bool exact15=
        g_ptarIso.spatialActive &&
        PtResolutionExactScale15(
            g_ptarIso.sourceW,g_ptarIso.sourceH,
            fit.width,fit.height);

    return PtHudFilterId(
        g_ptarIso.spatialActive,
        exact15);
}

static HRESULT PtIsoOverlayAndPresent(
    IDirect3DSurface9* frame,
    bool generated)
{
    PTARIsoPresenter& p=g_ptarIso;
    if(!p.presenter || !p.presenterBackBuffer || !frame)
        return D3DERR_INVALIDCALL;

    HRESULT hr=p.presenter->StretchRect(
        frame,0,
        p.presenterBackBuffer,0,
        D3DTEXF_NONE);
    if(FAILED(hr))
        return hr;

    p.presenter->SetDepthStencilSurface(0);
    p.presenter->SetRenderTarget(0,p.presenterBackBuffer);

    ++p.frameMarkerSequence;

    HRESULT hudHr=PtGw16RenderExactHudD3D9(
        p.presenter,
        PtHudVisible(),
        PtHudStateNotice(),
        PtHudMarkerEnabled(),
        p.frameMarkerSequence&4095ul,
        generated,
        PtHudDisplayFps(PtHudFgEnabled()),
        p.sourceW,p.sourceH,
        PtIsoFilterId(),
        PtHudFeedbackType(),
        PtHudFeedbackArgA(),
        PtHudFeedbackArgB(),
        PtHudFeedbackArgC());

    if(FAILED(hudHr))
    {
        PtDiagLogA(
            "GW16I_ISOLATED_HUD_FAIL hr=0x%08lX",
            (unsigned long)hudHr);
    }

    if(PtCaptureConsumeRequest())
    {
        wchar_t saved[MAX_PATH]={0};
        HRESULT capHr=PtCaptureSavePostOverlayBmp(
            p.presenter,
            p.presenterBackBuffer,
            p.selfModule,
            saved,_countof(saved));

        if(SUCCEEDED(capHr))
        {
            PtHudNotifyCaptureSaved();
            PtDiagLogA(
                "OK: F9 isolated D3D9 screenshot saved path=%ls frame=%s",
                saved,
                generated?"GENERATED":"REAL");
        }
        else
        {
            PtDiagLogA(
                "ERROR: F9 isolated D3D9 screenshot failed hr=0x%08lX",
                (unsigned long)capHr);
        }
    }

    hr=p.presenter->PresentEx(
        0,0,
        p.visibleHwnd,
        0,
        0);

    if(SUCCEEDED(hr))
    {
        PtFgPacerRecordVisible(generated);
        if(generated)
            ++p.presentedGenerated;
        else
            ++p.presentedReal;
    }
    else
    {
        ++p.presentFailures;
    }

    return hr;
}

static bool PtIsoFenceReady(PTARIsoSlot& s)
{
    if(!s.producerFence)
        return false;

    HRESULT hr=s.producerFence->GetData(0,0,0);
    if(hr==S_OK)
        return true;

    if(hr!=S_FALSE)
    {
        PtDiagLogA(
            "ISOLATED_FENCE_ERROR seq=%lu hr=0x%08lX",
            s.sequence,
            (unsigned long)hr);
    }
    return false;
}

static int PtIsoCountReadyLocked()
{
    int count=0;
    for(int i=0;i<6;++i)
        if(g_ptarIso.slots[i].state==PTAR_ISO_READY)
            ++count;
    return count;
}

static int PtIsoFindOldestReadyLocked()
{
    int found=-1;
    unsigned long best=0xFFFFFFFFul;

    for(int i=0;i<6;++i)
    {
        const PTARIsoSlot& s=g_ptarIso.slots[i];
        if(s.state==PTAR_ISO_READY && s.sequence<best)
        {
            best=s.sequence;
            found=i;
        }
    }
    return found;
}

inline void PtIsoFreeSlotLocked(int index)
{
    if(index<0 || index>=6)
        return;
    g_ptarIso.slots[index].state=PTAR_ISO_FREE;
}

static DWORD WINAPI PtIsoPresenterThread(LPVOID)
{
    PTARIsoPresenter& p=g_ptarIso;

    PtDiagLogA(
        "ISOLATED_PRESENTER_THREAD_START target60_sync1=1 "
        "separate_device=1 async_fg=1");

    while(InterlockedCompareExchange(&p.running,1,1)!=0)
    {
        HANDLE events[2]={p.stopEvent,p.wakeEvent};
        DWORD wr=WaitForMultipleObjects(
            2,events,FALSE,20);

        if(wr==WAIT_OBJECT_0)
            break;

        if(InterlockedCompareExchange(&p.enabled,1,1)==0)
            continue;

        int current=-1;
        int readyDepth=0;

        EnterCriticalSection(&p.lock);
        readyDepth=PtIsoCountReadyLocked();
        current=PtIsoFindOldestReadyLocked();
        if(current>=0)
            p.slots[current].state=PTAR_ISO_PROCESSING;
        LeaveCriticalSection(&p.lock);

        if(current<0)
            continue;

        PTARIsoSlot& cur=p.slots[current];

        // Producer already issued D3DGETDATA_FLUSH. Wait on the isolated thread,
        // never on the game thread.
        int spins=0;
        while(!PtIsoFenceReady(cur) &&
              WaitForSingleObject(p.stopEvent,0)!=WAIT_OBJECT_0)
        {
            if(++spins>500)
            {
                PtDiagLogA(
                    "ISOLATED_FENCE_TIMEOUT seq=%lu",
                    cur.sequence);
                break;
            }
            Sleep(0);
        }

        if(spins>500)
        {
            EnterCriticalSection(&p.lock);
            cur.state=PTAR_ISO_FREE;
            LeaveCriticalSection(&p.lock);
            continue;
        }

        if(p.historySlot<0)
        {
            PtIsoOverlayAndPresent(
                cur.presenterSurface,
                false);

            EnterCriticalSection(&p.lock);
            cur.state=PTAR_ISO_HISTORY;
            p.historySlot=current;
            LeaveCriticalSection(&p.lock);
            continue;
        }

        int previous=p.historySlot;
        PTARIsoSlot& prev=p.slots[previous];

        const bool fgEnabled=PtHudFgEnabled();
        bool generatedPresented=false;

        // Production load-shed principle: when source pressure creates backlog,
        // skip GENERATED work and preserve REAL delivery. Never stall the game
        // to maintain a synthetic 1:1 count.
        const bool loadShed=
            fgEnabled && readyDepth>1;

        if(fgEnabled && !loadShed)
        {
            HRESULT fgHr=PtIsoRenderGenerated(
                prev.presenterTexture,
                cur.presenterTexture);

            if(SUCCEEDED(fgHr))
            {
                HRESULT presentG=
                    PtIsoOverlayAndPresent(
                        p.generatedSurface,
                        true);
                generatedPresented=SUCCEEDED(presentG);
            }
            else
            {
                ++p.fgFailures;
                PtDiagLogA(
                    "ISOLATED_FG_FAIL seq=%lu hr=0x%08lX fallback=REAL_ONLY",
                    cur.sequence,
                    (unsigned long)fgHr);
            }
        }
        else if(loadShed)
        {
            ++p.loadShedRealOnly;
            PtDiagLogA(
                "ISOLATED_LOAD_SHED_REAL_ONLY seq=%lu ready_depth=%d",
                cur.sequence,
                readyDepth);
        }

        HRESULT realHr=PtIsoOverlayAndPresent(
            cur.presenterSurface,
            false);

        if(FAILED(realHr))
        {
            PtDiagLogA(
                "ISOLATED_REAL_PRESENT_FAIL seq=%lu hr=0x%08lX",
                cur.sequence,
                (unsigned long)realHr);
        }

        EnterCriticalSection(&p.lock);
        if(previous>=0 && previous<6)
            p.slots[previous].state=PTAR_ISO_FREE;
        cur.state=PTAR_ISO_HISTORY;
        p.historySlot=current;
        LeaveCriticalSection(&p.lock);

        if(fgEnabled)
        {
            PtDiagLogA(
                "ISOLATED_PAIR seq=%lu generated=%d real=%d ready_depth=%d",
                cur.sequence,
                generatedPresented?1:0,
                SUCCEEDED(realHr)?1:0,
                readyDepth);
        }
    }

    PtDiagLogA(
        "ISOLATED_PRESENTER_THREAD_STOP submitted_real=%lu "
        "presented_real=%lu presented_generated=%lu mailbox_drops=%lu "
        "load_shed=%lu fg_failures=%lu present_failures=%lu",
        p.submittedReal,
        p.presentedReal,
        p.presentedGenerated,
        p.mailboxDrops,
        p.loadShedRealOnly,
        p.fgFailures,
        p.presentFailures);

    return 0;
}

static void PtIsoPresenterRelease()
{
    PTARIsoPresenter& p=g_ptarIso;

    if(p.thread)
    {
        if(p.stopEvent) SetEvent(p.stopEvent);
        InterlockedExchange(&p.running,0);
        WaitForSingleObject(p.thread,3000);
        CloseHandle(p.thread);
        p.thread=0;
    }

    if(p.wakeEvent){CloseHandle(p.wakeEvent);p.wakeEvent=0;}
    if(p.stopEvent){CloseHandle(p.stopEvent);p.stopEvent=0;}

    for(int i=0;i<6;++i)
    {
        PTARIsoSlot& s=p.slots[i];
        if(s.producerFence){s.producerFence->Release();s.producerFence=0;}
        if(s.presenterSurface){s.presenterSurface->Release();s.presenterSurface=0;}
        if(s.presenterTexture){s.presenterTexture->Release();s.presenterTexture=0;}
        if(s.producerSurface){s.producerSurface->Release();s.producerSurface=0;}
        if(s.producerTexture){s.producerTexture->Release();s.producerTexture=0;}
        s.sharedHandle=0;
        s.state=PTAR_ISO_FREE;
        s.sequence=0;
    }

    if(p.generatedSurface){p.generatedSurface->Release();p.generatedSurface=0;}
    if(p.generatedTexture){p.generatedTexture->Release();p.generatedTexture=0;}
    if(p.motionFineSurface){p.motionFineSurface->Release();p.motionFineSurface=0;}
    if(p.motionFineTexture){p.motionFineTexture->Release();p.motionFineTexture=0;}
    if(p.motionCoarseSurface){p.motionCoarseSurface->Release();p.motionCoarseSurface=0;}
    if(p.motionCoarseTexture){p.motionCoarseTexture->Release();p.motionCoarseTexture=0;}

    if(p.interpolateShader){p.interpolateShader->Release();p.interpolateShader=0;}
    if(p.meRefineShader){p.meRefineShader->Release();p.meRefineShader=0;}
    if(p.meCoarseShader){p.meCoarseShader->Release();p.meCoarseShader=0;}

    if(p.presenterBackBuffer){p.presenterBackBuffer->Release();p.presenterBackBuffer=0;}
    if(p.presenter){p.presenter->Release();p.presenter=0;}
    if(p.presenterD3D){p.presenterD3D->Release();p.presenterD3D=0;}
    if(p.producer){p.producer->Release();p.producer=0;}

    if(p.lockInitialized)
    {
        DeleteCriticalSection(&p.lock);
        p.lockInitialized=false;
    }

    p.historySlot=-1;
    p.visibleHwnd=0;
    p.selfModule=0;
    p.adapter=0;
    p.deviceType=D3DDEVTYPE_HAL;
    p.sourceW=p.sourceH=p.outputW=p.outputH=0;
    p.motionCoarseW=p.motionCoarseH=0;
    p.motionFineW=p.motionFineH=0;
    p.refreshHz=0;
    p.sharedFormat=D3DFMT_UNKNOWN;
    p.spatialActive=false;
    p.presenterWindowed=true;
    p.submittedReal=0;
    p.presentedReal=0;
    p.presentedGenerated=0;
    p.mailboxDrops=0;
    p.loadShedRealOnly=0;
    p.fgFailures=0;
    p.presentFailures=0;
    p.frameMarkerSequence=0;

    InterlockedExchange(&p.enabled,0);
}

static HRESULT PtIsoCreatePresenterDevice(
    PTARIsoPFN_Direct3DCreate9Ex create9Ex,
    UINT adapter,
    D3DDEVTYPE type,
    HWND hwnd,
    const D3DPRESENT_PARAMETERS& original,
    UINT outputW,
    UINT outputH,
    UINT refreshHz)
{
    PTARIsoPresenter& p=g_ptarIso;
    if(!create9Ex || !hwnd || !outputW || !outputH)
        return D3DERR_INVALIDCALL;

    HRESULT hr=create9Ex(
        D3D_SDK_VERSION,
        &p.presenterD3D);
    if(FAILED(hr) || !p.presenterD3D)
        return FAILED(hr)?hr:E_FAIL;

    D3DDISPLAYMODEEX desktop={};
    desktop.Size=sizeof(desktop);
    D3DDISPLAYROTATION rotation=D3DDISPLAYROTATION_IDENTITY;
    HRESULT dmHr=p.presenterD3D->GetAdapterDisplayModeEx(
        adapter,&desktop,&rotation);

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=outputW;
    pp.BackBufferHeight=outputH;
    pp.BackBufferCount=1;
    pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality=0;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.EnableAutoDepthStencil=FALSE;
    pp.Flags=0;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

    D3DDISPLAYMODEEX fullscreen={};
    D3DDISPLAYMODEEX* fullscreenPtr=0;

    p.presenterWindowed=original.Windowed?true:false;

    if(original.Windowed)
    {
        pp.Windowed=TRUE;
        pp.BackBufferFormat=D3DFMT_UNKNOWN;
        pp.FullScreen_RefreshRateInHz=0;

        // FLIPEX is the closest D3D9Ex analogue to the production flip-model
        // isolated presenter. Fall back to DISCARD if the driver rejects it.
        pp.SwapEffect=D3DSWAPEFFECT_FLIPEX;
    }
    else
    {
        pp.Windowed=FALSE;

        const D3DFORMAT displayFormat=
            SUCCEEDED(dmHr)?
                desktop.Format:
                (original.BackBufferFormat!=D3DFMT_UNKNOWN?
                    original.BackBufferFormat:D3DFMT_X8R8G8B8);

        pp.BackBufferFormat=displayFormat;
        pp.FullScreen_RefreshRateInHz=
            refreshHz>=30u?refreshHz:60u;

        fullscreen.Size=sizeof(fullscreen);
        fullscreen.Width=outputW;
        fullscreen.Height=outputH;
        fullscreen.RefreshRate=pp.FullScreen_RefreshRateInHz;
        fullscreen.Format=displayFormat;
        fullscreen.ScanLineOrdering=
            D3DSCANLINEORDERING_PROGRESSIVE;
        fullscreenPtr=&fullscreen;
    }

    const DWORD flags=
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED;

    hr=p.presenterD3D->CreateDeviceEx(
        adapter,type,hwnd,
        flags,
        &pp,
        fullscreenPtr,
        &p.presenter);

    if(FAILED(hr) && original.Windowed &&
       pp.SwapEffect==D3DSWAPEFFECT_FLIPEX)
    {
        pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        hr=p.presenterD3D->CreateDeviceEx(
            adapter,type,hwnd,
            flags,
            &pp,
            0,
            &p.presenter);
    }

    if(FAILED(hr) || !p.presenter)
        return FAILED(hr)?hr:E_FAIL;

    hr=p.presenter->GetBackBuffer(
        0,0,D3DBACKBUFFER_TYPE_MONO,
        &p.presenterBackBuffer);
    if(FAILED(hr) || !p.presenterBackBuffer)
        return FAILED(hr)?hr:E_FAIL;

    return S_OK;
}

inline HRESULT PtIsoPresenterInitialize(
    IDirect3DDevice9Ex* producer,
    PTARIsoPFN_Direct3DCreate9Ex create9Ex,
    HMODULE selfModule,
    UINT adapter,
    D3DDEVTYPE type,
    HWND visibleHwnd,
    const D3DPRESENT_PARAMETERS& original,
    UINT sourceW,
    UINT sourceH,
    UINT outputW,
    UINT outputH,
    D3DFORMAT sharedFormat,
    bool spatialActive,
    UINT refreshHz)
{
    PtIsoPresenterRelease();

    if(!producer || !create9Ex || !visibleHwnd ||
       !sourceW || !sourceH || !outputW || !outputH)
        return D3DERR_INVALIDCALL;

    PTARIsoPresenter& p=g_ptarIso;

    p.producer=producer;
    p.producer->AddRef();
    p.selfModule=selfModule;
    p.adapter=adapter;
    p.deviceType=type;
    p.visibleHwnd=visibleHwnd;
    p.sourceW=sourceW;
    p.sourceH=sourceH;
    p.outputW=outputW;
    p.outputH=outputH;
    p.sharedFormat=sharedFormat;
    p.spatialActive=spatialActive;
    p.refreshHz=
        refreshHz>=30u&&refreshHz<=360u?
            refreshHz:60u;
    p.historySlot=-1;

    InitializeCriticalSection(&p.lock);
    p.lockInitialized=true;

    HRESULT hr=PtIsoCreatePresenterDevice(
        create9Ex,
        adapter,type,
        visibleHwnd,
        original,
        outputW,outputH,
        p.refreshHz);
    if(FAILED(hr))
    {
        PtDiagLogA(
            "ISOLATED_PRESENTER_DEVICE_FAIL hr=0x%08lX",
            (unsigned long)hr);
        PtIsoPresenterRelease();
        return hr;
    }

    D3DSURFACE_DESC presenterDesc={};
    hr=p.presenterBackBuffer->GetDesc(&presenterDesc);
    if(FAILED(hr))
    {
        PtIsoPresenterRelease();
        return hr;
    }

    // Shared producer/presenter ring.
    for(int i=0;i<6;++i)
    {
        PTARIsoSlot& s=p.slots[i];
        s.sharedHandle=0;

        hr=p.producer->CreateTexture(
            outputW,outputH,1,
            D3DUSAGE_RENDERTARGET,
            sharedFormat,
            D3DPOOL_DEFAULT,
            &s.producerTexture,
            &s.sharedHandle);
        if(FAILED(hr) || !s.producerTexture || !s.sharedHandle)
        {
            PtDiagLogA(
                "ISOLATED_SHARED_CREATE_FAIL index=%d fmt=%u hr=0x%08lX handle=%p",
                i,(unsigned)sharedFormat,
                (unsigned long)hr,
                s.sharedHandle);
            PtIsoPresenterRelease();
            return FAILED(hr)?hr:E_FAIL;
        }

        HANDLE openHandle=s.sharedHandle;
        hr=p.presenter->CreateTexture(
            outputW,outputH,1,
            D3DUSAGE_RENDERTARGET,
            sharedFormat,
            D3DPOOL_DEFAULT,
            &s.presenterTexture,
            &openHandle);
        if(FAILED(hr) || !s.presenterTexture)
        {
            PtDiagLogA(
                "ISOLATED_SHARED_OPEN_FAIL index=%d fmt=%u hr=0x%08lX",
                i,(unsigned)sharedFormat,
                (unsigned long)hr);
            PtIsoPresenterRelease();
            return FAILED(hr)?hr:E_FAIL;
        }

        hr=s.producerTexture->GetSurfaceLevel(
            0,&s.producerSurface);
        if(FAILED(hr) || !s.producerSurface)
        {
            PtIsoPresenterRelease();
            return FAILED(hr)?hr:E_FAIL;
        }

        hr=s.presenterTexture->GetSurfaceLevel(
            0,&s.presenterSurface);
        if(FAILED(hr) || !s.presenterSurface)
        {
            PtIsoPresenterRelease();
            return FAILED(hr)?hr:E_FAIL;
        }

        hr=p.producer->CreateQuery(
            D3DQUERYTYPE_EVENT,
            &s.producerFence);
        if(FAILED(hr) || !s.producerFence)
        {
            PtIsoPresenterRelease();
            return FAILED(hr)?hr:E_FAIL;
        }

        s.state=PTAR_ISO_FREE;
    }

    // FG resources live only on the isolated presenter device.
    p.motionCoarseW=(outputW+3u)/4u;
    p.motionCoarseH=(outputH+3u)/4u;
    p.motionFineW=(outputW+1u)/2u;
    p.motionFineH=(outputH+1u)/2u;

    hr=p.presenter->CreatePixelShader(
        (const DWORD*)g_ptarFgMeCoarsePs,
        &p.meCoarseShader);
    if(FAILED(hr) || !p.meCoarseShader)
    {
        PtIsoPresenterRelease();
        return FAILED(hr)?hr:E_FAIL;
    }

    hr=p.presenter->CreatePixelShader(
        (const DWORD*)g_ptarFgMeRefinePs,
        &p.meRefineShader);
    if(FAILED(hr) || !p.meRefineShader)
    {
        PtIsoPresenterRelease();
        return FAILED(hr)?hr:E_FAIL;
    }

    hr=p.presenter->CreatePixelShader(
        (const DWORD*)g_ptarFgInterpolatePs,
        &p.interpolateShader);
    if(FAILED(hr) || !p.interpolateShader)
    {
        PtIsoPresenterRelease();
        return FAILED(hr)?hr:E_FAIL;
    }

    hr=PtIsoCreateRenderTexture(
        p.presenter,
        p.motionCoarseW,p.motionCoarseH,
        D3DFMT_A16B16G16R16F,
        &p.motionCoarseTexture,
        &p.motionCoarseSurface);
    if(FAILED(hr))
    {
        PtIsoPresenterRelease();
        return hr;
    }

    hr=PtIsoCreateRenderTexture(
        p.presenter,
        p.motionFineW,p.motionFineH,
        D3DFMT_A16B16G16R16F,
        &p.motionFineTexture,
        &p.motionFineSurface);
    if(FAILED(hr))
    {
        PtIsoPresenterRelease();
        return hr;
    }

    hr=PtIsoCreateRenderTexture(
        p.presenter,
        outputW,outputH,
        sharedFormat,
        &p.generatedTexture,
        &p.generatedSurface);
    if(FAILED(hr))
    {
        PtIsoPresenterRelease();
        return hr;
    }

    p.stopEvent=CreateEventW(0,TRUE,FALSE,0);
    p.wakeEvent=CreateEventW(0,FALSE,FALSE,0);
    if(!p.stopEvent || !p.wakeEvent)
    {
        hr=HRESULT_FROM_WIN32(GetLastError());
        PtIsoPresenterRelease();
        return hr;
    }

    InterlockedExchange(&p.running,1);
    InterlockedExchange(&p.enabled,1);

    p.thread=CreateThread(
        0,0,
        PtIsoPresenterThread,
        0,0,0);
    if(!p.thread)
    {
        hr=HRESULT_FROM_WIN32(GetLastError());
        InterlockedExchange(&p.running,0);
        PtIsoPresenterRelease();
        return hr;
    }

    PtDiagLogA(
        "ISOLATED_PRESENTER_READY separate_device=1 async_fg=1 "
        "src=%ux%u out=%ux%u fmt=%u refresh=%u windowed=%d "
        "swap=SYNC1 slots=6",
        sourceW,sourceH,
        outputW,outputH,
        (unsigned)sharedFormat,
        p.refreshHz,
        p.presenterWindowed?1:0);

    return S_OK;
}

static bool PtIsoPresenterAvailable()
{
    return g_ptarIso.thread!=0;
}

static bool PtIsoPresenterIsActive()
{
    return
        PtIsoPresenterAvailable() &&
        InterlockedCompareExchange(
            &g_ptarIso.enabled,1,1)!=0;
}

static void PtIsoPresenterClearQueue()
{
    PTARIsoPresenter& p=g_ptarIso;
    if(!p.lockInitialized)
        return;

    EnterCriticalSection(&p.lock);
    for(int i=0;i<6;++i)
    {
        if(p.slots[i].state!=PTAR_ISO_PROCESSING)
            p.slots[i].state=PTAR_ISO_FREE;
    }
    p.historySlot=-1;
    LeaveCriticalSection(&p.lock);
}

static void PtIsoPresenterSetEnabled(bool enabled)
{
    InterlockedExchange(
        &g_ptarIso.enabled,
        enabled?1:0);

    PtIsoPresenterClearQueue();

    if(enabled && g_ptarIso.wakeEvent)
        SetEvent(g_ptarIso.wakeEvent);

    PtDiagLogA(
        "ISOLATED_PRESENTER_ENABLED=%d",
        enabled?1:0);
}

static int PtIsoAcquireProducerSlot()
{
    PTARIsoPresenter& p=g_ptarIso;
    if(!p.lockInitialized)
        return -1;

    EnterCriticalSection(&p.lock);

    int freeSlot=-1;
    for(int i=0;i<6;++i)
    {
        if(p.slots[i].state==PTAR_ISO_FREE)
        {
            freeSlot=i;
            break;
        }
    }

    if(freeSlot<0)
    {
        // Production load-shed philosophy: preserve the newest REAL source
        // frame. Reclaim the oldest queued REAL, never the history currently
        // used for the next interpolation.
        int oldest=-1;
        unsigned long seq=0xFFFFFFFFul;
        for(int i=0;i<6;++i)
        {
            const PTARIsoSlot& s=p.slots[i];
            if(s.state==PTAR_ISO_READY && s.sequence<seq)
            {
                seq=s.sequence;
                oldest=i;
            }
        }

        if(oldest>=0)
        {
            freeSlot=oldest;
            ++p.mailboxDrops;
        }
    }

    if(freeSlot>=0)
        p.slots[freeSlot].state=PTAR_ISO_WRITING;

    LeaveCriticalSection(&p.lock);
    return freeSlot;
}

inline HRESULT PtIsoSubmitReal(
    IDirect3DSurface9* sourceSurface,
    unsigned long sequence)
{
    PTARIsoPresenter& p=g_ptarIso;

    if(!PtIsoPresenterIsActive() ||
       !p.producer || !sourceSurface)
        return D3DERR_NOTAVAILABLE;

    const int slotIndex=PtIsoAcquireProducerSlot();
    if(slotIndex<0)
    {
        ++p.mailboxDrops;
        PtDiagLogA(
            "ISOLATED_MAILBOX_DROP seq=%lu reason=NO_SLOT",
            sequence);
        return S_FALSE;
    }

    PTARIsoSlot& s=p.slots[slotIndex];
    s.sequence=sequence;

    HRESULT hr=p.producer->StretchRect(
        sourceSurface,0,
        s.producerSurface,0,
        D3DTEXF_NONE);

    if(SUCCEEDED(hr))
        hr=s.producerFence->Issue(D3DISSUE_END);

    if(SUCCEEDED(hr))
    {
        // Flush producer commands without waiting for completion. The isolated
        // thread waits on the EVENT query.
        s.producerFence->GetData(
            0,0,D3DGETDATA_FLUSH);
    }

    EnterCriticalSection(&p.lock);
    if(SUCCEEDED(hr))
    {
        s.state=PTAR_ISO_READY;
        ++p.submittedReal;
    }
    else
    {
        s.state=PTAR_ISO_FREE;
    }
    LeaveCriticalSection(&p.lock);

    if(FAILED(hr))
    {
        PtDiagLogA(
            "ISOLATED_SUBMIT_REAL_FAIL seq=%lu hr=0x%08lX",
            sequence,
            (unsigned long)hr);
        return hr;
    }

    if(p.wakeEvent)
        SetEvent(p.wakeEvent);

    return S_OK;
}

inline unsigned long PtIsoPresentedReal()
{
    return g_ptarIso.presentedReal;
}

inline unsigned long PtIsoPresentedGenerated()
{
    return g_ptarIso.presentedGenerated;
}

inline unsigned long PtIsoMailboxDrops()
{
    return g_ptarIso.mailboxDrops;
}

inline unsigned long PtIsoLoadShedCount()
{
    return g_ptarIso.loadShedRealOnly;
}
