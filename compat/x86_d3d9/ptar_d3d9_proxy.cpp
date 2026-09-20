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
    IDirect3DStateBlock9* stateBlock;
    UINT sourceW;
    UINT sourceH;
    UINT outputW;
    UINT outputH;
    D3DFORMAT outputFormat;
    D3DFORMAT depthFormat;
    BOOL originalAutoDepth;
    bool active;
    bool inPresent;
};

static PTARContext g_ptar={};

static void Log(const wchar_t* fmt,...)
{
    wchar_t line[1024]={0};
    va_list ap;
    va_start(ap,fmt);
    _vsnwprintf_s(line,_countof(line),_TRUNCATE,fmt,ap);
    va_end(ap);

    wchar_t mod[MAX_PATH]={0};
    if(!g_self || !GetModuleFileNameW(g_self,mod,_countof(mod)))
        return;
    wchar_t* slash=wcsrchr(mod,L'\\');
    if(slash) *(slash+1)=0;
    wcscat_s(mod,L"PTAR_X86_D3D9.log");

    FILE* f=0;
    if(_wfopen_s(&f,mod,L"a+, ccs=UTF-8")==0 && f)
    {
        SYSTEMTIME st={};
        GetLocalTime(&st);
        fwprintf(f,L"%04u-%02u-%02u %02u:%02u:%02u %s\n",
            st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,line);
        fclose(f);
    }
}

static bool EnsureRealD3D9()
{
    if(g_realD3D9) return true;

    wchar_t sys[MAX_PATH]={0};
    UINT n=GetSystemDirectoryW(sys,_countof(sys));
    if(!n || n>=_countof(sys)) return false;
    wcscat_s(sys,L"\\d3d9.dll");

    g_realD3D9=LoadLibraryW(sys);
    if(!g_realD3D9) return false;

    g_sysDirect3DCreate9=(PFN_Direct3DCreate9)GetProcAddress(g_realD3D9,"Direct3DCreate9");
    g_sysDirect3DCreate9Ex=(PFN_Direct3DCreate9Ex)GetProcAddress(g_realD3D9,"Direct3DCreate9Ex");
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
    ReleasePTARResources();

    g_ptar.device=dev;
    g_ptar.sourceW=sourceW;
    g_ptar.sourceH=sourceH;
    g_ptar.outputW=outputW;
    g_ptar.outputH=outputH;
    g_ptar.originalAutoDepth=originalAutoDepth;
    g_ptar.depthFormat=originalDepthFormat;

    D3DSURFACE_DESC desc={};
    HRESULT hr=g_realGetBackBuffer(dev,0,0,D3DBACKBUFFER_TYPE_MONO,&g_ptar.realBackBuffer);
    if(FAILED(hr) || !g_ptar.realBackBuffer) goto fail;

    hr=g_ptar.realBackBuffer->GetDesc(&desc);
    if(FAILED(hr)) goto fail;
    g_ptar.outputFormat=desc.Format;

    hr=dev->CreateTexture(
        sourceW,sourceH,1,D3DUSAGE_RENDERTARGET,
        desc.Format,D3DPOOL_DEFAULT,&g_ptar.sourceTexture,0);
    if(FAILED(hr) || !g_ptar.sourceTexture) goto fail;

    hr=g_ptar.sourceTexture->GetSurfaceLevel(0,&g_ptar.sourceSurface);
    if(FAILED(hr) || !g_ptar.sourceSurface) goto fail;

    if(originalAutoDepth)
    {
        hr=dev->CreateDepthStencilSurface(
            sourceW,sourceH,originalDepthFormat,
            D3DMULTISAMPLE_NONE,0,TRUE,&g_ptar.virtualDepth,0);
        if(FAILED(hr) || !g_ptar.virtualDepth) goto fail;
    }

    hr=dev->CreatePixelShader((const DWORD*)g_ptarPs,&g_ptar.shader);
    if(FAILED(hr) || !g_ptar.shader) goto fail;

    hr=dev->CreateStateBlock(D3DSBT_ALL,&g_ptar.stateBlock);
    if(FAILED(hr) || !g_ptar.stateBlock) goto fail;

    hr=dev->SetRenderTarget(0,g_ptar.sourceSurface);
    if(FAILED(hr)) goto fail;
    hr=dev->SetDepthStencilSurface(g_ptar.virtualDepth);
    if(FAILED(hr)) goto fail;
    hr=SetVirtualViewport(dev);
    if(FAILED(hr)) goto fail;

    g_ptar.active=true;
    Log(L"PTAR_ACTIVE src=%ux%u out=%ux%u shader=MoE_v01_D3D9_PS3 samples=8",
        sourceW,sourceH,outputW,outputH);
    return S_OK;

fail:
    Log(L"PTAR_INIT_FAIL hr=0x%08X",(unsigned)hr);
    ReleasePTARResources();
    return FAILED(hr)?hr:E_FAIL;
}

