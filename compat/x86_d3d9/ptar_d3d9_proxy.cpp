#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define Direct3DCreate9 PTAR_D3D9_HEADER_Direct3DCreate9
#define Direct3DCreate9Ex PTAR_D3D9_HEADER_Direct3DCreate9Ex
#define D3DPERF_BeginEvent PTAR_D3D9_HEADER_D3DPERF_BeginEvent
#define D3DPERF_EndEvent PTAR_D3D9_HEADER_D3DPERF_EndEvent
#define D3DPERF_GetStatus PTAR_D3D9_HEADER_D3DPERF_GetStatus
#define D3DPERF_QueryRepeatFrame PTAR_D3D9_HEADER_D3DPERF_QueryRepeatFrame
#define D3DPERF_SetMarker PTAR_D3D9_HEADER_D3DPERF_SetMarker
#define D3DPERF_SetOptions PTAR_D3D9_HEADER_D3DPERF_SetOptions
#define D3DPERF_SetRegion PTAR_D3D9_HEADER_D3DPERF_SetRegion
#include <d3d9.h>
#undef Direct3DCreate9
#undef Direct3DCreate9Ex
#undef D3DPERF_BeginEvent
#undef D3DPERF_EndEvent
#undef D3DPERF_GetStatus
#undef D3DPERF_QueryRepeatFrame
#undef D3DPERF_SetMarker
#undef D3DPERF_SetOptions
#undef D3DPERF_SetRegion
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "ptar_ps_bytecode.h"
#include "ptar_universal_ps_bytecode.h"
#include "ptar_bilinear_ps_bytecode.h"
#include "ptar_fg_me_coarse_ps_bytecode.h"
#include "ptar_fg_me_refine_ps_bytecode.h"
#include "ptar_fg_interpolate_ps_bytecode.h"
#include "ptar_diag.h"
#include "ptar_fg_pacer.h"
#include "ptar_fg_governor.h"
#include "ptar_resolution_policy.h"

// F10 lives in the HUD input contract but controls the isolated presenter.
// Forward declarations break the intentional header dependency cycle:
// HUD input -> presenter controls; presenter rendering -> HUD state.
static bool PtIsoPresenterAvailable();
static bool PtIsoPresenterIsActive();
static void PtIsoPresenterSetEnabled(bool enabled);

#include "ptar_hud.h"
#include "ptar_gw16i_hud_d3d9.h"
#include "ptar_isolated_presenter.h"

static HMODULE g_self=0;
static HMODULE g_realD3D9=0;

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT,IDirect3D9Ex**);
static PFN_Direct3DCreate9 g_sysDirect3DCreate9=0;
static PFN_Direct3DCreate9Ex g_sysDirect3DCreate9Ex=0;

typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateDevice)(
    IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Reset)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Present)(
    IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetBackBuffer)(
    IDirect3DDevice9*,UINT,UINT,D3DBACKBUFFER_TYPE,IDirect3DSurface9**);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderTarget)(
    IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetDisplayMode)(
    IDirect3DDevice9*,UINT,D3DDISPLAYMODE*);

static PFN_CreateDevice g_realCreateDevice=0;
static PFN_Reset g_realReset=0;
static PFN_Present g_realPresent=0;
static PFN_GetBackBuffer g_realGetBackBuffer=0;
static PFN_SetRenderTarget g_realSetRenderTarget=0;
static PFN_GetDisplayMode g_realGetDisplayMode=0;

static UINT g_ptarAdapter=D3DADAPTER_DEFAULT;
static D3DDEVTYPE g_ptarDeviceType=D3DDEVTYPE_HAL;
static HWND g_ptarFocusWindow=0;

struct PTARContext
{
    IDirect3DDevice9* device;
    IDirect3DTexture9* sourceTexture;
    IDirect3DSurface9* sourceSurface;
    IDirect3DSurface9* virtualDepth;
    IDirect3DSurface9* realBackBuffer;
    IDirect3DPixelShader9* shader;
    IDirect3DPixelShader9* universalShader;
    IDirect3DPixelShader9* bilinearShader;
    IDirect3DTexture9* currentRealTexture;
    IDirect3DSurface9* currentRealSurface;
    IDirect3DStateBlock9* stateBlock;
    UINT sourceW;
    UINT sourceH;
    UINT outputW;
    UINT outputH;
    D3DFORMAT outputFormat;
    D3DFORMAT depthFormat;
    BOOL originalAutoDepth;
    bool active;
    bool spatialActive;
    bool inPresent;
    UINT outputRefreshHz;
    unsigned long sourceSequence;
};

static PTARContext g_ptar={};

static void Log(const wchar_t* fmt,...)
{
    va_list ap;
    va_start(ap,fmt);
    PtDiagVLogW(fmt,ap);
    va_end(ap);
}

static bool EnsureRealD3D9()
{
    PtDiagStage("EnsureRealD3D9_ENTER");
    if(g_realD3D9)
    {
        PtDiagLogA("REAL_D3D9_ALREADY_LOADED module=%p",g_realD3D9);
        return true;
    }

    wchar_t sys[MAX_PATH]={0};
    UINT n=GetSystemDirectoryW(sys,_countof(sys));
    if(!n || n>=_countof(sys)) return false;
    wcscat_s(sys,L"\\d3d9.dll");

    PtDiagStage("EnsureRealD3D9_LoadLibrary");
    g_realD3D9=LoadLibraryW(sys);
    PtDiagLogA("REAL_D3D9_LOAD result=%p gle=%lu",g_realD3D9,(unsigned long)GetLastError());
    if(!g_realD3D9) return false;

    g_sysDirect3DCreate9=(PFN_Direct3DCreate9)GetProcAddress(g_realD3D9,"Direct3DCreate9");
    g_sysDirect3DCreate9Ex=(PFN_Direct3DCreate9Ex)GetProcAddress(g_realD3D9,"Direct3DCreate9Ex");
    PtDiagLogA("REAL_D3D9_EXPORTS create9=%p create9ex=%p",
        (void*)g_sysDirect3DCreate9,(void*)g_sysDirect3DCreate9Ex);
    return g_sysDirect3DCreate9!=0;
}

static FARPROC RealProc(const char* name)
{
    if(!EnsureRealD3D9()) return 0;
    return GetProcAddress(g_realD3D9,name);
}

static void ReleasePTARResources()
{
    g_ptar.active=false;
    g_ptar.spatialActive=false;

    // Presenter owns its own D3D9Ex device/thread and must stop before the
    // producer default-pool resources are released or Reset.
    PtIsoPresenterRelease();

    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}

    if(g_ptar.bilinearShader){g_ptar.bilinearShader->Release();g_ptar.bilinearShader=0;}
    if(g_ptar.universalShader){g_ptar.universalShader->Release();g_ptar.universalShader=0;}
    if(g_ptar.shader){g_ptar.shader->Release();g_ptar.shader=0;}

    if(g_ptar.currentRealSurface){g_ptar.currentRealSurface->Release();g_ptar.currentRealSurface=0;}
    if(g_ptar.currentRealTexture){g_ptar.currentRealTexture->Release();g_ptar.currentRealTexture=0;}

    if(g_ptar.realBackBuffer){g_ptar.realBackBuffer->Release();g_ptar.realBackBuffer=0;}
    if(g_ptar.virtualDepth){g_ptar.virtualDepth->Release();g_ptar.virtualDepth=0;}
    if(g_ptar.sourceSurface){g_ptar.sourceSurface->Release();g_ptar.sourceSurface=0;}
    if(g_ptar.sourceTexture){g_ptar.sourceTexture->Release();g_ptar.sourceTexture=0;}

    g_ptar.device=0;
    g_ptar.sourceW=g_ptar.sourceH=g_ptar.outputW=g_ptar.outputH=0;
    g_ptar.outputRefreshHz=0;
    g_ptar.sourceSequence=0;
}

