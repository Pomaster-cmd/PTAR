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
#include "../../build_x86/ptar_tsr1_motion_subpixel_ps_bytecode.h"
#include "../../build_x86/ptar_tsr1_resolve_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};

static HWND MakeWindow()
{
    HINSTANCE i=GetModuleHandleW(0);WNDCLASSW c={};
    c.lpfnWndProc=DefWindowProcW;c.hInstance=i;c.lpszClassName=L"PTAR_TSR1_SMOKE";
    RegisterClassW(&c);
    return CreateWindowExW(0,c.lpszClassName,L"PTAR TSR1",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);
}

static HRESULT CreateRT(IDirect3DDevice9*d,UINT w,UINT h,IDirect3DTexture9**t,IDirect3DSurface9**s)
{
    *t=0;*s=0;HRESULT hr=d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t,0);
    if(FAILED(hr))return hr;hr=(*t)->GetSurfaceLevel(0,s);if(FAILED(hr)){(*t)->Release();*t=0;}return hr;
}

static BYTE Q(float v){int n=(int)floorf(std::max(0.0f,std::min(1.0f,v))*255.0f+0.5f);return (BYTE)n;}
static DWORD Pattern(float x,float y)
{
    float a=0.50f+0.18f*sinf(0.47f*x+0.11f*y)+0.13f*sinf(0.19f*x-0.41f*y);
    float b=0.50f+0.20f*sinf(0.37f*x-0.17f*y)+0.10f*cosf(0.23f*x+0.29f*y);
    float c=0.50f+0.17f*cosf(0.43f*x+0.31f*y)+0.12f*sinf(0.13f*x+0.39f*y);
    return 0xff000000u|((DWORD)Q(a)<<16)|((DWORD)Q(b)<<8)|(DWORD)Q(c);
}

static HRESULT CreatePattern(IDirect3DDevice9*d,UINT w,UINT h,float shiftX,float shiftY,IDirect3DTexture9**t)
{
    *t=0;HRESULT hr=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(hr))return hr;
    D3DLOCKED_RECT lr={};hr=(*t)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    for(UINT y=0;y<h;++y){DWORD*row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);for(UINT x=0;x<w;++x)row[x]=Pattern((float)x-shiftX,(float)y-shiftY);}(*t)->UnlockRect(0);return S_OK;
}

static HRESULT CreateMotion(IDirect3DDevice9*d,UINT w,UINT h,BYTE rg,BYTE confidence,IDirect3DTexture9**t)
{
    *t=0;HRESULT hr=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(hr))return hr;
    D3DLOCKED_RECT lr={};hr=(*t)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    DWORD p=0xff000000u|((DWORD)rg<<16)|((DWORD)rg<<8)|(DWORD)confidence;
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

