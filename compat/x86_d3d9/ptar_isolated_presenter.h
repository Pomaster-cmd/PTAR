#pragma once

#include <windows.h>
#include <d3d9.h>
#include <cstring>

// Generic D3D9 implementation of the validated D3D11/GW16I presenter model.
//
// Invariants copied from the production model:
//   - the game/source device never performs the physical display Present while
//     the isolated presenter is active;
//   - GENERATED work runs only on the isolated presenter device/thread;
//   - visible ordering is GENERATED -> REAL for each source pair;
//   - FG OFF keeps presenter infrastructure alive and routes REAL_ONLY;
//   - under pressure, GENERATED is shed before REAL;
//   - the final HUD/F9 surface is the isolated presenter's BackBuffer0.
//
// D3D9-specific transport:
// regular IDirect3DDevice9 cannot create cross-device shared resources. The
// laboratory probe proved that replacing the game's device with D3D9Ex would
// expose shared handles but would also remove D3DPOOL_MANAGED support. To avoid
// that compatibility regression, the generic path keeps the game's ordinary
// D3D9 device unchanged and bridges each completed REAL frame through a short
// readback into a CPU ring. The isolated D3D9Ex presenter uploads from that
// ring on its own thread.
//
// Measured CI readback cost before integration:
//   1280x720  ~0.39 ms / frame
//   1600x900  ~0.56 ms / frame
//   1920x1080 ~0.79 ms / frame
// Those measurements are laboratory evidence only; field hardware remains the
// final gate.

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
    IDirect3DSurface9* producerReadback;
    IDirect3DTexture9* presenterTexture;
    IDirect3DSurface9* presenterSurface;

    unsigned char* cpuBytes;
    UINT rowBytes;
    UINT rows;

    PTARIsoSlotState state;
    unsigned long sequence;
};

struct PTARIsoVertex
{
    float x,y,z,rhw,u,v;
};

struct PTARIsoPresenter
{
    IDirect3DDevice9* producer;

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
    D3DFORMAT frameFormat;

    volatile LONG running;
    volatile LONG enabled;
    volatile LONG resetHistoryRequested;

    unsigned long submittedReal;
    unsigned long presentedReal;
    unsigned long presentedGenerated;
    unsigned long mailboxDrops;
    unsigned long loadShedRealOnly;
    unsigned long fgFailures;
    unsigned long presentFailures;
    unsigned long bridgeFailures;
    unsigned long frameMarkerSequence;

    LARGE_INTEGER qpcFrequency;
    LONGLONG bridgeTotalTicks;
    LONGLONG bridgeMaxTicks;
    unsigned long bridgeSamples;
};

static PTARIsoPresenter g_ptarIso={};

static UINT PtIsoBytesPerPixel(D3DFORMAT format)
{
    switch(format)
    {
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
        return 2u;

    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_A8B8G8R8:
        return 4u;

    default:
        return 0u;
    }
}