static HRESULT CreateRenderTexture(
    IDirect3DDevice9* dev,
    UINT width,
    UINT height,
    D3DFORMAT format,
    IDirect3DTexture9** textureOut,
    IDirect3DSurface9** surfaceOut)
{
    if(!dev || !textureOut || !surfaceOut || !width || !height)
        return D3DERR_INVALIDCALL;

    *textureOut=0;
    *surfaceOut=0;

    HRESULT hr=dev->CreateTexture(
        width,height,1,D3DUSAGE_RENDERTARGET,
        format,D3DPOOL_DEFAULT,textureOut,0);
    if(FAILED(hr) || !*textureOut)
        return FAILED(hr)?hr:E_FAIL;

    hr=(*textureOut)->GetSurfaceLevel(0,surfaceOut);
    if(FAILED(hr) || !*surfaceOut)
    {
        (*textureOut)->Release();
        *textureOut=0;
        return FAILED(hr)?hr:E_FAIL;
    }

    return S_OK;
}

static HRESULT SetVirtualViewport(IDirect3DDevice9* dev)
{
    D3DVIEWPORT9 vp={};
    vp.X=0; vp.Y=0;
    vp.Width=g_ptar.sourceW;
    vp.Height=g_ptar.sourceH;
    vp.MinZ=0.0f; vp.MaxZ=1.0f;
    return dev->SetViewport(&vp);
}

static void PreparePTARPresentationParameters(
    const D3DPRESENT_PARAMETERS& original,
    const PTARResolutionPlan& plan,
    D3DPRESENT_PARAMETERS* actual)
{
    if(!actual)
        return;

    *actual=original;
    actual->BackBufferWidth=plan.deviceW;
    actual->BackBufferHeight=plan.deviceH;

    // PTAR owns a virtual game render target. Keep that target non-MSAA and
    // provide the game's depth surface separately at the source resolution.
    actual->MultiSampleType=D3DMULTISAMPLE_NONE;
    actual->MultiSampleQuality=0;
    actual->EnableAutoDepthStencil=FALSE;

    // Mirror the production D3D11 ownership split: the game/source device is
    // not the visible presenter. Keep its swapchain windowed + IMMEDIATE so it
    // can never take the exclusive/VBlank ownership that belongs to the
    // isolated presenter. The game still sees its original parameters through
    // the proxy contract.
    actual->Windowed=TRUE;
    actual->BackBufferFormat=D3DFMT_UNKNOWN;
    actual->FullScreen_RefreshRateInHz=0;
    actual->SwapEffect=D3DSWAPEFFECT_DISCARD;
    actual->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    if(!actual->hDeviceWindow)
        actual->hDeviceWindow=g_ptarFocusWindow;
}

static HRESULT QueryRealBackBufferGeometry(
    IDirect3DDevice9* dev,
    UINT* width,UINT* height)
{
    if(!dev || !width || !height || !g_realGetBackBuffer)
        return D3DERR_INVALIDCALL;

    *width=0;
    *height=0;

    IDirect3DSurface9* backBuffer=0;
    HRESULT hr=g_realGetBackBuffer(
        dev,0,0,D3DBACKBUFFER_TYPE_MONO,&backBuffer);
    if(FAILED(hr) || !backBuffer)
        return FAILED(hr)?hr:E_FAIL;

    D3DSURFACE_DESC desc={};
    hr=backBuffer->GetDesc(&desc);
    backBuffer->Release();

    if(FAILED(hr) || !desc.Width || !desc.Height)
        return FAILED(hr)?hr:E_FAIL;

    *width=desc.Width;
    *height=desc.Height;
    return S_OK;
}

static bool FinalizeCurrentResolutionPlan(
    const PTARResolutionPlan& plan,
    IDirect3DDevice9* dev,
    UINT* sourceW,UINT* sourceH,
    UINT* outputW,UINT* outputH)
{
    UINT resolvedW=0;
    UINT resolvedH=0;
    HRESULT hr=QueryRealBackBufferGeometry(dev,&resolvedW,&resolvedH);
    if(FAILED(hr))
    {
        PtDiagLogA(
            "RESOLUTION_QUERY_BACKBUFFER_FAIL hr=0x%08lX",
            (unsigned long)hr);
        return false;
    }

    const bool spatialActive=PtResolutionFinalizePlan(
        &plan,resolvedW,resolvedH,
        sourceW,sourceH,outputW,outputH);

    PtDiagLogA(
        "RESOLUTION_PLAN requested=%ux%u device=%ux%u resolved=%ux%u mode=%s",
        plan.requestedW,plan.requestedH,
        plan.deviceW,plan.deviceH,
        resolvedW,resolvedH,
        spatialActive?"PTAR_X1.5":"NATIVE_1X1");
    return spatialActive;
}