int main()
{
    wchar_t p[MAX_PATH]={0};GetSystemDirectoryW(p,MAX_PATH);wcscat_s(p,L"\\d3d9.dll");HMODULE m=LoadLibraryW(p);if(!m)return 2;PFN_Direct3DCreate9 fn=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");IDirect3D9*d3d=fn?fn(D3D_SDK_VERSION):0;if(!d3d)return 3;HWND w=MakeWindow();if(!w)return 4;
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=144;pp.BackBufferHeight=96;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9*d=0;HRESULT hr=d3d->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(hr))hr=d3d->CreateDevice(0,D3DDEVTYPE_REF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(hr)||!d)return 5;

    const UINT sw=96,sh=64,dw=144,dh=96,mw=48,mh=32;
    IDirect3DTexture9 *curr=0,*histSame=0,*histShift=0,*motionZero=0,*motionHalf=0,*baseZero=0,*subOut=0,*r7Out=0,*tsrOut=0;
    IDirect3DSurface9 *subS=0,*r7S=0,*tsrS=0;
    if(FAILED(CreatePattern(d,sw,sh,0,0,&curr)))return 10;
    if(FAILED(CreatePattern(d,sw,sh,0,0,&histSame)))return 11;
    if(FAILED(CreatePattern(d,sw,sh,0.5f,0.5f,&histShift)))return 12;
    if(FAILED(CreateMotion(d,mw,mh,128,255,&motionZero)))return 13;
    if(FAILED(CreateMotion(d,mw,mh,130,255,&motionHalf)))return 14;
    if(FAILED(CreateMotion(d,mw,mh,128,255,&baseZero)))return 15;
    if(FAILED(CreateRT(d,mw,mh,&subOut,&subS)))return 16;
    if(FAILED(CreateRT(d,dw,dh,&r7Out,&r7S)))return 17;
    if(FAILED(CreateRT(d,dw,dh,&tsrOut,&tsrS)))return 18;

    IDirect3DPixelShader9 *r7=0,*sub=0,*resolve=0;
    if(FAILED(d->CreatePixelShader((const DWORD*)g_ptarSpec5Ps,&r7))||!r7)return 20;
    if(FAILED(d->CreatePixelShader((const DWORD*)g_ptarTsr1MotionSubpixelPs,&sub))||!sub)return 21;
    if(FAILED(d->CreatePixelShader((const DWORD*)g_ptarTsr1ResolvePs,&resolve))||!resolve)return 22;

    float sizes[4]={(float)sw,(float)sh,(float)dw,(float)dh};
    if(FAILED(Draw(d,r7S,dw,dh,r7,curr,0,0,sizes)))return 30;
    if(FAILED(Draw(d,tsrS,dw,dh,resolve,curr,histSame,motionZero,sizes)))return 31;
    std::vector<DWORD>a,b;if(FAILED(Read(d,r7S,dw,dh,a))||FAILED(Read(d,tsrS,dw,dh,b)))return 32;
    uint64_t sum=0;int maxe=0;unsigned over1=0;
    for(size_t i=0;i<a.size();++i)for(int s=0;s<=16;s+=8){int e=abs((int)((a[i]>>s)&255)-(int)((b[i]>>s)&255));sum+=e;maxe=std::max(maxe,e);if(e>1)++over1;}
    double mae=(double)sum/(double)(a.size()*3u);
    std::printf("TSR1_ZERO_MOTION_MAE=%.6f MAX=%d OVER1=%u\n",mae,maxe,over1);
    if(mae>0.05||maxe>1||over1!=0){std::printf("TSR1_ZERO_FALLBACK=FAIL\n");return 33;}
    std::printf("TSR1_ZERO_FALLBACK=PASS\n");

    // Known half-pixel source phase must activate the union-lattice path.
    if(FAILED(Draw(d,tsrS,dw,dh,resolve,curr,histShift,motionHalf,sizes)))return 34;
    if(FAILED(Read(d,tsrS,dw,dh,b)))return 35;unsigned changed=0;
    for(size_t i=0;i<a.size();++i){int e=abs((int)(a[i]&255)-(int)(b[i]&255))+abs((int)((a[i]>>8)&255)-(int)((b[i]>>8)&255))+abs((int)((a[i]>>16)&255)-(int)((b[i]>>16)&255));if(e>=3)++changed;}
    std::printf("TSR1_FRACTIONAL_CHANGED_PIXELS=%u OF %u\n",changed,(unsigned)a.size());
    if(changed<a.size()/20u){std::printf("TSR1_FRACTIONAL_PATH=FAIL\n");return 36;}
    std::printf("TSR1_FRACTIONAL_PATH=PASS\n");

    // Subpixel motion shader must turn a coarse zero lattice into a non-empty
    // fractional field on a shifted textured pair.
    float srcInfo[4]={(float)sw,(float)sh,1.0f/(float)sw,1.0f/(float)sh};
    if(FAILED(Draw(d,subS,mw,mh,sub,histShift,curr,baseZero,srcInfo)))return 37;
    std::vector<DWORD>mv;if(FAILED(Read(d,subS,mw,mh,mv)))return 38;unsigned confident=0,fractional=0;
    for(size_t i=0;i<mv.size();++i){BYTE r=(BYTE)((mv[i]>>16)&255),g=(BYTE)((mv[i]>>8)&255),cf=(BYTE)(mv[i]&255);if(cf>=32)++confident;if((r!=128||g!=128)&&cf>=32)++fractional;}
    std::printf("TSR1_SUBPIXEL_CONFIDENT=%u FRACTIONAL=%u OF %u\n",confident,fractional,(unsigned)mv.size());
    if(confident<mv.size()/20u||fractional<mv.size()/50u){std::printf("TSR1_SUBPIXEL_MOTION=FAIL\n");return 39;}
    std::printf("TSR1_SUBPIXEL_MOTION=PASS\n");
    std::printf("D3D9_TSR1_GPU_SMOKE=PASS\n");

    resolve->Release();sub->Release();r7->Release();tsrS->Release();tsrOut->Release();r7S->Release();r7Out->Release();subS->Release();subOut->Release();baseZero->Release();motionHalf->Release();motionZero->Release();histShift->Release();histSame->Release();curr->Release();d->Release();DestroyWindow(w);d3d->Release();FreeLibrary(m);return 0;
}