static LONGLONG PtIsoNow()
{
    LARGE_INTEGER q={};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

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

static HRESULT PtIsoCreateUploadTexture(
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
        D3DUSAGE_DYNAMIC,
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

static void PtIsoMaybeLogStatus()
{
    PTARIsoPresenter& p=g_ptarIso;

    if(PtHudConsumeStatusLogPending())
    {
        PtDiagLogA(
            "STATUS_RUNTIME fg=%s profile=%d src=%ux%u out=%ux%u "
            "filter=%d real_fps=%.3f visible_fps=%.3f "
            "real_count=%lu gen_count=%lu mailbox_drop=%lu "
            "load_shed=%lu bridge_fail=%lu",
            PtHudFgEnabled()?"ON":"OFF",
            PtHudFgProfile(),
            p.sourceW,p.sourceH,
            p.outputW,p.outputH,
            PtIsoFilterId(),
            PtHudRealFps(),
            PtFgPacerVisibleFps(),
            p.presentedReal,
            p.presentedGenerated,
            p.mailboxDrops,
            p.loadShedRealOnly,
            p.bridgeFailures);
    }

    const LONGLONG now=PtHudNow();
    if(g_ptarHudLastFpsLogQpc==0 ||
       now-g_ptarHudLastFpsLogQpc>=g_ptarHudFreq.QuadPart)
    {
        g_ptarHudLastFpsLogQpc=now;

        double bridgeAvgMs=0.0;
        double bridgeMaxMs=0.0;
        if(p.qpcFrequency.QuadPart>0)
        {
            if(p.bridgeSamples)
            {
                bridgeAvgMs=
                    (double)p.bridgeTotalTicks*1000.0/
                    ((double)p.qpcFrequency.QuadPart*
                     (double)p.bridgeSamples);
            }
            bridgeMaxMs=
                (double)p.bridgeMaxTicks*1000.0/
                (double)p.qpcFrequency.QuadPart;
        }

        PtDiagLogA(
            "FPS_WALLCLOCK_SAMPLE real_fps=%.3f visible_fps=%.3f "
            "fg=%s real_count=%lu gen_count=%lu mailbox_drop=%lu "
            "load_shed=%lu bridge_avg_ms=%.3f bridge_max_ms=%.3f",
            PtHudRealFps(),
            PtFgPacerVisibleFps(),
            PtHudFgEnabled()?"ON":"OFF",
            p.presentedReal,
            p.presentedGenerated,
            p.mailboxDrops,
            p.loadShedRealOnly,
            bridgeAvgMs,
            bridgeMaxMs);
    }
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
        PtFgPacerVisibleFps(),
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

    PtIsoMaybeLogStatus();

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

    // Sync1 is intentional here. Unlike the rejected same-device design this
    // call runs on a completely separate D3D9Ex device/thread, so VBlank
    // residence cannot serialize the game's producer device.
    hr=p.presenter->PresentEx(
        0,0,0,0,0);

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
        PtDiagLogA(
            "ISOLATED_PRESENT_FAIL generated=%d hr=0x%08lX",
            generated?1:0,
            (unsigned long)hr);
    }

    return hr;
}

static HRESULT PtIsoUploadSlot(PTARIsoSlot& s)
{
    if(!s.presenterTexture || !s.cpuBytes ||
       !s.rowBytes || !s.rows)
        return D3DERR_INVALIDCALL;

    D3DLOCKED_RECT lr={};
    HRESULT hr=s.presenterTexture->LockRect(
        0,&lr,0,D3DLOCK_DISCARD);
    if(FAILED(hr))
        return hr;

    const unsigned char* src=s.cpuBytes;
    unsigned char* dst=(unsigned char*)lr.pBits;

    for(UINT y=0;y<s.rows;++y)
    {
        std::memcpy(
            dst+(size_t)y*(size_t)lr.Pitch,
            src+(size_t)y*(size_t)s.rowBytes,
            s.rowBytes);
    }

    return s.presenterTexture->UnlockRect(0);
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

static void PtIsoResetHistoryOnPresenterThread()
{
    PTARIsoPresenter& p=g_ptarIso;

    if(InterlockedExchange(
           &p.resetHistoryRequested,0)==0)
        return;

    EnterCriticalSection(&p.lock);

    for(int i=0;i<6;++i)
    {
        if(p.slots[i].state==PTAR_ISO_READY ||
           p.slots[i].state==PTAR_ISO_HISTORY)
            p.slots[i].state=PTAR_ISO_FREE;
    }

    p.historySlot=-1;

    LeaveCriticalSection(&p.lock);

    PtDiagLogA("ISOLATED_HISTORY_RESET");
}

static DWORD WINAPI PtIsoPresenterThread(LPVOID)
{
    PTARIsoPresenter& p=g_ptarIso;

    PtDiagLogA(
        "ISOLATED_PRESENTER_THREAD_START "
        "transport=CPU_READBACK separate_device=1 async_fg=1 sync=1");

    while(InterlockedCompareExchange(&p.running,1,1)!=0)
    {
        HANDLE events[2]={p.stopEvent,p.wakeEvent};
        DWORD wr=WaitForMultipleObjects(
            2,events,FALSE,20);

        if(wr==WAIT_OBJECT_0)
            break;

        PtIsoResetHistoryOnPresenterThread();

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

        HRESULT uploadHr=PtIsoUploadSlot(cur);
        if(FAILED(uploadHr))
        {
            ++p.bridgeFailures;
            PtDiagLogA(
                "ISOLATED_UPLOAD_FAIL seq=%lu hr=0x%08lX",
                cur.sequence,
                (unsigned long)uploadHr);

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

        const int previous=p.historySlot;
        PTARIsoSlot& prev=p.slots[previous];

        const bool fgEnabled=PtHudFgEnabled();
        bool generatedPresented=false;

        // Mirrors the production load-shed principle: pressure can reduce
        // GENERATED delivery, but it must not slow REAL source production.
        const bool loadShed=
            fgEnabled && readyDepth>1;

        if(fgEnabled && !loadShed)
        {
            HRESULT fgHr=PtIsoRenderGenerated(
                prev.presenterTexture,
                cur.presenterTexture);

            if(SUCCEEDED(fgHr))
            {
                HRESULT gHr=PtIsoOverlayAndPresent(
                    p.generatedSurface,
                    true);
                generatedPresented=SUCCEEDED(gHr);
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
        "load_shed=%lu fg_failures=%lu present_failures=%lu "
        "bridge_failures=%lu",
        p.submittedReal,
        p.presentedReal,
        p.presentedGenerated,
        p.mailboxDrops,
        p.loadShedRealOnly,
        p.fgFailures,
        p.presentFailures,
        p.bridgeFailures);

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

        if(s.presenterSurface)
        {
            s.presenterSurface->Release();
            s.presenterSurface=0;
        }

        if(s.presenterTexture)
        {
            s.presenterTexture->Release();
            s.presenterTexture=0;
        }

        if(s.producerReadback)
        {
            s.producerReadback->Release();
            s.producerReadback=0;
        }

        if(s.cpuBytes)
        {
            VirtualFree(s.cpuBytes,0,MEM_RELEASE);
            s.cpuBytes=0;
        }

        s.rowBytes=0;
        s.rows=0;
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
    p.frameFormat=D3DFMT_UNKNOWN;

    p.submittedReal=0;
    p.presentedReal=0;
    p.presentedGenerated=0;
    p.mailboxDrops=0;
    p.loadShedRealOnly=0;
    p.fgFailures=0;
    p.presentFailures=0;
    p.bridgeFailures=0;
    p.frameMarkerSequence=0;

    p.qpcFrequency.QuadPart=0;
    p.bridgeTotalTicks=0;
    p.bridgeMaxTicks=0;
    p.bridgeSamples=0;

    InterlockedExchange(&p.enabled,0);
    InterlockedExchange(&p.resetHistoryRequested,0);
}

static HRESULT PtIsoCreatePresenterDevice(
    PTARIsoPFN_Direct3DCreate9Ex create9Ex,
    UINT adapter,
    D3DDEVTYPE type,
    HWND hwnd,
    UINT outputW,
    UINT outputH)
{
    PTARIsoPresenter& p=g_ptarIso;

    if(!create9Ex || !hwnd || !outputW || !outputH)
        return D3DERR_INVALIDCALL;

    HRESULT hr=create9Ex(
        D3D_SDK_VERSION,
        &p.presenterD3D);
    if(FAILED(hr) || !p.presenterD3D)
        return FAILED(hr)?hr:E_FAIL;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=outputW;
    pp.BackBufferHeight=outputH;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality=0;
    pp.SwapEffect=D3DSWAPEFFECT_FLIPEX;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.EnableAutoDepthStencil=FALSE;
    pp.Flags=0;
    pp.FullScreen_RefreshRateInHz=0;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

    const DWORD flags=
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED;

    hr=p.presenterD3D->CreateDeviceEx(
        adapter,
        type,
        hwnd,
        flags,
        &pp,
        0,
        &p.presenter);

    if(FAILED(hr) || !p.presenter)
    {
        PtDiagLogA(
            "ISOLATED_FLIPEX_CREATE_FAIL hr=0x%08lX fallback=DISCARD",
            (unsigned long)hr);

        pp.SwapEffect=D3DSWAPEFFECT_DISCARD;

        hr=p.presenterD3D->CreateDeviceEx(
            adapter,
            type,
            hwnd,
            flags,
            &pp,
            0,
            &p.presenter);
    }

    if((FAILED(hr) || !p.presenter) &&
       type!=D3DDEVTYPE_HAL)
    {
        pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        hr=p.presenterD3D->CreateDeviceEx(
            adapter,
            D3DDEVTYPE_HAL,
            hwnd,
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
    IDirect3DDevice9* producer,
    PTARIsoPFN_Direct3DCreate9Ex create9Ex,
    HMODULE selfModule,
    UINT adapter,
    D3DDEVTYPE type,
    HWND visibleHwnd,
    UINT sourceW,
    UINT sourceH,
    UINT outputW,
    UINT outputH,
    D3DFORMAT frameFormat,
    bool spatialActive,
    UINT refreshHz)
{
    PtIsoPresenterRelease();

    if(!producer || !create9Ex || !visibleHwnd ||
       !sourceW || !sourceH || !outputW || !outputH)
        return D3DERR_INVALIDCALL;

    const UINT bpp=PtIsoBytesPerPixel(frameFormat);
    if(!bpp)
    {
        PtDiagLogA(
            "ISOLATED_UNSUPPORTED_FRAME_FORMAT fmt=%u",
            (unsigned)frameFormat);
        return D3DERR_NOTAVAILABLE;
    }

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
    p.frameFormat=frameFormat;
    p.spatialActive=spatialActive;
    p.refreshHz=
        refreshHz>=30u&&refreshHz<=360u?
            refreshHz:60u;
    p.historySlot=-1;

    QueryPerformanceFrequency(&p.qpcFrequency);
    if(p.qpcFrequency.QuadPart<=0)
        p.qpcFrequency.QuadPart=1;

    InitializeCriticalSection(&p.lock);
    p.lockInitialized=true;

    HRESULT hr=PtIsoCreatePresenterDevice(
        create9Ex,
        adapter,
        type,
        visibleHwnd,
        outputW,
        outputH);
    if(FAILED(hr))
    {
        PtDiagLogA(
            "ISOLATED_PRESENTER_DEVICE_FAIL hr=0x%08lX",
            (unsigned long)hr);
        PtIsoPresenterRelease();
        return hr;
    }

    // CPU bridge + upload textures.
    for(int i=0;i<6;++i)
    {
        PTARIsoSlot& s=p.slots[i];

        hr=producer->CreateOffscreenPlainSurface(
            outputW,outputH,
            frameFormat,
            D3DPOOL_SYSTEMMEM,
            &s.producerReadback,
            0);

        if(FAILED(hr) || !s.producerReadback)
        {
            PtDiagLogA(
                "ISOLATED_READBACK_SURFACE_FAIL index=%d fmt=%u hr=0x%08lX",
                i,(unsigned)frameFormat,
                (unsigned long)hr);
            PtIsoPresenterRelease();
            return FAILED(hr)?hr:E_FAIL;
        }

        hr=PtIsoCreateUploadTexture(
            p.presenter,
            outputW,outputH,
            frameFormat,
            &s.presenterTexture,
            &s.presenterSurface);

        if(FAILED(hr))
        {
            PtDiagLogA(
                "ISOLATED_UPLOAD_TEXTURE_FAIL index=%d fmt=%u hr=0x%08lX",
                i,(unsigned)frameFormat,
                (unsigned long)hr);
            PtIsoPresenterRelease();
            return hr;
        }

        s.rowBytes=outputW*bpp;
        s.rows=outputH;

        const SIZE_T bytes=
            (SIZE_T)s.rowBytes*(SIZE_T)s.rows;

        s.cpuBytes=(unsigned char*)VirtualAlloc(
            0,bytes,
            MEM_COMMIT|MEM_RESERVE,
            PAGE_READWRITE);

        if(!s.cpuBytes)
        {
            hr=HRESULT_FROM_WIN32(GetLastError());
            PtIsoPresenterRelease();
            return hr;
        }

        s.state=PTAR_ISO_FREE;
    }

    // FG shader + render resources exist only on the isolated presenter.
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
        frameFormat,
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
        "ISOLATED_PRESENTER_READY transport=CPU_READBACK "
        "separate_device=1 async_fg=1 src=%ux%u out=%ux%u fmt=%u "
        "refresh=%u sync=1 slots=6",
        sourceW,sourceH,
        outputW,outputH,
        (unsigned)frameFormat,
        p.refreshHz);

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

static void PtIsoPresenterSetEnabled(bool enabled)
{
    InterlockedExchange(
        &g_ptarIso.enabled,
        enabled?1:0);

    InterlockedExchange(
        &g_ptarIso.resetHistoryRequested,1);

    if(g_ptarIso.wakeEvent)
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

    int result=-1;

    for(int i=0;i<6;++i)
    {
        if(p.slots[i].state==PTAR_ISO_FREE)
        {
            result=i;
            break;
        }
    }

    if(result<0)
    {
        // Reclaim only an unconsumed READY frame. Never touch the HISTORY frame
        // currently required for the next midpoint.
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
            result=oldest;
            ++p.mailboxDrops;
        }
    }

    if(result>=0)
        p.slots[result].state=PTAR_ISO_WRITING;

    LeaveCriticalSection(&p.lock);

    return result;
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

    const LONGLONG t0=PtIsoNow();

    HRESULT hr=p.producer->GetRenderTargetData(
        sourceSurface,
        s.producerReadback);

    if(SUCCEEDED(hr))
    {
        D3DLOCKED_RECT lr={};
        hr=s.producerReadback->LockRect(
            &lr,0,D3DLOCK_READONLY);

        if(SUCCEEDED(hr))
        {
            const unsigned char* src=
                (const unsigned char*)lr.pBits;

            for(UINT y=0;y<s.rows;++y)
            {
                std::memcpy(
                    s.cpuBytes+
                        (size_t)y*(size_t)s.rowBytes,
                    src+
                        (size_t)y*(size_t)lr.Pitch,
                    s.rowBytes);
            }

            HRESULT unlockHr=
                s.producerReadback->UnlockRect();

            if(FAILED(unlockHr))
                hr=unlockHr;
        }
    }

    const LONGLONG dt=PtIsoNow()-t0;

    ++p.bridgeSamples;
    p.bridgeTotalTicks+=dt;
    if(dt>p.bridgeMaxTicks)
        p.bridgeMaxTicks=dt;

    EnterCriticalSection(&p.lock);

    if(SUCCEEDED(hr))
    {
        s.sequence=sequence;
        s.state=PTAR_ISO_READY;
        ++p.submittedReal;
    }
    else
    {
        s.state=PTAR_ISO_FREE;
        ++p.bridgeFailures;
    }

    LeaveCriticalSection(&p.lock);

    if(FAILED(hr))
    {
        PtDiagLogA(
            "ISOLATED_READBACK_FAIL seq=%lu hr=0x%08lX",
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
