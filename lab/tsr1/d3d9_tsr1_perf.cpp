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
struct Vtx{float x,y,z,rhw,u,v;};

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
    IDirect3DTexture9* coarseTex; IDirect3DSurface9* coarseSurf;
    IDirect3DTexture9* fineTex;   IDirect3DSurface9* fineSurf;
    IDirect3DTexture9* subTex;    IDirect3DSurface9* subSurf;
    IDirect3DTexture9* r7Tex;     IDirect3DSurface9* r7Surf;
    IDirect3DTexture9* outputTex; IDirect3DSurface9* outputSurf;
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
    WORK_R8_EXTRA=1,
    WORK_R8_TOTAL=2,
    WORK_COARSE=3,
    WORK_REFINE=4,
    WORK_SUBPIXEL=5,
    WORK_RESOLVE=6
};

static HWND MakeWindow()
{
    HINSTANCE i=GetModuleHandleW(0);WNDCLASSW c={};
    c.lpfnWndProc=DefWindowProcW;c.hInstance=i;c.lpszClassName=L"PTAR_R8_TSR1_PERF";
    RegisterClassW(&c);
    return CreateWindowExW(0,c.lpszClassName,L"PTAR R8 TSR1 PERF",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);
}

static HRESULT CreatePattern(IDirect3DDevice9*d,UINT w,UINT h,UINT phase,IDirect3DTexture9**t)
{
    *t=0;HRESULT hr=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(hr))return hr;
    D3DLOCKED_RECT lr={};hr=(*t)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    for(UINT y=0;y<h;++y){DWORD*row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);for(UINT x=0;x<w;++x){BYTE r=(BYTE)((x*37u+y*11u+phase*17u)&255u),g=(BYTE)((x*13u+y*29u+phase*31u)&255u),b=(BYTE)((x*7u+y*53u+phase*47u)&255u);row[x]=0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;}}
    (*t)->UnlockRect(0);return S_OK;
}

static HRESULT CreateRT(IDirect3DDevice9*d,UINT w,UINT h,IDirect3DTexture9**t,IDirect3DSurface9**s)
{
    *t=0;*s=0;HRESULT hr=d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t,0);if(FAILED(hr))return hr;
    hr=(*t)->GetSurfaceLevel(0,s);if(FAILED(hr)){(*t)->Release();*t=0;}return hr;
}

static HRESULT DrawPass(IDirect3DDevice9*d,IDirect3DSurface9*rt,UINT w,UINT h,IDirect3DPixelShader9*ps,IDirect3DTexture9*t0,IDirect3DTexture9*t1,IDirect3DTexture9*t2,IDirect3DTexture9*t3,const float*c0)
{
    for(DWORD s=0;s<4;++s)d->SetTexture(s,0);d->SetDepthStencilSurface(0);HRESULT hr=d->SetRenderTarget(0,rt);if(FAILED(hr))return hr;
    D3DVIEWPORT9 vp={0,0,w,h,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    for(DWORD s=0;s<4;++s){d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);}d->SetSamplerState(3,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(3,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    if(c0)d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShader(ps);d->SetVertexShader(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,t0);d->SetTexture(1,t1);d->SetTexture(2,t2);d->SetTexture(3,t3);
    Vtx q[4]={{-.5f,-.5f,0,1,0,0},{(float)w-.5f,-.5f,0,1,1,0},{-.5f,(float)h-.5f,0,1,0,1},{(float)w-.5f,(float)h-.5f,0,1,1,1}};
    hr=d->BeginScene();if(SUCCEEDED(hr)){hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=d->EndScene();if(SUCCEEDED(hr))hr=e;}for(DWORD s=0;s<4;++s)d->SetTexture(s,0);return hr;
}

static HRESULT RunR7(PerfContext&c)
{
    const UINT sw=1280,sh=720,dw=1920,dh=1080;float sizes[4]={(float)sw,(float)sh,(float)dw,(float)dh};
    return DrawPass(c.dev,c.r7Surf,dw,dh,c.r7,c.current,0,0,0,sizes);
}

static HRESULT RunCoarse(PerfContext&c)
{
    const UINT sw=1280,sh=720;float src[4]={(float)sw,(float)sh,1.0f/sw,1.0f/sh};
    return DrawPass(c.dev,c.coarseSurf,320,180,c.coarse,c.history,c.current,0,0,src);
}
static HRESULT RunRefine(PerfContext&c)
{
    const UINT sw=1280,sh=720;float src[4]={(float)sw,(float)sh,1.0f/sw,1.0f/sh};
    return DrawPass(c.dev,c.fineSurf,640,360,c.refine,c.history,c.current,c.coarseTex,0,src);
}
static HRESULT RunSubpixel(PerfContext&c)
{
    const UINT sw=1280,sh=720;float src[4]={(float)sw,(float)sh,1.0f/sw,1.0f/sh};
    return DrawPass(c.dev,c.subSurf,640,360,c.subpixel,c.history,c.current,c.fineTex,0,src);
}
static HRESULT RunResolve(PerfContext&c)
{
    const UINT sw=1280,sh=720,dw=1920,dh=1080;float sizes[4]={(float)sw,(float)sh,(float)dw,(float)dh};
    return DrawPass(c.dev,c.outputSurf,dw,dh,c.resolve,c.current,c.history,c.subTex,c.r7Tex,sizes);
}
static HRESULT RunExtra(PerfContext&c)
{
    HRESULT hr=RunCoarse(c);if(FAILED(hr))return hr;hr=RunRefine(c);if(FAILED(hr))return hr;hr=RunSubpixel(c);if(FAILED(hr))return hr;return RunResolve(c);
}
static HRESULT RunWork(PerfContext&c,Workload w)
{
    if(w==WORK_R7)return RunR7(c);
    if(w==WORK_R8_EXTRA)return RunExtra(c);
    if(w==WORK_R8_TOTAL){HRESULT hr=RunR7(c);if(FAILED(hr))return hr;return RunExtra(c);}
    if(w==WORK_COARSE)return RunCoarse(c);
    if(w==WORK_REFINE)return RunRefine(c);
    if(w==WORK_SUBPIXEL)return RunSubpixel(c);
    return RunResolve(c);
}

static bool WaitData(IDirect3DQuery9*q,void*data,DWORD bytes)
{
    for(unsigned i=0;i<500000u;++i){HRESULT hr=q->GetData(data,bytes,D3DGETDATA_FLUSH);if(hr==S_OK)return true;if(hr!=S_FALSE)return false;if((i&1023u)==0u)Sleep(0);}return false;
}
static bool InitClock(IDirect3DDevice9*d,GpuClock&g)
{
    std::memset(&g,0,sizeof(g));if(FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&g.frequency))||!g.frequency)return false;if(FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&g.disjoint))||!g.disjoint)return false;if(FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&g.begin))||!g.begin)return false;if(FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&g.end))||!g.end)return false;if(FAILED(g.frequency->Issue(D3DISSUE_END)))return false;if(!WaitData(g.frequency,&g.ticksPerSecond,sizeof(g.ticksPerSecond)))return false;return g.ticksPerSecond!=0;
}
static void ReleaseClock(GpuClock&g){if(g.end)g.end->Release();if(g.begin)g.begin->Release();if(g.disjoint)g.disjoint->Release();if(g.frequency)g.frequency->Release();std::memset(&g,0,sizeof(g));}

