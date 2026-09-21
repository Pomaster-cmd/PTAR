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
#include "ptar_bilinear_ps_bytecode.h"
#include "ptar_fg_me_coarse_ps_bytecode.h"
#include "ptar_fg_me_refine_ps_bytecode.h"
#include "ptar_fg_interpolate_ps_bytecode.h"
#include "ptar_diag.h"
#include "ptar_hud.h"

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

struct PTARContext
{
    IDirect3DDevice9* device;
    IDirect3DTexture9* sourceTexture;
    IDirect3DSurface9* sourceSurface;
    IDirect3DSurface9* virtualDepth;
    IDirect3DSurface9* realBackBuffer;
    IDirect3DPixelShader9* shader;
    IDirect3DPixelShader9* bilinearShader;
    IDirect3DPixelShader9* fgMeCoarseShader;
    IDirect3DPixelShader9* fgMeRefineShader;
    IDirect3DPixelShader9* fgInterpolateShader;
    IDirect3DTexture9* previousRealTexture;
    IDirect3DSurface9* previousRealSurface;
    IDirect3DTexture9* currentRealTexture;
    IDirect3DSurface9* currentRealSurface;
    IDirect3DTexture9* generatedTexture;
    IDirect3DSurface9* generatedSurface;
    IDirect3DTexture9* motionCoarseTexture;
    IDirect3DSurface9* motionCoarseSurface;
    IDirect3DTexture9* motionFineTexture;
    IDirect3DSurface9* motionFineSurface;
    IDirect3DStateBlock9* stateBlock;
    UINT sourceW;
    UINT sourceH;
    UINT outputW;
    UINT outputH;
    UINT motionCoarseW;
    UINT motionCoarseH;
    UINT motionFineW;
    UINT motionFineH;
    D3DFORMAT outputFormat;
    D3DFORMAT depthFormat;
    BOOL originalAutoDepth;
    bool active;
    bool inPresent;
    bool previousRealValid;
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
    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}
    if(g_ptar.bilinearShader){g_ptar.bilinearShader->Release();g_ptar.bilinearShader=0;}
    if(g_ptar.shader){g_ptar.shader->Release();g_ptar.shader=0;}
    if(g_ptar.realBackBuffer){g_ptar.realBackBuffer->Release();g_ptar.realBackBuffer=0;}
    if(g_ptar.virtualDepth){g_ptar.virtualDepth->Release();g_ptar.virtualDepth=0;}
    if(g_ptar.sourceSurface){g_ptar.sourceSurface->Release();g_ptar.sourceSurface=0;}
    if(g_ptar.sourceTexture){g_ptar.sourceTexture->Release();g_ptar.sourceTexture=0;}
    g_ptar.device=0;
    g_ptar.sourceW=g_ptar.sourceH=g_ptar.outputW=g_ptar.outputH=0;
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

