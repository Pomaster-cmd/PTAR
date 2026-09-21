#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

struct Ctx
{
    IDirect3DDevice9* dev;
    IDirect3DSwapChain9* swap;
    HANDLE stopEvent;
    HANDLE thread;
    volatile LONG ok;
    volatile LONG busy;
    volatile LONG errors;
};

static Ctx g={};

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_SWAPCHAIN_NOWAIT_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR swapchain no-wait",
        WS_OVERLAPPEDWINDOW,
        0,0,320,180,0,0,inst,0);
}

static DWORD WINAPI PresentThread(LPVOID)
{
    while(WaitForSingleObject(g.stopEvent,0)!=WAIT_OBJECT_0)
    {
        HRESULT hr=g.swap->Present(
            0,0,0,0,
            D3DPRESENT_DONOTWAIT);

        if(SUCCEEDED(hr))
            InterlockedIncrement(&g.ok);
        else if(hr==D3DERR_WASSTILLDRAWING)
            InterlockedIncrement(&g.busy);
        else
            InterlockedIncrement(&g.errors);

        Sleep(1);
    }
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
    pp.BackBufferWidth=320;
    pp.BackBufferHeight=180;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

    DWORD flags=
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED;

    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,
        hwnd,flags,&pp,&g.dev);
    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,
            hwnd,flags,&pp,&g.dev);
    }
    if(FAILED(hr)||!g.dev) return 6;

    hr=g.dev->GetSwapChain(0,&g.swap);
    if(FAILED(hr)||!g.swap) return 7;

    g.stopEvent=CreateEventW(0,TRUE,FALSE,0);
    if(!g.stopEvent) return 8;

    g.thread=CreateThread(0,0,PresentThread,0,0,0);
    if(!g.thread) return 9;

    // Producer workload on the same device. If swapchain Present(DONOTWAIT)
    // still monopolizes the D3D9 runtime until VBlank, these 60 iterations will
    // inflate toward ~1 second just like the rejected device-Present design.
    IDirect3DTexture9* tex=0;
    IDirect3DSurface9* surf=0;
    hr=g.dev->CreateTexture(
        320,180,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
        &tex,0);
    if(FAILED(hr)||!tex) return 10;
    tex->GetSurfaceLevel(0,&surf);
    if(!surf) return 11;

    LARGE_INTEGER freq={};
    LARGE_INTEGER t0={};
    LARGE_INTEGER t1={};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    for(int i=0;i<60;++i)
    {
        g.dev->SetRenderTarget(0,surf);
        g.dev->Clear(
            0,0,D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(
                (i*3)&255,
                (i*5)&255,
                (i*7)&255),
            1.0f,0);
        Sleep(5);
    }

    QueryPerformanceCounter(&t1);
    const double producerMs=
        (double)(t1.QuadPart-t0.QuadPart)*
        1000.0/(double)freq.QuadPart;

    SetEvent(g.stopEvent);
    WaitForSingleObject(g.thread,3000);

    std::printf("SWAPCHAIN_NOWAIT_PRODUCER_MS=%.3f\n",producerMs);
    std::printf("SWAPCHAIN_NOWAIT_OK=%ld\n",InterlockedCompareExchange(&g.ok,0,0));
    std::printf("SWAPCHAIN_NOWAIT_BUSY=%ld\n",InterlockedCompareExchange(&g.busy,0,0));
    std::printf("SWAPCHAIN_NOWAIT_ERRORS=%ld\n",InterlockedCompareExchange(&g.errors,0,0));

    const LONG ok=InterlockedCompareExchange(&g.ok,0,0);
    const LONG errors=InterlockedCompareExchange(&g.errors,0,0);

    CloseHandle(g.thread);
    CloseHandle(g.stopEvent);
    surf->Release();
    tex->Release();
    g.swap->Release();
    g.dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(dll);

    if(producerMs>650.0)
        return 12;
    if(ok<1)
        return 13;
    if(errors!=0)
        return 14;

    std::printf("D3D9_SWAPCHAIN_DONOTWAIT_PRODUCER_ISOLATION=PASS\n");
    return 0;
}
