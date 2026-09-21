#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_D3D9_READBACK_PROBE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR D3D9 readback probe",
        WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,inst,0);
}

static int RunCase(
    IDirect3DDevice9* dev,
    UINT w,UINT h,
    int frames)
{
    IDirect3DTexture9* rtTex=0;
    IDirect3DSurface9* rt=0;
    IDirect3DSurface9* sys=0;

    HRESULT hr=dev->CreateTexture(
        w,h,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
        &rtTex,0);
    if(FAILED(hr)||!rtTex) return 20;

    hr=rtTex->GetSurfaceLevel(0,&rt);
    if(FAILED(hr)||!rt) return 21;

    hr=dev->CreateOffscreenPlainSurface(
        w,h,D3DFMT_A8R8G8B8,
        D3DPOOL_SYSTEMMEM,&sys,0);
    if(FAILED(hr)||!sys) return 22;

    LARGE_INTEGER freq={};
    LARGE_INTEGER t0={};
    LARGE_INTEGER t1={};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    unsigned long checksum=0;

    for(int i=0;i<frames;++i)
    {
        dev->SetRenderTarget(0,rt);
        dev->Clear(
            0,0,D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(
                (i*3)&255,
                (i*5)&255,
                (i*7)&255),
            1.0f,0);

        hr=dev->GetRenderTargetData(rt,sys);
        if(FAILED(hr))
        {
            std::printf(
                "READBACK_FAIL %ux%u frame=%d hr=0x%08lX\n",
                w,h,i,(unsigned long)hr);
            return 23;
        }

        D3DLOCKED_RECT lr={};
        hr=sys->LockRect(&lr,0,D3DLOCK_READONLY);
        if(FAILED(hr)) return 24;
        checksum+=*(const DWORD*)lr.pBits;
        sys->UnlockRect();
    }

    QueryPerformanceCounter(&t1);

    const double ms=
        (double)(t1.QuadPart-t0.QuadPart)*
        1000.0/(double)freq.QuadPart;

    std::printf(
        "READBACK_%ux%u_%d_FRAMES_MS=%.3f AVG_MS=%.3f CHECK=%lu\n",
        w,h,frames,ms,ms/(double)frames,checksum);

    sys->Release();
    rt->Release();
    rtTex->Release();
    return 0;
}

int main()
{
    HMODULE dll=LoadLibraryW(L"d3d9.dll");
    if(!dll) return 2;
    PFN_Direct3DCreate9 create9=
        (PFN_Direct3DCreate9)GetProcAddress(dll,"Direct3DCreate9");
    if(!create9) return 3;

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d) return 4;

    HWND hwnd=MakeWindow();
    if(!hwnd) return 5;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=640;
    pp.BackBufferHeight=360;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED,
        &pp,&dev);
    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|
            D3DCREATE_MULTITHREADED,
            &pp,&dev);
    }
    if(FAILED(hr)||!dev) return 6;

    int rc=RunCase(dev,1280,720,30);
    if(!rc) rc=RunCase(dev,1600,900,30);
    if(!rc) rc=RunCase(dev,1920,1080,30);

    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(dll);

    if(rc) return rc;

    std::printf("D3D9_REGULAR_READBACK_PROBE=PASS\n");
    return 0;
}
