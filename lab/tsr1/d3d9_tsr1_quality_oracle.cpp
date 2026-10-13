#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include "../../build_x86/ptar_spec5_d3d9_ps_bytecode.h"
#include "../../build_x86/ptar_tsr1_resolve_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};

static HWND MakeWindow()
{
    HINSTANCE i=GetModuleHandleW(0);WNDCLASSW c={};
    c.lpfnWndProc=DefWindowProcW;c.hInstance=i;c.lpszClassName=L"PTAR_TSR1_QUALITY";
    RegisterClassW(&c);
    return CreateWindowExW(0,c.lpszClassName,L"PTAR TSR1 quality",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);
}

static HRESULT CreateRT(IDirect3DDevice9*d,UINT w,UINT h,IDirect3DTexture9**t,IDirect3DSurface9**s)
{
    *t=0;*s=0;HRESULT hr=d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t,0);
    if(FAILED(hr))return hr;hr=(*t)->GetSurfaceLevel(0,s);if(FAILED(hr)){(*t)->Release();*t=0;}return hr;
}

static BYTE Q(float v){int n=(int)floorf(std::max(0.0f,std::min(1.0f,v))*255.0f+0.5f);return (BYTE)n;}
static DWORD Pattern(float x,float y)
{
    const float r=0.50f+0.18f*sinf(0.47f*x+0.11f*y)+0.13f*sinf(0.19f*x-0.41f*y);
    const float g=0.50f+0.20f*sinf(0.37f*x-0.17f*y)+0.10f*cosf(0.23f*x+0.29f*y);
    const float b=0.50f+0.17f*cosf(0.43f*x+0.31f*y)+0.12f*sinf(0.13f*x+0.39f*y);
    return 0xff000000u|((DWORD)Q(r)<<16)|((DWORD)Q(g)<<8)|(DWORD)Q(b);
}

static HRESULT CreatePattern(IDirect3DDevice9*d,UINT w,UINT h,float sx,float sy,IDirect3DTexture9**t)
{
    *t=0;HRESULT hr=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(hr))return hr;
    D3DLOCKED_RECT lr={};hr=(*t)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    for(UINT y=0;y<h;++y){DWORD*row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);for(UINT x=0;x<w;++x)row[x]=Pattern((float)x-sx,(float)y-sy);}(*t)->UnlockRect(0);return S_OK;
}

static BYTE EncodeMotion(float pixels)
{
    const float zero=128.0f/255.0f;
    return Q(pixels/64.0f+zero);
}

static HRESULT CreateMotion(IDirect3DDevice9*d,UINT w,UINT h,float mx,float my,IDirect3DTexture9**t)
{
    *t=0;HRESULT hr=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(hr))return hr;
    D3DLOCKED_RECT lr={};hr=(*t)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    const DWORD p=0xff000000u|((DWORD)EncodeMotion(mx)<<16)|((DWORD)EncodeMotion(my)<<8)|255u;
    for(UINT y=0;y<h;++y){DWORD*row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);for(UINT x=0;x<w;++x)row[x]=p;}(*t)->UnlockRect(0);return S_OK;
}

static HRESULT Draw(IDirect3DDevice9*d,IDirect3DSurface9*rt,UINT w,UINT h,IDirect3DPixelShader9*ps,IDirect3DTexture9*t0,IDirect3DTexture9*t1,IDirect3DTexture9*t2,const float*c0)
{
    d->SetTexture(0,0);d->SetTexture(1,0);d->SetTexture(2,0);d->SetDepthStencilSurface(0);HRESULT hr=d->SetRenderTarget(0,rt);if(FAILED(hr))return hr;
    D3DVIEWPORT9 vp={0,0,w,h,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    for(DWORD s=0;s<3;++s){d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);}
    if(c0)d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShader(ps);d->SetVertexShader(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,t0);d->SetTexture(1,t1);d->SetTexture(2,t2);
    Vtx q[4]={{-.5f,-.5f,0,1,0,0},{(float)w-.5f,-.5f,0,1,1,0},{-.5f,(float)h-.5f,0,1,0,1},{(float)w-.5f,(float)h-.5f,0,1,1,1}};
    hr=d->BeginScene();if(SUCCEEDED(hr)){hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=d->EndScene();if(SUCCEEDED(hr))hr=e;}d->SetTexture(0,0);d->SetTexture(1,0);d->SetTexture(2,0);return hr;
}

static HRESULT Read(IDirect3DDevice9*d,IDirect3DSurface9*s,UINT w,UINT h,std::vector<DWORD>&out)
{
    IDirect3DSurface9*r=0;HRESULT hr=d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&r,0);if(FAILED(hr))return hr;hr=d->GetRenderTargetData(s,r);if(SUCCEEDED(hr)){D3DLOCKED_RECT lr={};hr=r->LockRect(&lr,0,D3DLOCK_READONLY);if(SUCCEEDED(hr)){out.resize((size_t)w*h);for(UINT y=0;y<h;++y)memcpy(&out[(size_t)y*w],(BYTE*)lr.pBits+y*lr.Pitch,w*4);r->UnlockRect();}}r->Release();return hr;
}

