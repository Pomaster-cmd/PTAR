#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Create9)(UINT);

static HWND CreateSmokeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    const wchar_t* cls=L"PTAR_ISOLATED_PROXY_SMOKE_WINDOW";

    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=cls;
    RegisterClassW(&wc);

    HWND hwnd=CreateWindowExW(
        0,cls,L"PTAR isolated proxy smoke",
        WS_OVERLAPPEDWINDOW,
        0,0,800,600,
        0,0,inst,0);

    if(hwnd)
    {
        ShowWindow(hwnd,SW_SHOW);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

static int DrawAndPresent(
    IDirect3DDevice9* dev,
    int frames,
    double* elapsedMs)
{
    if(!dev || frames<=0 || !elapsedMs)
        return 10;

    LARGE_INTEGER freq={};
    LARGE_INTEGER t0={};
    LARGE_INTEGER t1={};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    for(int i=0;i<frames;++i)
    {
        HRESULT hr=dev->Clear(
            0,0,D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(
                (i*13)&255,
                (i*7)&255,
                (i*3)&255),
            1.0f,0);
        if(FAILED(hr))
        {
            std::printf(
                "PROXY_CLEAR_FAIL frame=%d hr=0x%08lX\n",
                i,(unsigned long)hr);
            return 11;
        }

        hr=dev->Present(0,0,0,0);
        if(FAILED(hr))
        {
            std::printf(
                "PROXY_PRESENT_FAIL frame=%d hr=0x%08lX\n",
                i,(unsigned long)hr);
            return 12;
        }
    }

    QueryPerformanceCounter(&t1);
    *elapsedMs=
        (double)(t1.QuadPart-t0.QuadPart)*
        1000.0/(double)freq.QuadPart;
    return 0;
}

int main()
{
    HMODULE proxy=LoadLibraryW(L"d3d9.dll");
    if(!proxy)
    {
        std::printf(
            "FAIL LoadLibrary gle=%lu\n",
            (unsigned long)GetLastError());
        return 2;
    }

    PFN_Create9 create9=
        (PFN_Create9)GetProcAddress(
            proxy,"Direct3DCreate9");
    if(!create9)
        return 3;

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d)
        return 4;

    HWND hwnd=CreateSmokeWindow();
    if(!hwnd)
        return 5;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=640;
    pp.BackBufferHeight=480;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.EnableAutoDepthStencil=FALSE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &pp,&dev);

    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_REF,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp,&dev);
    }

    std::printf(
        "ISOLATED_PROXY_CREATE hr=0x%08lX dev=%p\n",
        (unsigned long)hr,dev);

    if(FAILED(hr)||!dev)
        return 6;

    double ms=0.0;
    int rc=DrawAndPresent(dev,20,&ms);
    std::printf(
        "ISOLATED_PROXY_20_REAL_MS=%.3f\n",
        ms);
    if(rc)
        return rc;

    // Give the separate Sync1 presenter enough time to drain the REAL queue.
    Sleep(500);

    // Reset is also a lifecycle gate: it must stop the old presenter before
    // resetting DEFAULT-pool producer resources, then recreate it cleanly.
    pp.BackBufferWidth=800;
    pp.BackBufferHeight=600;
    hr=dev->Reset(&pp);

    std::printf(
        "ISOLATED_PROXY_RESET hr=0x%08lX returned=%ux%u\n",
        (unsigned long)hr,
        pp.BackBufferWidth,
        pp.BackBufferHeight);

    if(FAILED(hr))
        return 20;

    double ms2=0.0;
    rc=DrawAndPresent(dev,8,&ms2);
    std::printf(
        "ISOLATED_PROXY_POST_RESET_8_REAL_MS=%.3f\n",
        ms2);
    if(rc)
        return rc;

    Sleep(300);

    // Trigger one more Reset so the runtime writes the presenter stop counters
    // while the process is still alive and the CI can inspect them.
    pp.BackBufferWidth=640;
    pp.BackBufferHeight=480;
    hr=dev->Reset(&pp);
    std::printf(
        "ISOLATED_PROXY_FINAL_RESET hr=0x%08lX\n",
        (unsigned long)hr);

    if(FAILED(hr))
        return 21;

    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(proxy);

    // 20 source frames are intentionally governed at <=60 REAL/s, so roughly
    // 333 ms is expected. A >750 ms result is evidence that physical Sync1
    // presentation has leaked back into the game/source path.
    if(ms>750.0)
    {
        std::printf(
            "FAIL source path appears presenter-blocked ms=%.3f\n",
            ms);
        return 22;
    }

    std::printf("D3D9_ISOLATED_PROXY_SMOKE=PASS\n");
    return 0;
}