static bool Measure(PerfContext&c,GpuClock&g,Workload w,unsigned reps,unsigned frames,double&median)
{
    // Populate every dependency once outside the timed region. Static inputs are
    // intentional: this is a pure GPU pass-cost oracle, not a content benchmark.
    if(FAILED(RunR7(c))||FAILED(RunCoarse(c))||FAILED(RunRefine(c))||FAILED(RunSubpixel(c)))return false;
    std::vector<double>v;v.reserve(reps);unsigned attempts=0;
    while(v.size()<reps&&attempts<reps*4u){++attempts;if(FAILED(g.disjoint->Issue(D3DISSUE_BEGIN)))return false;if(FAILED(g.begin->Issue(D3DISSUE_END)))return false;for(unsigned i=0;i<frames;++i)if(FAILED(RunWork(c,w)))return false;if(FAILED(g.end->Issue(D3DISSUE_END)))return false;if(FAILED(g.disjoint->Issue(D3DISSUE_END)))return false;UINT64 a=0,b=0;BOOL dis=TRUE;if(!WaitData(g.end,&b,sizeof(b))||!WaitData(g.begin,&a,sizeof(a))||!WaitData(g.disjoint,&dis,sizeof(dis)))return false;if(dis||b<=a)continue;double ms=((double)(b-a)*1000.0/(double)g.ticksPerSecond)/(double)frames;if(ms>0)v.push_back(ms);}
    if(v.size()!=reps)return false;std::sort(v.begin(),v.end());median=v[v.size()/2u];return true;
}

static void Release(PerfContext&c)
{
    if(c.outputSurf)c.outputSurf->Release();if(c.outputTex)c.outputTex->Release();if(c.r7Surf)c.r7Surf->Release();if(c.r7Tex)c.r7Tex->Release();if(c.subSurf)c.subSurf->Release();if(c.subTex)c.subTex->Release();if(c.fineSurf)c.fineSurf->Release();if(c.fineTex)c.fineTex->Release();if(c.coarseSurf)c.coarseSurf->Release();if(c.coarseTex)c.coarseTex->Release();if(c.history)c.history->Release();if(c.current)c.current->Release();if(c.resolve)c.resolve->Release();if(c.subpixel)c.subpixel->Release();if(c.refine)c.refine->Release();if(c.coarse)c.coarse->Release();if(c.r7)c.r7->Release();if(c.dev)c.dev->Release();std::memset(&c,0,sizeof(c));
}

