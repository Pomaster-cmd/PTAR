#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include "ptar_fg_me_coarse_ps_bytecode.h"
#include "ptar_fg_me_refine_ps_bytecode.h"
#include "ptar_fg_interpolate_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

struct Vtx { float x,y,z,rhw,u,v; };

static HRESULT DrawPass(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT w,UINT h,
    IDirect3DPixelShader9* ps,
    IDirect3DTexture9* t0,
    IDirect3DTexture9* t1,
    IDirect3DTexture9* t2,
    const float* c0)
{
    dev->SetTexture(0,0);dev->SetTexture(1,0);dev->SetTexture(2,0);
    HRESULT hr=dev->SetDepthStencilSurface(0);
    if(FAILED(hr)) return hr;
    hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={0,0,w,h,0.0f,1.0f};
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);

    for(DWORD s=0;s<3;++s){
        dev->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    }

    if(c0) dev->SetPixelShaderConstantF(0,c0,1);
    dev->SetPixelShader(ps);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetTexture(0,t0);dev->SetTexture(1,t1);dev->SetTexture(2,t2);

    Vtx q[4]={
        {-0.5f,-0.5f,0,1,0,0},
        {(float)w-0.5f,-0.5f,0,1,1,0},
        {-0.5f,(float)h-0.5f,0,1,0,1},
        {(float)w-0.5f,(float)h-0.5f,0,1,1,1}
    };
    hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));
    dev->SetTexture(0,0);dev->SetTexture(1,0);dev->SetTexture(2,0);
    return hr;
}

static HRESULT CreateRT(
    IDirect3DDevice9* dev,UINT w,UINT h,D3DFORMAT fmt,
    IDirect3DTexture9** tex,IDirect3DSurface9** surf)
{
    *tex=0;*surf=0;
    HRESULT hr=dev->CreateTexture(
        w,h,1,D3DUSAGE_RENDERTARGET,fmt,D3DPOOL_DEFAULT,tex,0);
    if(FAILED(hr)) return hr;
    hr=(*tex)->GetSurfaceLevel(0,surf);
    if(FAILED(hr)){(*tex)->Release();*tex=0;}
    return hr;
}

static HRESULT CreatePattern(
    IDirect3DDevice9* dev,UINT w,UINT h,int shift,IDirect3DTexture9** tex)
{
    *tex=0;
    HRESULT hr=dev->CreateTexture(
        w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,tex,0);
    if(FAILED(hr)) return hr;

    D3DLOCKED_RECT lr={};
    hr=(*tex)->LockRect(0,&lr,0,0);
    if(FAILED(hr)) return hr;

    for(UINT y=0;y<h;++y){
        DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<w;++x){
            const bool checker=((x/16+y/16)&1)!=0;
            BYTE base=checker?52:30;
            BYTE r=base,g=(BYTE)(base+8),b=(BYTE)(base+16);

            int bx=(int)x-(250+shift);
            if(bx>=0 && bx<48 && y>90 && y<270){
                r=230;g=210;b=55;
            }
            if(((int)x-(120+shift))>=0 && ((int)x-(120+shift))<12 &&
               y>40 && y<320){
                r=50;g=220;b=240;
            }
            row[x]=0xFF000000u|((DWORD)r<<16)|((DWORD)g<<8)|b;
        }
    }
    (*tex)->UnlockRect(0);
    return S_OK;
}

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_FG_GPU_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR FG GPU Smoke",
        WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,inst,0);
}