static HRESULT InitializePTARResources(
    IDirect3DDevice9* dev,
    UINT sourceW,UINT sourceH,
    UINT outputW,UINT outputH,
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

    PtDiagStage("Initialize_CreateBilinearShader");
    hr=dev->CreatePixelShader((const DWORD*)g_ptarBilinearPs,&g_ptar.bilinearShader);
    PtDiagLogA("INIT_CreateBilinearShader hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.bilinearShader);
    if(FAILED(hr) || !g_ptar.bilinearShader) goto fail;

    PtDiagStage("Initialize_CreateStateBlock");
    hr=dev->CreateStateBlock(D3DSBT_ALL,&g_ptar.stateBlock);
    PtDiagLogA("INIT_CreateStateBlock hr=0x%08lX ptr=%p",(unsigned long)hr,g_ptar.stateBlock);
    if(FAILED(hr) || !g_ptar.stateBlock) goto fail;

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

    g_ptar.active=true;
    Log(L"PTAR_ACTIVE src=%ux%u out=%ux%u shader=MoE_v01_D3D9_PS3 samples=8 hud=ON compare=F6",
        sourceW,sourceH,outputW,outputH);
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

static HRESULT RenderPTARToRealBackBuffer(IDirect3DDevice9* dev)
{
    if(!g_ptar.active || !g_ptar.sourceTexture || !g_ptar.realBackBuffer ||
       !g_ptar.shader || !g_ptar.bilinearShader || !g_ptar.stateBlock)
        return S_FALSE;

    HRESULT hr=g_ptar.stateBlock->Capture();
    if(FAILED(hr)) return hr;

    hr=g_realSetRenderTarget(dev,0,g_ptar.realBackBuffer);
    if(FAILED(hr)) return hr;
    dev->SetDepthStencilSurface(0);

    D3DVIEWPORT9 vp={};
    vp.X=0; vp.Y=0; vp.Width=g_ptar.outputW; vp.Height=g_ptar.outputH;
    vp.MinZ=0.0f; vp.MaxZ=1.0f;
    dev->SetViewport(&vp);

    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,
        D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|
        D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA);

    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);

    float sizes[4]={
        (float)g_ptar.sourceW,(float)g_ptar.sourceH,
        (float)g_ptar.outputW,(float)g_ptar.outputH};
    dev->SetPixelShaderConstantF(0,sizes,1);
    dev->SetPixelShader(PtHudUseMoe()?g_ptar.shader:g_ptar.bilinearShader);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetTexture(0,g_ptar.sourceTexture);

    const float w=(float)g_ptar.outputW;
    const float h=(float)g_ptar.outputH;
    QuadVertex q[4]={
        {-0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f},
        {w-0.5f,-0.5f,0.0f,1.0f,1.0f,0.0f},
        {-0.5f,h-0.5f,0.0f,1.0f,0.0f,1.0f},
        {w-0.5f,h-0.5f,0.0f,1.0f,1.0f,1.0f}
    };

    hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(QuadVertex));
    dev->SetTexture(0,0);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookPresent(
    IDirect3DDevice9* self,const RECT* src,const RECT* dst,HWND hwnd,const RGNDATA* dirty)
{
    if(!g_ptar.active || self!=g_ptar.device || g_ptar.inPresent)
        return g_realPresent(self,src,dst,hwnd,dirty);

    g_ptar.inPresent=true;
    PtHudFrameTick();

    HRESULT drawHr=RenderPTARToRealBackBuffer(self);
    if(FAILED(drawHr))
        Log(L"PTAR_DRAW_FAIL hr=0x%08X",(unsigned)drawHr);

    if(SUCCEEDED(drawHr))
        PtHudDraw(
            self,
            g_ptar.sourceW,g_ptar.sourceH,
            g_ptar.outputW,g_ptar.outputH);

    HRESULT presentHr=g_realPresent(self,0,0,hwnd,dirty);

    if(g_ptar.stateBlock)
        g_ptar.stateBlock->Apply();
    if(g_ptar.sourceSurface)
        g_realSetRenderTarget(self,0,g_ptar.sourceSurface);
    self->SetDepthStencilSurface(g_ptar.virtualDepth);
    SetVirtualViewport(self);

    g_ptar.inPresent=false;
    return presentHr;
}

static HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* self,D3DPRESENT_PARAMETERS* pp)
{
    if(!pp) return D3DERR_INVALIDCALL;

    D3DPRESENT_PARAMETERS original=*pp;
    const bool eligible=!original.Windowed && original.BackBufferWidth>=2 &&
        original.BackBufferHeight>=2 &&
        (original.BackBufferWidth%2u)==0u && (original.BackBufferHeight%2u)==0u;

    ReleasePTARResources();

    if(!eligible)
    {
        Log(L"RESET_PASSTHROUGH windowed_or_invalid_geometry");
        return g_realReset(self,pp);
    }

    D3DPRESENT_PARAMETERS actual=original;
    const UINT outW=original.BackBufferWidth*3u/2u;
    const UINT outH=original.BackBufferHeight*3u/2u;
    actual.BackBufferWidth=outW;
    actual.BackBufferHeight=outH;
    actual.MultiSampleType=D3DMULTISAMPLE_NONE;
    actual.MultiSampleQuality=0;
    actual.EnableAutoDepthStencil=FALSE;

    HRESULT hr=g_realReset(self,&actual);
    *pp=original;
    if(FAILED(hr))
    {
        Log(L"RESET_PTAR_TARGET_FAIL hr=0x%08X retry_native",(unsigned)hr);
        hr=g_realReset(self,pp);
        return hr;
    }

    hr=InitializePTARResources(
        self,original.BackBufferWidth,original.BackBufferHeight,
        outW,outH,original.EnableAutoDepthStencil,original.AutoDepthStencilFormat);
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
    PtDiagLogA("HOOK_DEVICE_DONE vtable=%p present=%p reset=%p getbb=%p setrt=%p",
        vt,(void*)g_realPresent,(void*)g_realReset,
        (void*)g_realGetBackBuffer,(void*)g_realSetRenderTarget);
    return true;
}

