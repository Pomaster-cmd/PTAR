#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#pragma warning(disable:4505)
#include "ptar_diag_ui_bridge.h"
#include "ptar_gw16i_hud_d3d9.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_DIAG_UI_BRIDGE_GPU_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR Diag UI Bridge GPU Smoke",
        WS_OVERLAPPEDWINDOW,0,0,960,540,0,0,inst,0);
}

static bool WriteUi(unsigned seq,int type,int duration)
{
    wchar_t v[32]={0};
    _snwprintf_s(v,_countof(v),_TRUNCATE,L"%u",seq);
    if(!WritePrivateProfileStringW(L"PTAR_DIAG_UI",L"Sequence",v,g_ptarDiagUiPath)) return false;
    _snwprintf_s(v,_countof(v),_TRUNCATE,L"%d",type);
    if(!WritePrivateProfileStringW(L"PTAR_DIAG_UI",L"Type",v,g_ptarDiagUiPath)) return false;
    _snwprintf_s(v,_countof(v),_TRUNCATE,L"%d",duration);
    if(!WritePrivateProfileStringW(L"PTAR_DIAG_UI",L"DurationMs",v,g_ptarDiagUiPath)) return false;
    if(!WritePrivateProfileStringW(L"PTAR_DIAG_UI",L"A",L"0",g_ptarDiagUiPath)) return false;
    if(!WritePrivateProfileStringW(L"PTAR_DIAG_UI",L"B",L"0",g_ptarDiagUiPath)) return false;
    if(!WritePrivateProfileStringW(L"PTAR_DIAG_UI",L"C",L"0",g_ptarDiagUiPath)) return false;
    // Flush the profile cache before the runtime-side poll.
    WritePrivateProfileStringW(0,0,0,g_ptarDiagUiPath);
    return true;
}

static int CountChanged(IDirect3DDevice9* dev,IDirect3DSurface9* rt,UINT w,UINT h)
{
    IDirect3DSurface9* read=0;
    HRESULT hr=dev->CreateOffscreenPlainSurface(
        w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);
    if(FAILED(hr)) return -1;
    hr=dev->GetRenderTargetData(rt,read);
    if(FAILED(hr)){read->Release();return -2;}
    D3DLOCKED_RECT lr={};
    hr=read->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)){read->Release();return -3;}
    int changed=0;
    for(UINT y=0;y<h;++y)
    {
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<w;++x)
            if((row[x]&0x00FFFFFFu)!=0x00112233u) ++changed;
    }
    read->UnlockRect();
    read->Release();
    return changed;
}

static int RenderType(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* rt,
    int type,
    unsigned seq,
    const char* label)
{
    if(!WriteUi(seq,type,2500)) return 40;
    g_ptarDiagUiNextPollMs=0;
    PtDiagUiTick();
    if(!PtDiagUiActive() || PtDiagUiType()!=type) return 41;

    dev->SetRenderTarget(0,rt);
    dev->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0x11,0x22,0x33),1.0f,0);
    HRESULT hr=PtGw16RenderExactHudD3D9(
        dev,false,0,false,0,false,0.0,1280,720,2,
        PtDiagUiType(),PtDiagUiArgA(),PtDiagUiArgB(),PtDiagUiArgC());
    if(FAILED(hr)) return 42;
    int changed=CountChanged(dev,rt,960,540);
    std::printf("%s type=%d changed=%d\n",label,type,changed);
    if(changed<1000) return 43;
    return 0;
}

int main()
{
    PtDiagUiInit(GetModuleHandleW(0));
    if(!g_ptarDiagUiPath[0]) return 2;
    DeleteFileW(g_ptarDiagUiPath);

    wchar_t sys[MAX_PATH]={0};
    if(!GetSystemDirectoryW(sys,MAX_PATH)) return 3;
    wcscat_s(sys,L"\\d3d9.dll");
    HMODULE d3d9=LoadLibraryW(sys);
    if(!d3d9) return 4;
    PFN_Direct3DCreate9 create9=(PFN_Direct3DCreate9)GetProcAddress(d3d9,"Direct3DCreate9");
    if(!create9) return 5;
    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d) return 6;
    HWND hwnd=MakeWindow();
    if(!hwnd) return 7;

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
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)||!dev) return 8;

    IDirect3DTexture9* rtTex=0;
    IDirect3DSurface9* rt=0;
    hr=dev->CreateTexture(
        960,540,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,&rtTex,0);
    if(FAILED(hr)) return 9;
    hr=rtTex->GetSurfaceLevel(0,&rt);
    if(FAILED(hr)) return 10;
    dev->SetDepthStencilSurface(0);

    // Exercise every controller state that will be emitted by CTRL+F5/F1.
    const int types[]={19,20,21,22,23,24,25,26,27,28};
    const char* labels[]={
        "F5_PRE","F5_FG_ACTIVE","F5_POST","F5_MEASURE","F5_DONE",
        "F1_READY","F1_MEASURE","F1_DONE","F1_ERROR","F5_ERROR"};
    for(int i=0;i<10;++i)
    {
        int rc=RenderType(dev,rt,types[i],100u+(unsigned)i,labels[i]);
        if(rc) return rc+i;
    }

    // Duration/expiry gate: bridge must stop overriding the normal HUD.
    if(!WriteUi(999,24,250)) return 70;
    g_ptarDiagUiNextPollMs=0;
    PtDiagUiTick();
    if(!PtDiagUiActive()) return 71;
    Sleep(330);
    g_ptarDiagUiNextPollMs=0;
    PtDiagUiTick();
    if(PtDiagUiActive()) return 72;
    std::printf("DIAG_UI_EXPIRY=PASS\n");

    DeleteFileW(g_ptarDiagUiPath);
    rt->Release();
    rtTex->Release();
    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(d3d9);
    std::printf("D3D9_DIAG_UI_BRIDGE_GPU_SMOKE=PASS\n");
    return 0;
}