int main()
{
    wchar_t sys[MAX_PATH]={0};
    if(!GetSystemDirectoryW(sys,MAX_PATH)) return 2;
    wcscat_s(sys,L"\\d3d9.dll");
    HMODULE d3d9=LoadLibraryW(sys);
    if(!d3d9){std::printf("FAIL load system d3d9\n");return 3;}

    PFN_Direct3DCreate9 create9=
        (PFN_Direct3DCreate9)GetProcAddress(d3d9,"Direct3DCreate9");
    if(!create9) return 4;

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d) return 5;

    HWND hwnd=MakeWindow();
    if(!hwnd) return 6;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=640;pp.BackBufferHeight=360;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)){
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    }
    if(FAILED(hr)||!dev){
        std::printf("FAIL CreateDevice hr=0x%08lX\n",(unsigned long)hr);
        return 7;
    }

    IDirect3DTexture9 *prev=0,*curr=0,*coarse=0,*fine=0,*gen=0;
    IDirect3DSurface9 *coarseS=0,*fineS=0,*genS=0;
    IDirect3DPixelShader9 *coarsePS=0,*finePS=0,*genPS=0;

    hr=CreatePattern(dev,640,360,0,&prev); if(FAILED(hr)) return 10;
    hr=CreatePattern(dev,640,360,8,&curr); if(FAILED(hr)) return 11;
    hr=CreateRT(dev,160,90,D3DFMT_A8R8G8B8,&coarse,&coarseS); if(FAILED(hr)) return 12;
    hr=CreateRT(dev,320,180,D3DFMT_A8R8G8B8,&fine,&fineS); if(FAILED(hr)) return 13;
    hr=CreateRT(dev,640,360,D3DFMT_A8R8G8B8,&gen,&genS); if(FAILED(hr)) return 14;

    hr=dev->CreatePixelShader((DWORD*)g_ptarFgMeCoarsePs,&coarsePS); if(FAILED(hr)) return 15;
    hr=dev->CreatePixelShader((DWORD*)g_ptarFgMeRefinePs,&finePS); if(FAILED(hr)) return 16;
    hr=dev->CreatePixelShader((DWORD*)g_ptarFgInterpolatePs,&genPS); if(FAILED(hr)) return 17;

    float out[4]={640.0f,360.0f,1.0f/640.0f,1.0f/360.0f};

    hr=DrawPass(dev,coarseS,160,90,coarsePS,prev,curr,0,out);
    if(FAILED(hr)){std::printf("FAIL coarse 0x%08lX\n",(unsigned long)hr);return 20;}
    hr=DrawPass(dev,fineS,320,180,finePS,prev,curr,coarse,out);
    if(FAILED(hr)){std::printf("FAIL refine 0x%08lX\n",(unsigned long)hr);return 21;}
    hr=DrawPass(dev,genS,640,360,genPS,prev,curr,fine,out);
    if(FAILED(hr)){std::printf("FAIL interpolate 0x%08lX\n",(unsigned long)hr);return 22;}

    IDirect3DSurface9* readback=0;
    hr=dev->CreateOffscreenPlainSurface(
        640,360,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,0);
    if(FAILED(hr)) return 23;
    hr=dev->GetRenderTargetData(genS,readback);
    if(FAILED(hr)){std::printf("FAIL readback 0x%08lX\n",(unsigned long)hr);return 24;}

    D3DLOCKED_RECT lr={};
    hr=readback->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)) return 25;

    uint64_t sum=0;
    uint32_t nonzero=0;
    for(UINT y=0;y<360;++y){
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<640;++x){
            DWORD v=row[x];
            sum+=(uint64_t)(v&0x00FFFFFFu);
            if((v&0x00FFFFFFu)!=0) ++nonzero;
        }
    }
    readback->UnlockRect();

    IDirect3DSurface9* motionRead=0;
    hr=dev->CreateOffscreenPlainSurface(
        320,180,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&motionRead,0);
    if(FAILED(hr)) return 26;
    hr=dev->GetRenderTargetData(fineS,motionRead);
    if(FAILED(hr)) return 27;
    hr=motionRead->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)) return 28;

    uint32_t moved=0;
    for(UINT y=0;y<180;++y){
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<320;++x){
            DWORD v=row[x];
            BYTE r=(BYTE)((v>>16)&255);
            BYTE g=(BYTE)((v>>8)&255);
            if(r<124||r>132||g<124||g>132) ++moved;
        }
    }
    motionRead->UnlockRect();

    std::printf("FG_GENERATED_SUM=%llu\n",(unsigned long long)sum);
    std::printf("FG_GENERATED_NONZERO=%u\n",nonzero);
    std::printf("FG_MOTION_NONZERO_PIXELS=%u\n",moved);

    if(nonzero<640u*360u/2u){
        std::printf("FAIL generated output mostly empty\n");
        return 29;
    }
    if(moved==0){
        std::printf("FAIL motion estimator produced only zero motion\n");
        return 30;
    }

    std::printf("D3D9_FG_GPU_PIPELINE_SMOKE=PASS\n");

    motionRead->Release();readback->Release();
    genPS->Release();finePS->Release();coarsePS->Release();
    genS->Release();gen->Release();
    fineS->Release();fine->Release();
    coarseS->Release();coarse->Release();
    curr->Release();prev->Release();
    dev->Release();DestroyWindow(hwnd);d3d->Release();FreeLibrary(d3d9);
    return 0;
}
