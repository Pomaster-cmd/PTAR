#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include "ptar_universal_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx { float x,y,z,rhw,u,v; };

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_UNIVERSAL_SPATIAL_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR Universal Spatial Smoke",
        WS_OVERLAPPEDWINDOW,0,0,960,540,0,0,inst,0);
}

static HRESULT CreatePattern(
    IDirect3DDevice9* dev,UINT w,UINT h,IDirect3DTexture9** tex)
{
    *tex=0;
    HRESULT hr=dev->CreateTexture(
        w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,tex,0);
    if(FAILED(hr)) return hr;

    D3DLOCKED_RECT lr={};
    hr=(*tex)->LockRect(0,&lr,0,0);
    if(FAILED(hr)) return hr;

    for(UINT y=0;y<h;++y)
    {
        DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<w;++x)
        {
            const BYTE r=(BYTE)((x*255u)/(w?w:1u));
            const BYTE g=(BYTE)((y*255u)/(h?h:1u));
            const BYTE b=(BYTE)((((x/12u)^(y/12u))&1u)?220u:35u);
            row[x]=0xFF000000u|((DWORD)r<<16)|((DWORD)g<<8)|b;
        }
    }

    (*tex)->UnlockRect(0);
    return S_OK;
}

static HRESULT DrawRect(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT targetW,UINT targetH,
    UINT x,UINT y,UINT w,UINT h,
    IDirect3DPixelShader9* ps,
    IDirect3DTexture9* src,
    const float* c0)
{
    HRESULT hr=dev->SetDepthStencilSurface(0);
    if(FAILED(hr)) return hr;
    hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={0,0,targetW,targetH,0.0f,1.0f};
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);

    hr=dev->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0,0,0),1.0f,0);
    if(FAILED(hr)) return hr;

    dev->SetPixelShaderConstantF(0,c0,1);
    dev->SetPixelShader(ps);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetTexture(0,src);

    hr=dev->BeginScene();
    if(FAILED(hr)) return hr;

    Vtx q[4]={
        {(float)x-0.5f,(float)y-0.5f,0,1,0,0},
        {(float)(x+w)-0.5f,(float)y-0.5f,0,1,1,0},
        {(float)x-0.5f,(float)(y+h)-0.5f,0,1,0,1},
        {(float)(x+w)-0.5f,(float)(y+h)-0.5f,0,1,1,1}
    };
    hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));
    HRESULT endHr=dev->EndScene();
    dev->SetTexture(0,0);
    if(FAILED(hr)) return hr;
    return endHr;
}

static int ValidateCase(
    IDirect3DDevice9* dev,
    IDirect3DPixelShader9* ps,
    UINT srcW,UINT srcH,
    UINT targetW,UINT targetH,
    UINT fitX,UINT fitY,UINT fitW,UINT fitH,
    const char* label)
{
    IDirect3DTexture9* src=0;
    HRESULT hr=CreatePattern(dev,srcW,srcH,&src);
    if(FAILED(hr)) return 20;

    IDirect3DTexture9* dst=0;
    IDirect3DSurface9* dstS=0;
    hr=dev->CreateTexture(
        targetW,targetH,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);
    if(FAILED(hr)) return 21;
    hr=dst->GetSurfaceLevel(0,&dstS);
    if(FAILED(hr)) return 22;

    float sizes[4]={
        (float)srcW,(float)srcH,
        (float)fitW,(float)fitH};

    hr=DrawRect(
        dev,dstS,targetW,targetH,
        fitX,fitY,fitW,fitH,
        ps,src,sizes);
    if(FAILED(hr))
    {
        std::printf("FAIL %s draw hr=0x%08lX\n",label,(unsigned long)hr);
        return 23;
    }

    IDirect3DSurface9* read=0;
    hr=dev->CreateOffscreenPlainSurface(
        targetW,targetH,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);
    if(FAILED(hr)) return 24;
    hr=dev->GetRenderTargetData(dstS,read);
    if(FAILED(hr)) return 25;

    D3DLOCKED_RECT lr={};
    hr=read->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)) return 26;

    uint64_t insideSum=0;
    uint64_t outsideSum=0;
    uint32_t insidePixels=0;
    uint32_t outsidePixels=0;

    for(UINT y=0;y<targetH;++y)
    {
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<targetW;++x)
        {
            const DWORD rgb=row[x]&0x00FFFFFFu;
            const bool inside=
                x>=fitX && x<fitX+fitW &&
                y>=fitY && y<fitY+fitH;
            if(inside){insideSum+=rgb;++insidePixels;}
            else{outsideSum+=rgb;++outsidePixels;}
        }
    }
    read->UnlockRect();

    std::printf(
        "%s inside_sum=%llu outside_sum=%llu inside_pixels=%u outside_pixels=%u\n",
        label,
        (unsigned long long)insideSum,
        (unsigned long long)outsideSum,
        insidePixels,outsidePixels);

    read->Release();
    dstS->Release();
    dst->Release();
    src->Release();

    if(insideSum==0 || insidePixels==0)
        return 27;
    if(outsidePixels && outsideSum!=0)
        return 28;
    return 0;
}

int main()
{
    wchar_t sys[MAX_PATH]={0};
    if(!GetSystemDirectoryW(sys,MAX_PATH)) return 2;
    wcscat_s(sys,L"\\d3d9.dll");

    HMODULE d3d9=LoadLibraryW(sys);
    if(!d3d9) return 3;
    PFN_Direct3DCreate9 create9=
        (PFN_Direct3DCreate9)GetProcAddress(d3d9,"Direct3DCreate9");
    if(!create9) return 4;

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d) return 5;
    HWND hwnd=MakeWindow();
    if(!hwnd) return 6;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=960;
    pp.BackBufferHeight=540;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    }
    if(FAILED(hr)||!dev) return 7;

    IDirect3DPixelShader9* ps=0;
    hr=dev->CreatePixelShader((DWORD*)g_ptarUniversalPs,&ps);
    if(FAILED(hr)||!ps) return 8;

    int rc=ValidateCase(
        dev,ps,
        800,450,
        960,540,
        0,0,960,540,
        "UNIVERSAL_1600X900_EQUIV");
    if(rc) return rc;

    rc=ValidateCase(
        dev,ps,
        640,480,
        960,540,
        120,0,720,540,
        "UNIVERSAL_4X3_ASPECT_FIT");
    if(rc) return rc;

    ps->Release();
    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(d3d9);

    std::printf("D3D9_UNIVERSAL_SPATIAL_GPU_SMOKE=PASS\n");
    return 0;
}