static HRESULT InitializePTARResources(
    IDirect3DDevice9* dev,
    UINT sourceW,UINT sourceH,
    UINT outputW,UINT outputH,
    bool spatialActive,
    UINT outputRefreshHz,
    BOOL originalAutoDepth,
    D3DFORMAT originalDepthFormat)
{
    PtDiagStage("InitializePTARResources_ENTER");
    PtDiagLogA("INIT_RESOURCES dev=%p src=%ux%u out=%ux%u autoDepth=%ld depthFmt=%u",
        dev,sourceW,sourceH,outputW,outputH,(long)originalAutoDepth,(unsigned)originalDepthFormat);
    ReleasePTARResources();

    g_ptar.device=dev;
    g_ptar.sourceW=sourceW;
    g_ptar.sourceH=sourceH;
    g_ptar.outputW=outputW;
    g_ptar.outputH=outputH;
    g_ptar.spatialActive=spatialActive;

    if(outputRefreshHz<30u || outputRefreshHz>360u)
    {
        D3DDISPLAYMODE mode={};
        if(g_realGetDisplayMode &&
           SUCCEEDED(g_realGetDisplayMode(dev,0,&mode)) &&
           mode.RefreshRate>=30u &&
           mode.RefreshRate<=360u)
        {
            outputRefreshHz=mode.RefreshRate;
        }
        else
        {
            outputRefreshHz=60u;
        }
    }
    g_ptar.outputRefreshHz=outputRefreshHz;

    g_ptar.originalAutoDepth=originalAutoDepth;
    g_ptar.depthFormat=originalDepthFormat;

    D3DSURFACE_DESC desc={};
    PtDiagStage("Initialize_GetRealBackBuffer");
    HRESULT hr=g_realGetBackBuffer(dev,0,0,D3DBACKBUFFER_TYPE_MONO,&g_ptar.realBackBuffer);
    PtDiagLogA("INIT_GetBackBuffer hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.realBackBuffer);
    if(FAILED(hr) || !g_ptar.realBackBuffer) goto fail;

    PtDiagStage("Initialize_BackBufferGetDesc");
    hr=g_ptar.realBackBuffer->GetDesc(&desc);
    PtDiagLogA("INIT_GetDesc hr=0x%08lX w=%u h=%u fmt=%u ms=%u q=%lu",
        (unsigned long)hr,desc.Width,desc.Height,(unsigned)desc.Format,
        (unsigned)desc.MultiSampleType,(unsigned long)desc.MultiSampleQuality);
    if(FAILED(hr)) goto fail;
    g_ptar.outputFormat=desc.Format;

    PtDiagStage("Initialize_CreateSourceTexture");
    hr=dev->CreateTexture(
        sourceW,sourceH,1,D3DUSAGE_RENDERTARGET,
        desc.Format,D3DPOOL_DEFAULT,&g_ptar.sourceTexture,0);
    PtDiagLogA("INIT_CreateTexture hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.sourceTexture);
    if(FAILED(hr) || !g_ptar.sourceTexture) goto fail;

    PtDiagStage("Initialize_GetSourceSurface");
    hr=g_ptar.sourceTexture->GetSurfaceLevel(0,&g_ptar.sourceSurface);
    PtDiagLogA("INIT_GetSurfaceLevel hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.sourceSurface);
    if(FAILED(hr) || !g_ptar.sourceSurface) goto fail;

    if(originalAutoDepth)
    {
        PtDiagStage("Initialize_CreateVirtualDepth");
        hr=dev->CreateDepthStencilSurface(
            sourceW,sourceH,originalDepthFormat,
            D3DMULTISAMPLE_NONE,0,TRUE,&g_ptar.virtualDepth,0);
        PtDiagLogA("INIT_CreateDepth hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.virtualDepth);
        if(FAILED(hr) || !g_ptar.virtualDepth) goto fail;
    }

    PtDiagStage("Initialize_CreatePixelShader");
    hr=dev->CreatePixelShader((const DWORD*)g_ptarPs,&g_ptar.shader);
    PtDiagLogA("INIT_CreatePixelShader hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.shader);
    if(FAILED(hr) || !g_ptar.shader) goto fail;

    PtDiagStage("Initialize_CreateUniversalShader");
    hr=dev->CreatePixelShader((const DWORD*)g_ptarUniversalPs,&g_ptar.universalShader);
    PtDiagLogA("INIT_CreateUniversalShader hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.universalShader);
    if(FAILED(hr) || !g_ptar.universalShader) goto fail;

    PtDiagStage("Initialize_CreateBilinearShader");
    hr=dev->CreatePixelShader((const DWORD*)g_ptarBilinearPs,&g_ptar.bilinearShader);
    PtDiagLogA("INIT_CreateBilinearShader hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.bilinearShader);
    if(FAILED(hr) || !g_ptar.bilinearShader) goto fail;

    // GW16I HUD is raster-ported with D3D9 Clear(rects). The monolithic
    // ps_3_0 translation is not a runtime dependency: field hardware rejected
    // it with D3DERR_INVALIDCALL and previously caused all PTAR init to fail.
    PtHudLoadConfig(g_self);
    PtDiagLogA("INIT_GW16I_HUD renderer=D3D9_CLEAR_RECTS shader_dependency=NONE");

    PtDiagStage("Initialize_CreateCurrentReal");
    hr=CreateRenderTexture(
        dev,outputW,outputH,desc.Format,
        &g_ptar.currentRealTexture,&g_ptar.currentRealSurface);
    PtDiagLogA(
        "INIT_CurrentReal hr=0x%08lX tex=%p surf=%p",
        (unsigned long)hr,
        g_ptar.currentRealTexture,
        g_ptar.currentRealSurface);
    if(FAILED(hr)) goto fail;

    PtDiagStage("Initialize_CreateStateBlock");
    hr=dev->CreateStateBlock(D3DSBT_ALL,&g_ptar.stateBlock);
    PtDiagLogA("INIT_CreateStateBlock hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.stateBlock);
    if(FAILED(hr) || !g_ptar.stateBlock) goto fail;

    PtDiagStage("Initialize_IsolatedPresenter");
    {
        HRESULT presenterHr=PtIsoPresenterInitialize(
            dev,
            (PTARIsoPFN_Direct3DCreate9Ex)g_sysDirect3DCreate9Ex,
            g_self,
            g_ptarAdapter,
            g_ptarDeviceType,
            g_ptarFocusWindow,
            sourceW,sourceH,
            outputW,outputH,
            desc.Format,
            spatialActive,
            g_ptar.outputRefreshHz);

        PtDiagLogA(
            "INIT_IsolatedPresenter hr=0x%08lX active=%d "
            "transport=CPU_READBACK target_hz=%u",
            (unsigned long)presenterHr,
            PtIsoPresenterIsActive()?1:0,
            g_ptar.outputRefreshHz);

        // Fail-open: spatial PTAR remains usable even if the isolated display
        // path cannot start. FG remains disabled on the direct fallback rather
        // than reintroducing synchronous GENERATED Presents on the game thread.
    }

    PtDiagStage("Initialize_BindVirtualTargets");
    hr=dev->SetRenderTarget(0,g_ptar.sourceSurface);
    PtDiagLogA("INIT_SetRenderTarget hr=0x%08lX",(unsigned long)hr);
    if(FAILED(hr)) goto fail;
    hr=dev->SetDepthStencilSurface(g_ptar.virtualDepth);
    PtDiagLogA("INIT_SetDepthStencil hr=0x%08lX",(unsigned long)hr);
    if(FAILED(hr)) goto fail;
    hr=SetVirtualViewport(dev);
    PtDiagLogA("INIT_SetViewport hr=0x%08lX",(unsigned long)hr);
    if(FAILED(hr)) goto fail;

    PtFgPacerReset();
    PtRealGovernorReset(false);
    g_ptar.active=true;
    Log(L"PTAR_ACTIVE src=%ux%u out=%ux%u spatial=%s shader=MoE_v01_D3D9_PS3 samples=8 hud=CTRL_F11 fg=CTRL_F6 presenter=%s transport=CPU_READBACK targetVisible=%u",
        sourceW,sourceH,outputW,outputH,
        spatialActive?L"PTAR_X1.5":L"NATIVE_1X1",
        PtIsoPresenterIsActive()?L"ISOLATED":L"DIRECT_FALLBACK",
        g_ptar.outputRefreshHz);
    return S_OK;

fail:
    Log(L"PTAR_INIT_FAIL hr=0x%08X",(unsigned)hr);
    ReleasePTARResources();
    return FAILED(hr)?hr:E_FAIL;
}

static bool PatchVtableSlot(
    void* object,
    size_t index,
    void* hook,
    void** originalOut,
    const char* label)
{
    if(!object || !hook) return false;

    void** vtable=*(void***)object;
    if(!vtable) return false;

    void** slot=&vtable[index];
    void* current=*slot;

    if(current==hook)
    {
        // Preserve the previously captured real function. Returning the hook
        // through originalOut tells callers not to overwrite their saved
        // original with NULL when the shared D3D9 vtable was already patched.
        if(originalOut) *originalOut=hook;
        PtDiagLogA("VTABLE_PATCH_ALREADY label=%s object=%p vtable=%p index=%u",
            label?label:"<null>",object,vtable,(unsigned)index);
        return true;
    }

    DWORD oldProtect=0;
    PtDiagLogA(
        "VTABLE_PATCH_BEGIN label=%s object=%p vtable=%p index=%u old=%p hook=%p",
        label?label:"<null>",object,vtable,(unsigned)index,current,hook);

    if(!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&oldProtect))
    {
        PtDiagLogA(
            "VTABLE_PATCH_VPROTECT_FAIL label=%s gle=%lu",
            label?label:"<null>",(unsigned long)GetLastError());
        return false;
    }

    InterlockedExchangePointer((PVOID volatile*)slot,hook);

    DWORD ignored=0;
    if(!VirtualProtect(slot,sizeof(void*),oldProtect,&ignored))
    {
        PtDiagLogA(
            "VTABLE_PATCH_RESTORE_PROTECT_FAIL label=%s gle=%lu",
            label?label:"<null>",(unsigned long)GetLastError());
    }

    if(originalOut) *originalOut=current;

    PtDiagLogA(
        "VTABLE_PATCH_DONE label=%s index=%u now=%p preserved=%p",
        label?label:"<null>",(unsigned)index,*slot,current);
    return *slot==hook;
}

static HRESULT STDMETHODCALLTYPE HookGetDisplayMode(
    IDirect3DDevice9* self,UINT swap,D3DDISPLAYMODE* mode)
{
    HRESULT hr=g_realGetDisplayMode(self,swap,mode);
    if(SUCCEEDED(hr) && mode && g_ptar.active && self==g_ptar.device)
    {
        mode->Width=g_ptar.sourceW;
        mode->Height=g_ptar.sourceH;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookGetBackBuffer(
    IDirect3DDevice9* self,UINT swap,UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out)
{
    if(out) *out=0;
    if(g_ptar.active && self==g_ptar.device && swap==0 && index==0 &&
       type==D3DBACKBUFFER_TYPE_MONO && g_ptar.sourceSurface && out)
    {
        g_ptar.sourceSurface->AddRef();
        *out=g_ptar.sourceSurface;
        return S_OK;
    }
    return g_realGetBackBuffer(self,swap,index,type,out);
}

static HRESULT STDMETHODCALLTYPE HookSetRenderTarget(
    IDirect3DDevice9* self,DWORD index,IDirect3DSurface9* surface)
{
    if(g_ptar.active && self==g_ptar.device && index==0 &&
       surface && surface==g_ptar.realBackBuffer && g_ptar.sourceSurface)
        surface=g_ptar.sourceSurface;
    return g_realSetRenderTarget(self,index,surface);
}

struct QuadVertex
{
    float x,y,z,rhw;
    float u,v;
};

static HRESULT DrawFullscreenPassRect(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT targetW,
    UINT targetH,
    UINT drawX,
    UINT drawY,
    UINT drawW,
    UINT drawH,
    bool clearTarget,
    IDirect3DPixelShader9* shader,
    IDirect3DTexture9* texture0,
    IDirect3DTexture9* texture1,
    IDirect3DTexture9* texture2,
    const float* constant0)
{
    if(!dev || !target || !targetW || !targetH ||
       !drawW || !drawH || !shader || !texture0 ||
       drawX+drawW>targetW || drawY+drawH>targetH)
        return D3DERR_INVALIDCALL;

    // Always unbind shader resources before selecting a new render target so a
    // history texture can safely become the destination of a later pass.
    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    dev->SetTexture(2,0);

    HRESULT hr=dev->SetDepthStencilSurface(0);
    if(FAILED(hr)) return hr;

    hr=g_realSetRenderTarget(dev,0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={};
    vp.X=0;
    vp.Y=0;
    vp.Width=targetW;
    vp.Height=targetH;
    vp.MinZ=0.0f;
    vp.MaxZ=1.0f;
    hr=dev->SetViewport(&vp);
    if(FAILED(hr)) return hr;

    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
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

    if(clearTarget)
    {
        hr=dev->Clear(
            0,0,D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0,0,0),
            1.0f,0);
        if(FAILED(hr)) return hr;
    }

    if(constant0)
        dev->SetPixelShaderConstantF(0,constant0,1);

    dev->SetPixelShader(shader);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetTexture(0,texture0);
    dev->SetTexture(1,texture1);
    dev->SetTexture(2,texture2);

    hr=dev->BeginScene();
    if(FAILED(hr))
    {
        dev->SetTexture(0,0);
        dev->SetTexture(1,0);
        dev->SetTexture(2,0);
        return hr;
    }

    const float x=(float)drawX;
    const float y=(float)drawY;
    const float w=(float)drawW;
    const float h=(float)drawH;
    QuadVertex q[4]={
        {x-0.5f,y-0.5f,0.0f,1.0f,0.0f,0.0f},
        {x+w-0.5f,y-0.5f,0.0f,1.0f,1.0f,0.0f},
        {x-0.5f,y+h-0.5f,0.0f,1.0f,0.0f,1.0f},
        {x+w-0.5f,y+h-0.5f,0.0f,1.0f,1.0f,1.0f}
    };

    hr=dev->DrawPrimitiveUP(
        D3DPT_TRIANGLESTRIP,2,q,sizeof(QuadVertex));

    HRESULT endHr=dev->EndScene();

    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    dev->SetTexture(2,0);

    if(FAILED(hr))
        return hr;
    return endHr;
}

static HRESULT DrawFullscreenPass(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT targetW,
    UINT targetH,
    IDirect3DPixelShader9* shader,
    IDirect3DTexture9* texture0,
    IDirect3DTexture9* texture1,
    IDirect3DTexture9* texture2,
    const float* constant0)
{
    return DrawFullscreenPassRect(
        dev,target,targetW,targetH,
        0,0,targetW,targetH,false,
        shader,texture0,texture1,texture2,constant0);
}

static HRESULT RenderSpatialToCurrent(IDirect3DDevice9* dev)
{
    if(!g_ptar.spatialActive)
    {
        // Native 1:1 mode is not "PTAR disabled". The proxy, HUD, presenter
        // and FG remain active; only spatial reconstruction is bypassed.
        HRESULT hr=dev->StretchRect(
            g_ptar.sourceSurface,0,
            g_ptar.currentRealSurface,0,
            D3DTEXF_NONE);
        if(SUCCEEDED(hr))
            return hr;

        PtDiagLogA(
            "NATIVE_1X1_STRETCHRECT_FALLBACK hr=0x%08lX",
            (unsigned long)hr);
        return DrawFullscreenPass(
            dev,
            g_ptar.currentRealSurface,
            g_ptar.outputW,
            g_ptar.outputH,
            g_ptar.bilinearShader,
            g_ptar.sourceTexture,
            0,0,0);
    }

    const PTARPresentationRect fit=PtResolutionAspectFit(
        g_ptar.sourceW,g_ptar.sourceH,
        g_ptar.outputW,g_ptar.outputH);

    const bool exact15=PtResolutionExactScale15(
        g_ptar.sourceW,g_ptar.sourceH,
        fit.width,fit.height);

    IDirect3DPixelShader9* spatialShader=g_ptar.bilinearShader;
    const char* spatialMode="BILINEAR";
    if(PtHudUseMoe())
    {
        spatialShader=exact15?g_ptar.shader:g_ptar.universalShader;
        spatialMode=exact15?"MOE_X1.5":"MOE_UNIVERSAL";
    }

    float sizes[4]={
        (float)g_ptar.sourceW,
        (float)g_ptar.sourceH,
        (float)fit.width,
        (float)fit.height};

    PtDiagLogA(
        "SPATIAL_PASS mode=%s src=%ux%u fit=%u,%u %ux%u target=%ux%u",
        spatialMode,
        g_ptar.sourceW,g_ptar.sourceH,
        fit.x,fit.y,fit.width,fit.height,
        g_ptar.outputW,g_ptar.outputH);

    return DrawFullscreenPassRect(
        dev,
        g_ptar.currentRealSurface,
        g_ptar.outputW,
        g_ptar.outputH,
        fit.x,fit.y,fit.width,fit.height,
        true,
        spatialShader,
        g_ptar.sourceTexture,
        0,
        0,
        sizes);
}

static unsigned long g_ptarHudFrameSequence=0;


static HRESULT RenderGW16IProductionHud(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* targetSurface,
    bool generatedFrame,
    bool fgProducing)
{
    if(!dev || !targetSurface)
        return D3DERR_INVALIDCALL;

    // Make the final presenter backbuffer explicit. Clear(rects) then writes
    // the exact GW16I panel/glyph raster without depending on PS3 support or
    // on the game's inherited shader/raster state.
    HRESULT hr=dev->SetDepthStencilSurface(0);
    if(FAILED(hr))
        return hr;

    hr=g_realSetRenderTarget(dev,0,targetSurface);
    if(FAILED(hr))
        return hr;

    D3DVIEWPORT9 vp={};
    vp.X=0; vp.Y=0;
    vp.Width=g_ptar.outputW;
    vp.Height=g_ptar.outputH;
    vp.MinZ=0.0f; vp.MaxZ=1.0f;
    hr=dev->SetViewport(&vp);
    if(FAILED(hr))
        return hr;

    const PTARPresentationRect fit=PtResolutionAspectFit(
        g_ptar.sourceW,g_ptar.sourceH,
        g_ptar.outputW,g_ptar.outputH);
    const bool exact15=
        g_ptar.spatialActive &&
        PtResolutionExactScale15(
            g_ptar.sourceW,g_ptar.sourceH,
            fit.width,fit.height);

    ++g_ptarHudFrameSequence;

    hr=PtGw16RenderExactHudD3D9(
        dev,
        PtHudVisible(),
        PtHudStateNotice(),
        PtHudMarkerEnabled(),
        g_ptarHudFrameSequence&4095ul,
        generatedFrame,
        PtHudDisplayFps(fgProducing),
        g_ptar.sourceW,
        g_ptar.sourceH,
        PtHudFilterId(g_ptar.spatialActive,exact15),
        PtHudFeedbackType(),
        PtHudFeedbackArgA(),
        PtHudFeedbackArgB(),
        PtHudFeedbackArgC());

    if(FAILED(hr))
    {
        PtDiagLogA(
            "GW16I_HUD_DRAW_FAIL renderer=CLEAR_RECTS hr=0x%08lX",
            (unsigned long)hr);
        return hr;
    }

    if(PtHudConsumeStatusLogPending())
    {
        PtDiagLogA(
            "STATUS_RUNTIME fg=%s profile=%d src=%ux%u out=%ux%u "
            "filter=%d real_fps=%.3f visible_fps=%.3f "
            "real_count=%lu gen_count=%lu resyncs=%lu late_skip=%lu",
            PtHudFgEnabled()?"ON":"OFF",
            PtHudFgProfile(),
            g_ptar.sourceW,g_ptar.sourceH,
            g_ptar.outputW,g_ptar.outputH,
            PtHudFilterId(g_ptar.spatialActive,exact15),
            PtHudRealFps(),PtHudDisplayFps(fgProducing),
            PtFgPacerRealCount(),
            PtFgPacerGeneratedCount(),
            g_ptarFgPacer.resyncs,
            PtFgPacerLateSkipCount());
    }

    const LONGLONG now=PtHudNow();
    if(g_ptarHudLastFpsLogQpc==0 ||
       now-g_ptarHudLastFpsLogQpc>=g_ptarHudFreq.QuadPart)
    {
        g_ptarHudLastFpsLogQpc=now;
        PtDiagLogA(
            "FPS_WALLCLOCK_SAMPLE real_fps=%.3f visible_fps=%.3f "
            "fg=%s real_count=%lu gen_count=%lu resyncs=%lu",
            PtHudRealFps(),
            PtHudDisplayFps(fgProducing),
            PtHudFgEnabled()?"ON":"OFF",
            PtFgPacerRealCount(),
            PtFgPacerGeneratedCount(),
            g_ptarFgPacer.resyncs);
    }

    return S_OK;
}

static HRESULT ComposePTARFrameToSurface(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* sourceSurface,
    IDirect3DSurface9* targetSurface,
    bool generatedFrame,
    bool fgProducing)
{
    if(!dev || !sourceSurface || !targetSurface)
        return D3DERR_INVALIDCALL;

    // REAL/GENERATED are already output-sized render targets. A direct GPU
    // copy avoids another fullscreen shader pass before mailbox submission.
    HRESULT hr=dev->StretchRect(
        sourceSurface,0,
        targetSurface,0,
        D3DTEXF_NONE);
    if(FAILED(hr))
        return hr;

    HRESULT hudHr=RenderGW16IProductionHud(
        dev,targetSurface,
        generatedFrame,fgProducing);
    if(FAILED(hudHr))
    {
        PtDiagLogA(
            "GW16I_HUD_FAILOPEN hr=0x%08lX",
            (unsigned long)hudHr);
    }

    // Capture the exact composed mailbox frame that will be submitted by the
    // presenter. This retains the production post-overlay F9 contract.
    if(PtCaptureConsumeRequest())
    {
        wchar_t saved[MAX_PATH]={0};
        HRESULT captureHr=PtCaptureSavePostOverlayBmp(
            dev,targetSurface,g_self,
            saved,_countof(saved));

        if(SUCCEEDED(captureHr))
        {
            PtHudNotifyCaptureSaved();
            PtDiagLogA(
                "OK: F9 post-overlay D3D9 screenshot saved path=%ls frame=%s",
                saved,
                generatedFrame?"GENERATED":"REAL");
        }
        else
        {
            PtDiagLogA(
                "ERROR: F9 D3D9 post-overlay screenshot failed hr=0x%08lX",
                (unsigned long)captureHr);
        }
    }

    return S_OK;
}

static HRESULT PresentPTARFrameDirectFallback(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* sourceSurface,
    HWND hwnd,
    bool generatedFrame,
    bool fgProducing)
{
    if(!dev || !sourceSurface || !g_ptar.realBackBuffer)
        return D3DERR_INVALIDCALL;

    HRESULT hr=ComposePTARFrameToSurface(
        dev,sourceSurface,g_ptar.realBackBuffer,
        generatedFrame,fgProducing);
    if(FAILED(hr))
        return hr;

    hr=g_realPresent(dev,0,0,hwnd,0);
    if(SUCCEEDED(hr))
        PtFgPacerRecordVisible(generatedFrame);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookPresent(
    IDirect3DDevice9* self,
    const RECT* src,
    const RECT* dst,
    HWND hwnd,
    const RGNDATA* dirty)
{
    if(!g_ptar.active || self!=g_ptar.device || g_ptar.inPresent)
        return g_realPresent(self,src,dst,hwnd,dirty);

    g_ptar.inPresent=true;
    PtHudFrameTick();

    const unsigned long sequence=++g_ptar.sourceSequence;
    HRESULT result=S_OK;

    HRESULT captureHr=g_ptar.stateBlock?
        g_ptar.stateBlock->Capture():E_FAIL;

    if(FAILED(captureHr))
    {
        PtDiagLogA(
            "PRESENT_STATE_CAPTURE_FAIL hr=0x%08lX fallback=DIRECT",
            (unsigned long)captureHr);

        result=g_realPresent(self,src,dst,hwnd,dirty);
        PtFgPacerRecordVisible(false);
        g_ptar.inPresent=false;
        PtRealGovernorWaitAfterSourceFrame(false);
        return result;
    }

    PtDiagStage(
        g_ptar.spatialActive?
            "SPATIAL_CURRENT_REAL":
            "NATIVE_1X1_CURRENT_REAL");

    HRESULT spatialHr=RenderSpatialToCurrent(self);

    if(FAILED(spatialHr))
    {
        PtDiagLogA(
            "SPATIAL_CURRENT_REAL_FAIL hr=0x%08lX fallback=DIRECT",
            (unsigned long)spatialHr);

        result=g_realPresent(self,src,dst,hwnd,dirty);
        PtFgPacerRecordVisible(false);
        goto restore_game_state;
    }

    if(PtIsoPresenterIsActive())
    {
        PtDiagStage("ISOLATED_SUBMIT_REAL");

        HRESULT submitHr=PtIsoSubmitReal(
            g_ptar.currentRealSurface,
            sequence);

        if(SUCCEEDED(submitHr))
        {
            // The source frame is handed off. Physical Sync1 Present and all
            // GENERATED work now happen exclusively on the presenter device.
            result=S_OK;
        }
        else
        {
            PtDiagLogA(
                "ISOLATED_SUBMIT_REAL_FAIL seq=%lu hr=0x%08lX fallback=DIRECT",
                sequence,
                (unsigned long)submitHr);

            result=PresentPTARFrameDirectFallback(
                self,
                g_ptar.currentRealSurface,
                hwnd,
                false,
                false);
        }
    }
    else
    {
        // F10 / presenter-init fail-open path. Never run FG synchronously.
        PtDiagStage("PRESENT_REAL_DIRECT_FALLBACK");
        result=PresentPTARFrameDirectFallback(
            self,
            g_ptar.currentRealSurface,
            hwnd,
            false,
            false);
    }

restore_game_state:
    if(g_ptar.stateBlock)
        g_ptar.stateBlock->Apply();

    if(g_ptar.sourceSurface)
        g_realSetRenderTarget(
            self,0,g_ptar.sourceSurface);

    self->SetDepthStencilSurface(
        g_ptar.virtualDepth);

    SetVirtualViewport(self);

    PtDiagStage("PRESENT_RETURN_TO_GAME");
    g_ptar.inPresent=false;

    // Exact production governor semantics:
    //   presenter+FG ON -> target60 policy -> up to 30 REAL/s;
    //   FG OFF / direct fallback -> target120 policy -> up to 60 REAL/s.
    PtRealGovernorWaitAfterSourceFrame(
        PtHudFgEnabled() &&
        PtIsoPresenterIsActive());

    return result;
}

static HRESULT STDMETHODCALLTYPE HookReset(
    IDirect3DDevice9* self,
    D3DPRESENT_PARAMETERS* pp)
{
    if(!pp)
        return D3DERR_INVALIDCALL;

    const D3DPRESENT_PARAMETERS original=*pp;

    if(original.hDeviceWindow)
        g_ptarFocusWindow=original.hDeviceWindow;

    PtResolutionLoadConfig(g_self);
    PTARResolutionPlan plan={};
    PtResolutionBuildPlan(
        original.BackBufferWidth,
        original.BackBufferHeight,
        &plan);

    ReleasePTARResources();

    D3DPRESENT_PARAMETERS actual={};
    PreparePTARPresentationParameters(original,plan,&actual);

    PtDiagLogA(
        "RESET_PLAN requested=%ux%u plannedDevice=%ux%u spatialRequested=%d windowed=%ld",
        original.BackBufferWidth,original.BackBufferHeight,
        plan.deviceW,plan.deviceH,
        plan.spatialRequested?1:0,
        (long)original.Windowed);

    HRESULT hr=g_realReset(self,&actual);
    *pp=original;

    if(FAILED(hr) && plan.spatialRequested)
    {
        // Spatial negotiation is allowed to fail soft, but PTAR itself must
        // remain available. Retry the exact game resolution in native 1:1.
        PtDiagLogA(
            "RESET_SPATIAL_DEVICE_FAIL hr=0x%08lX retry=NATIVE_1X1",
            (unsigned long)hr);
        plan.spatialRequested=false;
        plan.deviceW=original.BackBufferWidth;
        plan.deviceH=original.BackBufferHeight;
        PreparePTARPresentationParameters(original,plan,&actual);
        hr=g_realReset(self,&actual);
        *pp=original;
    }

    if(FAILED(hr))
    {
        // Last-resort game-compatible reset. This preserves the game even when
        // the backend cannot virtualize an unusual presentation mode.
        Log(L"RESET_PTAR_DEVICE_FAIL hr=0x%08X retry_exact_game",(unsigned)hr);
        D3DPRESENT_PARAMETERS fallback=original;
        return g_realReset(self,&fallback);
    }

    UINT sourceW=0,sourceH=0,outputW=0,outputH=0;
    bool spatialActive=FinalizeCurrentResolutionPlan(
        plan,self,&sourceW,&sourceH,&outputW,&outputH);

    if(!sourceW || !sourceH || !outputW || !outputH)
    {
        Log(L"RESET_RESOLUTION_RESOLVE_FAIL retry_exact_game");
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT fallbackHr=g_realReset(self,&fallback);
        *pp=original;
        return fallbackHr;
    }

    hr=InitializePTARResources(
        self,sourceW,sourceH,outputW,outputH,
        spatialActive,
        original.FullScreen_RefreshRateInHz,
        original.EnableAutoDepthStencil,
        original.AutoDepthStencilFormat);

    if(FAILED(hr) && spatialActive)
    {
        // Mirror the D3D11 UniversalSpatialPresenter contract: a spatial
        // failure drops only x1.5 reconstruction, not the PTAR backend.
        PtDiagLogA(
            "RESET_SPATIAL_INIT_FAIL hr=0x%08lX retry=NATIVE_1X1",
            (unsigned long)hr);

        PTARResolutionPlan nativePlan=plan;
        nativePlan.spatialRequested=false;
        nativePlan.deviceW=original.BackBufferWidth;
        nativePlan.deviceH=original.BackBufferHeight;
        PreparePTARPresentationParameters(original,nativePlan,&actual);

        HRESULT nativeResetHr=g_realReset(self,&actual);
        *pp=original;
        if(SUCCEEDED(nativeResetHr))
        {
            sourceW=sourceH=outputW=outputH=0;
            spatialActive=FinalizeCurrentResolutionPlan(
                nativePlan,self,&sourceW,&sourceH,&outputW,&outputH);
            if(sourceW && sourceH && outputW && outputH)
            {
                hr=InitializePTARResources(
                    self,sourceW,sourceH,outputW,outputH,
                    spatialActive,
                    original.FullScreen_RefreshRateInHz,
                    original.EnableAutoDepthStencil,
                    original.AutoDepthStencilFormat);
            }
            else
            {
                hr=E_FAIL;
            }
        }
        else
        {
            hr=nativeResetHr;
        }
    }

    if(FAILED(hr))
    {
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT fallbackHr=g_realReset(self,&fallback);
        *pp=original;
        Log(L"RESET_PTAR_INIT_FAIL fallback=0x%08X",(unsigned)fallbackHr);
        return fallbackHr;
    }

    return S_OK;
}

static bool HookDevice(IDirect3DDevice9* dev)
{
    PtDiagStage("HookDevice_ENTER");
    PtDiagLogA("HOOK_DEVICE dev=%p",dev);
    if(!dev) return false;
    if(g_ptar.device && g_ptar.device!=dev)
    {
        Log(L"SECOND_DEVICE_UNSUPPORTED passthrough");
        return false;
    }

    PtDiagStage("HookDevice_PatchOriginalVtable");
    void** vt=*(void***)dev;
    PtDiagLogA("HOOK_DEVICE_ORIGINAL_VTABLE=%p",vt);
    if(!vt) return false;

    void* old=0;

    if(!PatchVtableSlot(dev,8,(void*)&HookGetDisplayMode,&old,"Device.GetDisplayMode"))
        return false;
    if(old!=(void*)&HookGetDisplayMode) g_realGetDisplayMode=(PFN_GetDisplayMode)old;

    old=0;
    if(!PatchVtableSlot(dev,16,(void*)&HookReset,&old,"Device.Reset"))
        return false;
    if(old!=(void*)&HookReset) g_realReset=(PFN_Reset)old;

    old=0;
    if(!PatchVtableSlot(dev,17,(void*)&HookPresent,&old,"Device.Present"))
        return false;
    if(old!=(void*)&HookPresent) g_realPresent=(PFN_Present)old;

    old=0;
    if(!PatchVtableSlot(dev,18,(void*)&HookGetBackBuffer,&old,"Device.GetBackBuffer"))
        return false;
    if(old!=(void*)&HookGetBackBuffer) g_realGetBackBuffer=(PFN_GetBackBuffer)old;

    old=0;
    if(!PatchVtableSlot(dev,37,(void*)&HookSetRenderTarget,&old,"Device.SetRenderTarget"))
        return false;
    if(old!=(void*)&HookSetRenderTarget) g_realSetRenderTarget=(PFN_SetRenderTarget)old;

    PtDiagStage("HookDevice_DONE");
    PtDiagLogA(
        "HOOK_DEVICE_DONE vtable=%p present=%p reset=%p getbb=%p setrt=%p",
        vt,(void*)g_realPresent,(void*)g_realReset,
        (void*)g_realGetBackBuffer,(void*)g_realSetRenderTarget);
    return true;
}

static HRESULT STDMETHODCALLTYPE HookCreateDevice(
    IDirect3D9* self,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
    D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out)
{
    PtDiagStage("HookCreateDevice_ENTER");
    PtDiagLogA(
        "CREATEDEVICE_CALL self=%p adapter=%u type=%u focus=%p flags=0x%08lX pp=%p out=%p",
        self,adapter,(unsigned)type,focus,(unsigned long)flags,pp,out);

    if(!pp || !out)
        return D3DERR_INVALIDCALL;
    *out=0;

    const D3DPRESENT_PARAMETERS original=*pp;

    g_ptarAdapter=adapter;
    g_ptarDeviceType=type;
    g_ptarFocusWindow=
        focus?focus:original.hDeviceWindow;

    PtDiagLogA(
        "CREATEDEVICE_PP w=%u h=%u fmt=%u count=%u ms=%u msq=%lu swap=%u hwnd=%p windowed=%ld "
        "autodepth=%ld depthfmt=%u refresh=%u interval=0x%08lX",
        original.BackBufferWidth,original.BackBufferHeight,
        (unsigned)original.BackBufferFormat,
        original.BackBufferCount,
        (unsigned)original.MultiSampleType,
        (unsigned long)original.MultiSampleQuality,
        (unsigned)original.SwapEffect,
        original.hDeviceWindow,
        (long)original.Windowed,
        (long)original.EnableAutoDepthStencil,
        (unsigned)original.AutoDepthStencilFormat,
        original.FullScreen_RefreshRateInHz,
        (unsigned long)original.PresentationInterval);

    PtResolutionLoadConfig(g_self);

    if(!g_ptarResolutionPolicy.enabled)
    {
        PtDiagLogA("CREATEDEVICE_PTAR_DISABLED_BY_CONFIG");
        return g_realCreateDevice(self,adapter,type,focus,flags,pp,out);
    }

    PTARResolutionPlan plan={};
    PtResolutionBuildPlan(
        original.BackBufferWidth,
        original.BackBufferHeight,
        &plan);

    D3DPRESENT_PARAMETERS actual={};
    PreparePTARPresentationParameters(original,plan,&actual);

    PtDiagLogA(
        "CREATEDEVICE_PLAN requested=%ux%u plannedDevice=%ux%u spatialRequested=%d "
        "windowed=%ld game_flags=0x%08lX "
        "game_interval=0x%08lX producer_interval=0x%08lX "
        "producer_windowed=%ld",
        original.BackBufferWidth,original.BackBufferHeight,
        plan.deviceW,plan.deviceH,
        plan.spatialRequested?1:0,
        (long)original.Windowed,
        (unsigned long)flags,
        (unsigned long)original.PresentationInterval,
        (unsigned long)actual.PresentationInterval,
        (long)actual.Windowed);

    HRESULT hr=g_realCreateDevice(
        self,adapter,type,focus,flags,&actual,out);
    *pp=original;

    if((FAILED(hr) || !*out) && plan.spatialRequested)
    {
        // A spatial target failure must not disable PTAR/FG. Retry native 1:1.
        PtDiagLogA(
            "CREATEDEVICE_SPATIAL_DEVICE_FAIL hr=0x%08lX retry=NATIVE_1X1",
            (unsigned long)hr);

        plan.spatialRequested=false;
        plan.deviceW=original.BackBufferWidth;
        plan.deviceH=original.BackBufferHeight;
        PreparePTARPresentationParameters(original,plan,&actual);

        *out=0;
        hr=g_realCreateDevice(
            self,adapter,type,focus,flags,&actual,out);
        *pp=original;
    }

    if(FAILED(hr) || !*out)
    {
        // Preserve game compatibility if the virtual-target form itself is
        // unsupported. This is the only path where PTAR cannot attach.
        PtDiagLogA(
            "CREATEDEVICE_VIRTUAL_TARGET_FAIL hr=0x%08lX retry=EXACT_GAME",
            (unsigned long)hr);
        *out=0;
        D3DPRESENT_PARAMETERS fallback=original;
        hr=g_realCreateDevice(
            self,adapter,type,focus,flags,&fallback,out);
        *pp=original;
        return hr;
    }

    IDirect3DDevice9* dev=*out;
    void** vt=*(void***)dev;
    if(!vt)
    {
        *pp=original;
        return S_OK;
    }

    g_realGetDisplayMode=(PFN_GetDisplayMode)vt[8];
    g_realReset=(PFN_Reset)vt[16];
    g_realPresent=(PFN_Present)vt[17];
    g_realGetBackBuffer=(PFN_GetBackBuffer)vt[18];
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];

    UINT sourceW=0,sourceH=0,outputW=0,outputH=0;
    bool spatialActive=FinalizeCurrentResolutionPlan(
        plan,dev,&sourceW,&sourceH,&outputW,&outputH);

    if(!sourceW || !sourceH || !outputW || !outputH)
    {
        Log(L"CREATEDEVICE_RESOLUTION_RESOLVE_FAIL passthrough");
        *pp=original;
        return S_OK;
    }

    hr=InitializePTARResources(
        dev,sourceW,sourceH,outputW,outputH,
        spatialActive,
        original.FullScreen_RefreshRateInHz,
        original.EnableAutoDepthStencil,
        original.AutoDepthStencilFormat);

    if(FAILED(hr) && spatialActive)
    {
        // If only the x1.5 path fails, recreate the presentation domain at the
        // game's requested resolution and keep PTAR active in native 1:1.
        PtDiagLogA(
            "CREATEDEVICE_SPATIAL_INIT_FAIL hr=0x%08lX retry=NATIVE_1X1",
            (unsigned long)hr);

        PTARResolutionPlan nativePlan=plan;
        nativePlan.spatialRequested=false;
        nativePlan.deviceW=original.BackBufferWidth;
        nativePlan.deviceH=original.BackBufferHeight;
        PreparePTARPresentationParameters(original,nativePlan,&actual);

        HRESULT resetHr=g_realReset(dev,&actual);
        *pp=original;
        if(SUCCEEDED(resetHr))
        {
            sourceW=sourceH=outputW=outputH=0;
            spatialActive=FinalizeCurrentResolutionPlan(
                nativePlan,dev,&sourceW,&sourceH,&outputW,&outputH);
            if(sourceW && sourceH && outputW && outputH)
            {
                hr=InitializePTARResources(
                    dev,sourceW,sourceH,outputW,outputH,
                    spatialActive,
                    original.FullScreen_RefreshRateInHz,
                    original.EnableAutoDepthStencil,
                    original.AutoDepthStencilFormat);
            }
            else
            {
                hr=E_FAIL;
            }
        }
        else
        {
            hr=resetHr;
        }
    }

    if(FAILED(hr))
    {
        // Keep the created game device alive even when PTAR resources fail.
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT resetHr=g_realReset(dev,&fallback);
        Log(L"CREATEDEVICE_PTAR_INIT_FAIL native_reset=0x%08X",(unsigned)resetHr);
        *pp=original;
        return S_OK;
    }

    if(!HookDevice(dev))
    {
        ReleasePTARResources();
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT resetHr=g_realReset(dev,&fallback);
        Log(L"CREATEDEVICE_HOOK_FAIL native_reset=0x%08X",(unsigned)resetHr);
        *pp=original;
        return S_OK;
    }

    *pp=original;
    return S_OK;
}

static bool HookD3D9(IDirect3D9* d3d)
{
    PtDiagStage("HookD3D9_ENTER");
    PtDiagLogA("HOOK_D3D9 object=%p",d3d);
    if(!d3d) return false;

    void** vt=*(void***)d3d;
    PtDiagLogA("HOOK_D3D9_ORIGINAL_VTABLE=%p",vt);
    if(!vt) return false;

    void* old=0;
    if(!PatchVtableSlot(
        d3d,16,(void*)&HookCreateDevice,&old,"IDirect3D9.CreateDevice"))
        return false;

    if(old!=(void*)&HookCreateDevice)
        g_realCreateDevice=(PFN_CreateDevice)old;

    PtDiagLogA(
        "HOOK_D3D9_PATCHED vtable=%p createDeviceReal=%p createDeviceNow=%p",
        vt,(void*)g_realCreateDevice,vt[16]);
    PtDiagStage("HookD3D9_DONE");
    return true;
}

extern "C" __declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk)
{
    PtDiagStage("Direct3DCreate9_ENTER");
    PtDiagLogA("Direct3DCreate9 sdk=%u",sdk);
    if(!EnsureRealD3D9())
    {
        PtDiagLogA("Direct3DCreate9 real runtime unavailable");
        return 0;
    }
    PtDiagStage("Direct3DCreate9_CallReal");
    IDirect3D9* d3d=g_sysDirect3DCreate9(sdk);
    PtDiagLogA("Direct3DCreate9 real returned=%p",d3d);
    if(d3d)
    {
        PtDiagStage("Direct3DCreate9_HookObject");
        const bool hooked=HookD3D9(d3d);
        PtDiagLogA("Direct3DCreate9 hook_result=%d",hooked?1:0);
        Log(L"PROXY_LOADED arch=x86 api=D3D9");
    }
    PtDiagStage("Direct3DCreate9_RETURN");
    return d3d;
}

extern "C" __declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk,IDirect3D9Ex** out)
{
    if(!out) return E_POINTER;
    *out=0;
    if(!EnsureRealD3D9() || !g_sysDirect3DCreate9Ex) return E_NOTIMPL;

    HRESULT hr=g_sysDirect3DCreate9Ex(sdk,out);
    if(SUCCEEDED(hr) && *out)
        Log(L"PROXY_LOADED arch=x86 api=D3D9Ex passthrough");
    return hr;
}

extern "C" __declspec(dllexport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR c,LPCWSTR n)
{
    typedef int (WINAPI *F)(D3DCOLOR,LPCWSTR);
    F f=(F)RealProc("D3DPERF_BeginEvent");
    return f?f(c,n):-1;
}

extern "C" __declspec(dllexport) int WINAPI D3DPERF_EndEvent()
{
    typedef int (WINAPI *F)();
    F f=(F)RealProc("D3DPERF_EndEvent");
    return f?f():-1;
}

extern "C" __declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus()
{
    typedef DWORD (WINAPI *F)();
    F f=(F)RealProc("D3DPERF_GetStatus");
    return f?f():0;
}

extern "C" __declspec(dllexport) BOOL WINAPI D3DPERF_QueryRepeatFrame()
{
    typedef BOOL (WINAPI *F)();
    F f=(F)RealProc("D3DPERF_QueryRepeatFrame");
    return f?f():FALSE;
}

extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR c,LPCWSTR n)
{
    typedef void (WINAPI *F)(D3DCOLOR,LPCWSTR);
    F f=(F)RealProc("D3DPERF_SetMarker");
    if(f) f(c,n);
}

extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD o)
{
    typedef void (WINAPI *F)(DWORD);
    F f=(F)RealProc("D3DPERF_SetOptions");
    if(f) f(o);
}

extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR c,LPCWSTR n)
{
    typedef void (WINAPI *F)(D3DCOLOR,LPCWSTR);
    F f=(F)RealProc("D3DPERF_SetRegion");
    if(f) f(c,n);
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID)
{
    if(reason==DLL_PROCESS_ATTACH)
    {
        g_self=h;
        PtDiagInit(h);
        PtDiagStage("DllMain_PROCESS_ATTACH");
        DisableThreadLibraryCalls(h);
        PtDiagLogA("DLL_ATTACH_COMPLETE");
    }
    return TRUE;
}
