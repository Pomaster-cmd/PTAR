#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include "../../build_x86/ptar_spec5_d3d9_ps_bytecode.h"
#include "../../build_x86/ptar_fg_me_coarse_ps_bytecode.h"
#include "../../build_x86/ptar_fg_me_refine_ps_bytecode.h"
#include "../../build_x86/ptar_tsr1_motion_subpixel_ps_bytecode.h"
#include "../../build_x86/ptar_tsr1_resolve_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

struct Vtx
{
    float x,y,z,rhw,u,v;
};

struct PerfContext
{
    IDirect3DDevice9* dev;
    IDirect3DPixelShader9* r7;
    IDirect3DPixelShader9* coarse;
    IDirect3DPixelShader9* refine;
    IDirect3DPixelShader9* subpixel;
    IDirect3DPixelShader9* resolve;
    IDirect3DTexture9* current;
    IDirect3DTexture9* history;
    IDirect3DTexture9* coarseTex;
    IDirect3DSurface9* coarseSurf;
    IDirect3DTexture9* fineTex;
    IDirect3DSurface9* fineSurf;
    IDirect3DTexture9* subTex;
    IDirect3DSurface9* subSurf;
    IDirect3DTexture9* outputTex;
    IDirect3DSurface9* outputSurf;
};

struct GpuClock
{
    IDirect3DQuery9* frequency;
    IDirect3DQuery9* disjoint;
    IDirect3DQuery9* begin;
    IDirect3DQuery9* end;
    UINT64 ticksPerSecond;
};

enum Workload
{
    WORK_R7=0,
    WORK_R8=1,
    WORK_COARSE=2,
    WORK_REFINE=3,
    WORK_SUBPIXEL=4,
    WORK_RESOLVE=5
};

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_R8_TSR1_PERF";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR R8 TSR1 PERF",WS_OVERLAPPEDWINDOW,
        0,0,320,240,0,0,inst,0);
}

static HRESULT CreatePattern(
    IDirect3DDevice9* dev,UINT w,UINT h,UINT phase,IDirect3DTexture9** texture)
{
    *texture=0;
    HRESULT hr=dev->CreateTexture(
        w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,texture,0);
    if(FAILED(hr)) return hr;

    D3DLOCKED_RECT lr={};
    hr=(*texture)->LockRect(0,&lr,0,0);
    if(FAILED(hr)) return hr;
    for(UINT y=0;y<h;++y)
    {
        DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<w;++x)
        {
            BYTE r=(BYTE)((x*37u+y*11u+phase*17u)&255u);
            BYTE g=(BYTE)((x*13u+y*29u+phase*31u)&255u);
            BYTE b=(BYTE)((x*7u+y*53u+phase*47u)&255u);
            row[x]=0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;
        }
    }
    (*texture)->UnlockRect(0);
    return S_OK;
}

static HRESULT CreateRT(
    IDirect3DDevice9* dev,UINT w,UINT h,
    IDirect3DTexture9** texture,IDirect3DSurface9** surface)
{
    *texture=0;
    *surface=0;
    HRESULT hr=dev->CreateTexture(
        w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
        texture,0);
    if(FAILED(hr)) return hr;
    hr=(*texture)->GetSurfaceLevel(0,surface);
    if(FAILED(hr))
    {
        (*texture)->Release();
        *texture=0;
    }
    return hr;
}

static HRESULT DrawPass(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,UINT w,UINT h,
    IDirect3DPixelShader9* shader,
    IDirect3DTexture9* t0,IDirect3DTexture9* t1,IDirect3DTexture9* t2,
    const float* c0)
{
    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    dev->SetTexture(2,0);
    dev->SetDepthStencilSurface(0);
    HRESULT hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={0,0,w,h,0.0f,1.0f};
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    for(DWORD s=0;s<3;++s)
    {
        dev->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    if(c0) dev->SetPixelShaderConstantF(0,c0,1);
    dev->SetPixelShader(shader);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetTexture(0,t0);
    dev->SetTexture(1,t1);
    dev->SetTexture(2,t2);

    Vtx q[4]={
        {-.5f,-.5f,0,1,0,0},
        {(float)w-.5f,-.5f,0,1,1,0},
        {-.5f,(float)h-.5f,0,1,0,1},
        {(float)w-.5f,(float)h-.5f,0,1,1,1}};

    hr=dev->BeginScene();
    if(SUCCEEDED(hr))
    {
        hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));
        HRESULT endHr=dev->EndScene();
        if(SUCCEEDED(hr)) hr=endHr;
    }
    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    dev->SetTexture(2,0);
    return hr;
}