static HRESULT STDMETHODCALLTYPE HookCreateDevice(
    IDirect3D9* self,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
    D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out)
{
    PtDiagStage("HookCreateDevice_ENTER");
    PtDiagLogA("CREATEDEVICE_CALL self=%p adapter=%u type=%u focus=%p flags=0x%08lX pp=%p out=%p",
        self,adapter,(unsigned)type,focus,(unsigned long)flags,pp,out);
    if(!pp || !out) return D3DERR_INVALIDCALL;
    *out=0;

    const D3DPRESENT_PARAMETERS original=*pp;
    PtDiagLogA(
        "CREATEDEVICE_PP w=%u h=%u fmt=%u count=%u ms=%u msq=%lu swap=%u hwnd=%p windowed=%ld "
        "autodepth=%ld depthfmt=%u refresh=%u interval=0x%08lX",
        original.BackBufferWidth,original.BackBufferHeight,(unsigned)original.BackBufferFormat,
        original.BackBufferCount,(unsigned)original.MultiSampleType,(unsigned long)original.MultiSampleQuality,
        (unsigned)original.SwapEffect,original.hDeviceWindow,(long)original.Windowed,
        (long)original.EnableAutoDepthStencil,(unsigned)original.AutoDepthStencilFormat,
        original.FullScreen_RefreshRateInHz,(unsigned long)original.PresentationInterval);
    const bool eligible=!original.Windowed &&
        original.BackBufferWidth>=2 && original.BackBufferHeight>=2 &&
        (original.BackBufferWidth%2u)==0u && (original.BackBufferHeight%2u)==0u;

    if(!eligible)
    {
        Log(L"CREATEDEVICE_PASSTHROUGH windowed_or_invalid_geometry %ux%u",
            original.BackBufferWidth,original.BackBufferHeight);
        return g_realCreateDevice(self,adapter,type,focus,flags,pp,out);
    }

    const UINT outW=original.BackBufferWidth*3u/2u;
    const UINT outH=original.BackBufferHeight*3u/2u;
    D3DPRESENT_PARAMETERS actual=original;
    actual.BackBufferWidth=outW;
    actual.BackBufferHeight=outH;
    actual.MultiSampleType=D3DMULTISAMPLE_NONE;
    actual.MultiSampleQuality=0;
    actual.EnableAutoDepthStencil=FALSE;

    Log(L"CREATEDEVICE_PTAR_TRY src=%ux%u out=%ux%u",
        original.BackBufferWidth,original.BackBufferHeight,outW,outH);

    PtDiagStage("HookCreateDevice_CallRealScaled");
    PtDiagLogA("CREATEDEVICE_REAL_SCALED w=%u h=%u",actual.BackBufferWidth,actual.BackBufferHeight);
    HRESULT hr=g_realCreateDevice(self,adapter,type,focus,flags,&actual,out);
    PtDiagLogA("CREATEDEVICE_REAL_SCALED_RETURN hr=0x%08lX dev=%p",(unsigned long)hr,(out?*out:0));
    *pp=original;

    if(FAILED(hr) || !*out)
    {
        Log(L"CREATEDEVICE_PTAR_TARGET_FAIL hr=0x%08X retry_native",(unsigned)hr);
        PtDiagStage("HookCreateDevice_CallRealNativeFallback");
        hr=g_realCreateDevice(self,adapter,type,focus,flags,pp,out);
        PtDiagLogA("CREATEDEVICE_REAL_NATIVE_RETURN hr=0x%08lX dev=%p",(unsigned long)hr,(out?*out:0));
        *pp=original;
        return hr;
    }

    PtDiagStage("HookCreateDevice_PreInit");
    IDirect3DDevice9* dev=*out;
    PtDiagLogA("CREATEDEVICE_DEVICE_PTR dev=%p",dev);
    void** vt=*(void***)dev;
    PtDiagLogA("CREATEDEVICE_DEVICE_VTABLE=%p",vt);
    g_realGetDisplayMode=(PFN_GetDisplayMode)vt[8];
    g_realReset=(PFN_Reset)vt[16];
    g_realPresent=(PFN_Present)vt[17];
    g_realGetBackBuffer=(PFN_GetBackBuffer)vt[18];
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];

    hr=InitializePTARResources(
        dev,original.BackBufferWidth,original.BackBufferHeight,
        outW,outH,original.EnableAutoDepthStencil,original.AutoDepthStencilFormat);

    if(FAILED(hr))
    {
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