static void** CloneVtable(void* object,size_t count)
{
    if(!object || !count) return 0;
    void*** obj=(void***)object;
    void** old=*obj;
    void** copy=(void**)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,count*sizeof(void*));
    if(!copy) return 0;
    memcpy(copy,old,count*sizeof(void*));
    *obj=copy;
    return old;
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
       !g_ptar.shader || !g_ptar.stateBlock)
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
    dev->SetPixelShader(g_ptar.shader);
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
    HRESULT drawHr=RenderPTARToRealBackBuffer(self);
    if(FAILED(drawHr))
        Log(L"PTAR_DRAW_FAIL hr=0x%08X",(unsigned)drawHr);

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
    if(!dev) return false;
    if(g_ptar.device && g_ptar.device!=dev)
    {
        Log(L"SECOND_DEVICE_UNSUPPORTED passthrough");
        return false;
    }

    void** old=CloneVtable(dev,119);
    if(!old) return false;

    g_realGetDisplayMode=(PFN_GetDisplayMode)old[8];
    g_realReset=(PFN_Reset)old[16];
    g_realPresent=(PFN_Present)old[17];
    g_realGetBackBuffer=(PFN_GetBackBuffer)old[18];
    g_realSetRenderTarget=(PFN_SetRenderTarget)old[37];

    void** now=*(void***)dev;
    now[8]=(void*)&HookGetDisplayMode;
    now[16]=(void*)&HookReset;
    now[17]=(void*)&HookPresent;
    now[18]=(void*)&HookGetBackBuffer;
    now[37]=(void*)&HookSetRenderTarget;
    return true;
}

static HRESULT STDMETHODCALLTYPE HookCreateDevice(
    IDirect3D9* self,UINT adapter,D3DDEVTYPE type,HWND focus,DWORD flags,
    D3DPRESENT_PARAMETERS* pp,IDirect3DDevice9** out)
{
    if(!pp || !out) return D3DERR_INVALIDCALL;
    *out=0;

    const D3DPRESENT_PARAMETERS original=*pp;
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

    HRESULT hr=g_realCreateDevice(self,adapter,type,focus,flags,&actual,out);
    *pp=original;

    if(FAILED(hr) || !*out)
    {
        Log(L"CREATEDEVICE_PTAR_TARGET_FAIL hr=0x%08X retry_native",(unsigned)hr);
        hr=g_realCreateDevice(self,adapter,type,focus,flags,pp,out);
        *pp=original;
        return hr;
    }

    IDirect3DDevice9* dev=*out;
    void** vt=*(void***)dev;
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
    if(!d3d) return false;
    void** old=CloneVtable(d3d,17);
    if(!old) return false;
    g_realCreateDevice=(PFN_CreateDevice)old[16];
    void** now=*(void***)d3d;
    now[16]=(void*)&HookCreateDevice;
    return true;
}

extern "C" __declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk)
{
    if(!EnsureRealD3D9()) return 0;
    IDirect3D9* d3d=g_sysDirect3DCreate9(sdk);
    if(d3d)
    {
        HookD3D9(d3d);
        Log(L"PROXY_LOADED arch=x86 api=D3D9");
    }
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
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