static HRESULT RunWork(PerfContext& c,Workload work)
{
    const UINT sw=1280u,sh=720u,dw=1920u,dh=1080u;
    const UINT cw=320u,ch=180u,fw=640u,fh=360u;
    float source[4]={(float)sw,(float)sh,1.0f/(float)sw,1.0f/(float)sh};
    float sizes[4]={(float)sw,(float)sh,(float)dw,(float)dh};

    if(work==WORK_R7)
        return DrawPass(c.dev,c.outputSurf,dw,dh,c.r7,c.current,0,0,sizes);
    if(work==WORK_COARSE)
        return DrawPass(c.dev,c.coarseSurf,cw,ch,c.coarse,c.history,c.current,0,source);
    if(work==WORK_REFINE)
        return DrawPass(c.dev,c.fineSurf,fw,fh,c.refine,c.history,c.current,c.coarseTex,source);
    if(work==WORK_SUBPIXEL)
        return DrawPass(c.dev,c.subSurf,fw,fh,c.subpixel,c.history,c.current,c.fineTex,source);
    if(work==WORK_RESOLVE)
        return DrawPass(c.dev,c.outputSurf,dw,dh,c.resolve,c.current,c.history,c.subTex,sizes);

    HRESULT hr=RunWork(c,WORK_COARSE);
    if(FAILED(hr)) return hr;
    hr=RunWork(c,WORK_REFINE);
    if(FAILED(hr)) return hr;
    hr=RunWork(c,WORK_SUBPIXEL);
    if(FAILED(hr)) return hr;
    return RunWork(c,WORK_RESOLVE);
}

static bool WaitData(IDirect3DQuery9* q,void* data,DWORD bytes)
{
    for(unsigned i=0;i<500000u;++i)
    {
        HRESULT hr=q->GetData(data,bytes,D3DGETDATA_FLUSH);
        if(hr==S_OK) return true;
        if(hr!=S_FALSE) return false;
        if((i&1023u)==0u) Sleep(0);
    }
    return false;
}

static bool InitGpuClock(IDirect3DDevice9* dev,GpuClock& clock)
{
    std::memset(&clock,0,sizeof(clock));
    if(FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&clock.frequency)) || !clock.frequency)
        return false;
    if(FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&clock.disjoint)) || !clock.disjoint)
        return false;
    if(FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&clock.begin)) || !clock.begin)
        return false;
    if(FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&clock.end)) || !clock.end)
        return false;

    if(FAILED(clock.frequency->Issue(D3DISSUE_END))) return false;
    if(!WaitData(clock.frequency,&clock.ticksPerSecond,sizeof(clock.ticksPerSecond))) return false;
    return clock.ticksPerSecond!=0u;
}

static void ReleaseGpuClock(GpuClock& clock)
{
    if(clock.end) clock.end->Release();
    if(clock.begin) clock.begin->Release();
    if(clock.disjoint) clock.disjoint->Release();
    if(clock.frequency) clock.frequency->Release();
    std::memset(&clock,0,sizeof(clock));
}

static bool Measure(
    PerfContext& c,GpuClock& clock,Workload work,
    unsigned repetitions,unsigned framesPerRep,double& medianMs)
{
    if(FAILED(RunWork(c,work))) return false;

    std::vector<double> values;
    values.reserve(repetitions);
    unsigned attempts=0;
    while(values.size()<repetitions && attempts<repetitions*4u)
    {
        ++attempts;
        if(FAILED(clock.disjoint->Issue(D3DISSUE_BEGIN))) return false;
        if(FAILED(clock.begin->Issue(D3DISSUE_END))) return false;
        for(unsigned i=0;i<framesPerRep;++i)
            if(FAILED(RunWork(c,work))) return false;
        if(FAILED(clock.end->Issue(D3DISSUE_END))) return false;
        if(FAILED(clock.disjoint->Issue(D3DISSUE_END))) return false;

        UINT64 a=0,b=0;
        BOOL disjoint=TRUE;
        if(!WaitData(clock.end,&b,sizeof(b))) return false;
        if(!WaitData(clock.begin,&a,sizeof(a))) return false;
        if(!WaitData(clock.disjoint,&disjoint,sizeof(disjoint))) return false;
        if(disjoint || b<=a) continue;

        double ms=((double)(b-a)*1000.0/(double)clock.ticksPerSecond)/(double)framesPerRep;
        if(ms>0.0) values.push_back(ms);
    }
    if(values.size()!=repetitions) return false;
    std::sort(values.begin(),values.end());
    medianMs=values[values.size()/2u];
    return true;
}

static void ReleaseContext(PerfContext& c)
{
    if(c.outputSurf)c.outputSurf->Release();
    if(c.outputTex)c.outputTex->Release();
    if(c.subSurf)c.subSurf->Release();
    if(c.subTex)c.subTex->Release();
    if(c.fineSurf)c.fineSurf->Release();
    if(c.fineTex)c.fineTex->Release();
    if(c.coarseSurf)c.coarseSurf->Release();
    if(c.coarseTex)c.coarseTex->Release();
    if(c.history)c.history->Release();
    if(c.current)c.current->Release();
    if(c.resolve)c.resolve->Release();
    if(c.subpixel)c.subpixel->Release();
    if(c.refine)c.refine->Release();
    if(c.coarse)c.coarse->Release();
    if(c.r7)c.r7->Release();
    if(c.dev)c.dev->Release();
    std::memset(&c,0,sizeof(c));
}

