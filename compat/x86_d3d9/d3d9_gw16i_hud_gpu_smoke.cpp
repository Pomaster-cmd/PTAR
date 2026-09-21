#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#pragma warning(disable:4505)
#include "ptar_gw16i_hud_d3d9.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_GW16I_HUD_GPU_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR GW16I HUD GPU Smoke",
        WS_OVERLAPPEDWINDOW,0,0,960,540,0,0,inst,0);
}

static int Analyze(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* rt,
    UINT w,UINT h,
    const char* label,
    UINT minChanged)
{
    IDirect3DSurface9* read=0;
    HRESULT hr=dev->CreateOffscreenPlainSurface(
        w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);
    if(FAILED(hr)) return 30;

    hr=dev->GetRenderTargetData(rt,read);
    if(FAILED(hr)) return 31;

    D3DLOCKED_RECT lr={};
    hr=read->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)) return 32;

    unsigned long changed=0;
    unsigned long topLeft=0;

    for(UINT y=0;y<h;++y)
    {
        const DWORD* row=
            (const DWORD*)((const BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<w;++x)
        {
            const DWORD rgb=row[x]&0x00FFFFFFu;
            if(rgb!=0x00112233u)
            {
                ++changed;
                if(x<700 && y<300)
                    ++topLeft;
            }
        }
    }

    read->UnlockRect();
    read->Release();

    std::printf(
        "%s changed=%lu top_left=%lu\n",
        label,changed,topLeft);

    if(changed<minChanged || topLeft<minChanged)
        return 33;

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
        (PFN_Direct3DCreate9)GetProcAddress(
            d3d9,"Direct3DCreate9");
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
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &pp,&dev);

    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp,&dev);
    }
    if(FAILED(hr)||!dev) return 7;

    IDirect3DTexture9* rtTex=0;
    IDirect3DSurface9* rt=0;
    hr=dev->CreateTexture(
        960,540,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
        &rtTex,0);
    if(FAILED(hr)) return 8;

    hr=rtTex->GetSurfaceLevel(0,&rt);
    if(FAILED(hr)) return 9;

    dev->SetDepthStencilSurface(0);
    dev->SetRenderTarget(0,rt);
    dev->Clear(
        0,0,D3DCLEAR_TARGET,
        D3DCOLOR_XRGB(0x11,0x22,0x33),
        1.0f,0);

    // Hostile state that killed the shader-based HUD must be irrelevant to
    // Clear(rects)-based rendering.
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);
    dev->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_NEVER);
    dev->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
    dev->SetRenderState(D3DRS_FOGENABLE,TRUE);

    hr=PtGw16RenderExactHudD3D9(
        dev,
        true,        // visible
        0,           // state
        true,        // marker
        123,         // frame id
        false,       // REAL
        59.0,
        1280,720,
        2,           // PTAR X15
        0,0,0,0);

    std::printf("HUD_CLEAR_RECTS_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr)) return 10;

    int rc=Analyze(dev,rt,960,540,"HUD_MAIN",200);
    if(rc) return rc;

    // Validate the F8 feedback vocabulary too.
    dev->SetRenderTarget(0,rt);
    dev->Clear(
        0,0,D3DCLEAR_TARGET,
        D3DCOLOR_XRGB(0x11,0x22,0x33),
        1.0f,0);

    hr=PtGw16RenderExactHudD3D9(
        dev,
        false,
        0,
        false,
        0,
        false,
        0.0,
        1280,720,
        2,
        5,0,0,0);   // F8 OK

    std::printf("HUD_FEEDBACK_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr)) return 11;

    rc=Analyze(dev,rt,960,540,"HUD_FEEDBACK",50);
    if(rc) return rc;

    rt->Release();
    rtTex->Release();
    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(d3d9);

    std::printf("D3D9_GW16I_HUD_GPU_SMOKE=PASS\n");
    return 0;
}