int main()
{
    wchar_t p[MAX_PATH]={0};GetSystemDirectoryW(p,MAX_PATH);wcscat_s(p,L"\\d3d9.dll");HMODULE m=LoadLibraryW(p);if(!m)return 2;PFN_Direct3DCreate9 fn=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");IDirect3D9*d3d=fn?fn(D3D_SDK_VERSION):0;if(!d3d)return 3;HWND wnd=MakeWindow();if(!wnd)return 4;
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=1920;pp.BackBufferHeight=1080;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=wnd;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    PerfContext c={};HRESULT hr=d3d->CreateDevice(0,D3DDEVTYPE_HAL,wnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&c.dev);if(FAILED(hr))hr=d3d->CreateDevice(0,D3DDEVTYPE_HAL,wnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&c.dev);if(FAILED(hr)||!c.dev)return 5;
    if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarSpec5Ps,&c.r7)))return 10;if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarFgMeCoarsePs,&c.coarse)))return 11;if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarFgMeRefinePs,&c.refine)))return 12;if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarTsr1MotionSubpixelPs,&c.subpixel)))return 13;if(FAILED(c.dev->CreatePixelShader((const DWORD*)g_ptarTsr1ResolvePs,&c.resolve)))return 14;
    if(FAILED(CreatePattern(c.dev,1280,720,0,&c.current)))return 20;if(FAILED(CreatePattern(c.dev,1280,720,1,&c.history)))return 21;if(FAILED(CreateRT(c.dev,320,180,&c.coarseTex,&c.coarseSurf)))return 22;if(FAILED(CreateRT(c.dev,640,360,&c.fineTex,&c.fineSurf)))return 23;if(FAILED(CreateRT(c.dev,640,360,&c.subTex,&c.subSurf)))return 24;if(FAILED(CreateRT(c.dev,1920,1080,&c.r7Tex,&c.r7Surf)))return 25;if(FAILED(CreateRT(c.dev,1920,1080,&c.outputTex,&c.outputSurf)))return 26;
    if(FAILED(RunWork(c,WORK_R8_TOTAL)))return 30;

    GpuClock clock={};if(!InitClock(c.dev,clock)){std::printf("TSR1_PERF_GPU_TIMESTAMP=UNSUPPORTED\n");return 31;}std::printf("TSR1_PERF_GPU_TIMESTAMP=PASS\n");std::printf("TSR1_PERF_TIMESTAMP_HZ=%llu\n",(unsigned long long)clock.ticksPerSecond);
    double r7=0,extra=0,total=0,coarse=0,refine=0,sub=0,resolve=0;
    if(!Measure(c,clock,WORK_R7,7,2,r7))return 40;if(!Measure(c,clock,WORK_R8_EXTRA,7,2,extra))return 41;if(!Measure(c,clock,WORK_R8_TOTAL,7,2,total))return 42;if(!Measure(c,clock,WORK_COARSE,5,3,coarse))return 43;if(!Measure(c,clock,WORK_REFINE,5,3,refine))return 44;if(!Measure(c,clock,WORK_SUBPIXEL,5,3,sub))return 45;if(!Measure(c,clock,WORK_RESOLVE,5,2,resolve))return 46;

    const double totalRatio=total/r7;const double extraRatio=extra/r7;const double overhead=(total-r7)/r7*100.0;const double passSum=r7+coarse+refine+sub+resolve;const double accounting=100.0*(total-passSum)/total;
    std::printf("TSR1_PERF_R7_GPU_MS=%.6f\n",r7);std::printf("TSR1_PERF_TSR_EXTRA_GPU_MS=%.6f\n",extra);std::printf("TSR1_PERF_R8_TOTAL_GPU_MS=%.6f\n",total);std::printf("TSR1_PERF_COARSE_GPU_MS=%.6f\n",coarse);std::printf("TSR1_PERF_REFINE_GPU_MS=%.6f\n",refine);std::printf("TSR1_PERF_SUBPIXEL_GPU_MS=%.6f\n",sub);std::printf("TSR1_PERF_RESOLVE_GPU_MS=%.6f\n",resolve);std::printf("TSR1_PERF_TOTAL_PASS_SUM_GPU_MS=%.6f\n",passSum);std::printf("TSR1_PERF_ACCOUNTING_DELTA_PCT=%.3f\n",accounting);std::printf("TSR1_PERF_R8_TOTAL_TO_R7_RATIO=%.4f\n",totalRatio);std::printf("TSR1_PERF_TSR_EXTRA_TO_R7_RATIO=%.4f\n",extraRatio);std::printf("TSR1_PERF_R8_OVERHEAD_PCT=%.3f\n",overhead);
    bool sane=r7>0&&extra>0&&total>0&&coarse>0&&refine>0&&sub>0&&resolve>0&&totalRatio>=1.0&&totalRatio<=3.0&&extra<total;std::printf("TSR1_PERF_INITIAL_ENVELOPE=%s\n",sane?"PASS":"FAIL");if(sane)std::printf("TSR1_GPU_PERF=PASS\n");
    ReleaseClock(clock);Release(c);d3d->Release();DestroyWindow(wnd);FreeLibrary(m);return sane?0:47;
}