int main()
{
    wchar_t path[MAX_PATH]={0};
    GetSystemDirectoryW(path,MAX_PATH);
    wcscat_s(path,L"\\d3d9.dll");
    HMODULE module=LoadLibraryW(path);
    if(!module) return 2;
    PFN_Direct3DCreate9 create9=(PFN_Direct3DCreate9)GetProcAddress(module,"Direct3DCreate9");
    IDirect3D9* d3d=create9?create9(D3D_SDK_VERSION):0;
    if(!d3d) return 3;
    HWND window=MakeWindow();
    if(!window) return 4;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=1920;
    pp.BackBufferHeight=1080;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    PerfContext c={};
    HRESULT hr=d3d->CreateDevice(
        0,D3DDEVTYPE_HAL,window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
        &pp,&c.dev);
    if(FAILED(hr))
        hr=d3d->CreateDevice(
            0,D3DDEVTYPE_HAL,window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
            &pp,&c.dev);
    if(FAILED(hr)||!c.dev) return 5;

    if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarSpec5Ps,&c.r7))) return 10;
    if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarFgMeCoarsePs,&c.coarse))) return 11;
    if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarFgMeRefinePs,&c.refine))) return 12;
    if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarTsr1MotionSubpixelPs,&c.subpixel))) return 13;
    if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarTsr1ResolvePs,&c.resolve))) return 14;

    if(FAILED(CreatePattern(c.dev,1280,720,0,&c.current))) return 20;
    if(FAILED(CreatePattern(c.dev,1280,720,1,&c.history))) return 21;
    if(FAILED(CreateRT(c.dev,320,180,&c.coarseTex,&c.coarseSurf))) return 22;
    if(FAILED(CreateRT(c.dev,640,360,&c.fineTex,&c.fineSurf))) return 23;
    if(FAILED(CreateRT(c.dev,640,360,&c.subTex,&c.subSurf))) return 24;
    if(FAILED(CreateRT(c.dev,1920,1080,&c.outputTex,&c.outputSurf))) return 25;

    if(FAILED(RunWork(c,WORK_R8))) return 30;

    GpuClock clock={};
    if(!InitGpuClock(c.dev,clock))
    {
        std::printf("TSR1_PERF_GPU_TIMESTAMP=UNSUPPORTED\n");
        return 31;
    }
    std::printf("TSR1_PERF_GPU_TIMESTAMP=PASS\n");
    std::printf("TSR1_PERF_TIMESTAMP_HZ=%llu\n",(unsigned long long)clock.ticksPerSecond);

    double r7=0.0,r8=0.0,coarse=0.0,refine=0.0,sub=0.0,resolve=0.0;
    if(!Measure(c,clock,WORK_R7,7,2,r7)) return 40;
    if(!Measure(c,clock,WORK_R8,7,2,r8)) return 41;
    if(!Measure(c,clock,WORK_COARSE,5,3,coarse)) return 42;
    if(!Measure(c,clock,WORK_REFINE,5,3,refine)) return 43;
    if(!Measure(c,clock,WORK_SUBPIXEL,5,3,sub)) return 44;
    if(!Measure(c,clock,WORK_RESOLVE,5,2,resolve)) return 45;

    double ratio=r8/r7;
    double overhead=(r8-r7)/r7*100.0;
    double sum=coarse+refine+sub+resolve;
    double accounting=100.0*(r8-sum)/r8;

    std::printf("TSR1_PERF_R7_GPU_MS=%.6f\n",r7);
    std::printf("TSR1_PERF_R8_FULL_GPU_MS=%.6f\n",r8);
    std::printf("TSR1_PERF_COARSE_GPU_MS=%.6f\n",coarse);
    std::printf("TSR1_PERF_REFINE_GPU_MS=%.6f\n",refine);
    std::printf("TSR1_PERF_SUBPIXEL_GPU_MS=%.6f\n",sub);
    std::printf("TSR1_PERF_RESOLVE_GPU_MS=%.6f\n",resolve);
    std::printf("TSR1_PERF_PASS_SUM_GPU_MS=%.6f\n",sum);
    std::printf("TSR1_PERF_ACCOUNTING_DELTA_PCT=%.3f\n",accounting);
    std::printf("TSR1_PERF_R8_TO_R7_RATIO=%.4f\n",ratio);
    std::printf("TSR1_PERF_R8_OVERHEAD_PCT=%.3f\n",overhead);

    bool sane=
        r7>0.0 && r8>0.0 && coarse>0.0 && refine>0.0 && sub>0.0 && resolve>0.0 &&
        ratio>=1.0 && ratio<=4.0;
    std::printf("TSR1_PERF_INITIAL_ENVELOPE=%s\n",sane?"PASS":"FAIL");
    if(sane) std::printf("TSR1_GPU_PERF=PASS\n");

    ReleaseGpuClock(clock);
    ReleaseContext(c);
    d3d->Release();
    DestroyWindow(window);
    FreeLibrary(module);
    return sane?0:46;
}