static uint64_t AbsRgb(DWORD a,DWORD b)
{
    uint64_t e=0;for(int s=0;s<=16;s+=8)e+=(uint64_t)abs((int)((a>>s)&255)-(int)((b>>s)&255));return e;
}

int main()
{
    wchar_t p[MAX_PATH]={0};GetSystemDirectoryW(p,MAX_PATH);wcscat_s(p,L"\\d3d9.dll");HMODULE m=LoadLibraryW(p);if(!m)return 2;PFN_Direct3DCreate9 fn=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");IDirect3D9*d3d=fn?fn(D3D_SDK_VERSION):0;if(!d3d)return 3;HWND wnd=MakeWindow();if(!wnd)return 4;
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=144;pp.BackBufferHeight=96;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=wnd;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9*d=0;HRESULT hr=d3d->CreateDevice(0,D3DDEVTYPE_HAL,wnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(hr))hr=d3d->CreateDevice(0,D3DDEVTYPE_REF,wnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(hr)||!d)return 5;

    const UINT sw=96,sh=64,dw=144,dh=96,mw=48,mh=32;
    IDirect3DTexture9 *curr=0,*r7Out=0,*tsrOut=0;IDirect3DSurface9 *r7S=0,*tsrS=0;
    if(FAILED(CreatePattern(d,sw,sh,0,0,&curr)))return 10;if(FAILED(CreateRT(d,dw,dh,&r7Out,&r7S)))return 11;if(FAILED(CreateRT(d,dw,dh,&tsrOut,&tsrS)))return 12;
    IDirect3DPixelShader9 *r7=0,*resolve=0;if(FAILED(d->CreatePixelShader((const DWORD*)g_ptarSpec5Ps,&r7))||!r7)return 13;if(FAILED(d->CreatePixelShader((const DWORD*)g_ptarTsr1ResolvePs,&resolve))||!resolve)return 14;
    float sizes[4]={(float)sw,(float)sh,(float)dw,(float)dh};if(FAILED(Draw(d,r7S,dw,dh,r7,curr,0,0,sizes)))return 15;
    std::vector<DWORD>r7Pixels;if(FAILED(Read(d,r7S,dw,dh,r7Pixels)))return 16;

    uint64_t r7Error=0;uint64_t referenceSamples=0;
    for(UINT y=6;y+6<dh;++y)for(UINT x=6;x+6<dw;++x){const float px=((float)x+0.5f)*(float)sw/(float)dw-0.5f;const float py=((float)y+0.5f)*(float)sh/(float)dh-0.5f;const DWORD ref=Pattern(px,py);r7Error+=AbsRgb(r7Pixels[(size_t)y*dw+x],ref);referenceSamples+=3;}
    const double r7Mae=(double)r7Error/(double)referenceSamples;

    struct Case{float sx,sy;const char*name;};
    const Case cases[]={{0.5f,0.0f,"X_HALF"},{0.0f,0.5f,"Y_HALF"},{0.5f,0.5f,"XY_HALF"}};
    double worstImprovement=100.0;
    for(unsigned ci=0;ci<sizeof(cases)/sizeof(cases[0]);++ci)
    {
        IDirect3DTexture9 *history=0,*motion=0;if(FAILED(CreatePattern(d,sw,sh,cases[ci].sx,cases[ci].sy,&history)))return 20+(int)ci;if(FAILED(CreateMotion(d,mw,mh,cases[ci].sx,cases[ci].sy,&motion)))return 24+(int)ci;
        if(FAILED(Draw(d,tsrS,dw,dh,resolve,curr,history,motion,sizes)))return 28+(int)ci;
        std::vector<DWORD>tsrPixels;if(FAILED(Read(d,tsrS,dw,dh,tsrPixels)))return 32+(int)ci;
        uint64_t tsrError=0;for(UINT y=6;y+6<dh;++y)for(UINT x=6;x+6<dw;++x){const float px=((float)x+0.5f)*(float)sw/(float)dw-0.5f;const float py=((float)y+0.5f)*(float)sh/(float)dh-0.5f;tsrError+=AbsRgb(tsrPixels[(size_t)y*dw+x],Pattern(px,py));}
        const double tsrMae=(double)tsrError/(double)referenceSamples;const double improvement=(1.0-tsrMae/r7Mae)*100.0;worstImprovement=std::min(worstImprovement,improvement);
        std::printf("TSR1_NATIVE_%s_R7_MAE=%.6f TSR_MAE=%.6f IMPROVEMENT_PCT=%.3f\n",cases[ci].name,r7Mae,tsrMae,improvement);
        motion->Release();history->Release();
        if(!(tsrMae<r7Mae*0.90)){std::printf("TSR1_NATIVE_%s=FAIL\n",cases[ci].name);return 40+(int)ci;}
        std::printf("TSR1_NATIVE_%s=PASS\n",cases[ci].name);
    }
    std::printf("TSR1_NATIVE_REFERENCE_WORST_IMPROVEMENT_PCT=%.3f\n",worstImprovement);
    std::printf("TSR1_NATIVE_REFERENCE_QUALITY=PASS\n");

    resolve->Release();r7->Release();tsrS->Release();tsrOut->Release();r7S->Release();r7Out->Release();curr->Release();d->Release();DestroyWindow(wnd);d3d->Release();FreeLibrary(m);return 0;
}
