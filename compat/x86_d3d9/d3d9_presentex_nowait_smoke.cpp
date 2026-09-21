#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(
    UINT,IDirect3D9Ex**);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_PRESENTEX_NOWAIT_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR PresentEx NoWait",
        WS_OVERLAPPEDWINDOW,
        0,0,320,180,0,0,inst,0);
}

int main()
{
    HMODULE d3d9=LoadLibraryW(L"d3d9.dll");
    if(!d3d9) return 2;

    PFN_Direct3DCreate9Ex create9Ex=
        (PFN_Direct3DCreate9Ex)GetProcAddress(
            d3d9,"Direct3DCreate9Ex");
    if(!create9Ex)
    {
        std::printf("PRESENTEX_AVAILABLE=0\n");
        return 3;
    }

    // Also determine whether a regular Direct3DCreate9/CreateDevice object
    // can be upgraded through QueryInterface. If that succeeds, PTAR can use
    // PresentEx without changing the device construction contract seen by the
    // game.
    IDirect3D9* regular=Direct3DCreate9(D3D_SDK_VERSION);
    HWND qiHwnd=MakeWindow();
    IDirect3DDevice9* regularDev=0;
    HRESULT qiCreate=E_FAIL;
    HRESULT qiExHr=E_NOINTERFACE;
    IDirect3DDevice9Ex* regularAsEx=0;

    if(regular && qiHwnd)
    {
        D3DPRESENT_PARAMETERS qpp={};
        qpp.BackBufferWidth=320;
        qpp.BackBufferHeight=180;
        qpp.BackBufferFormat=D3DFMT_UNKNOWN;
        qpp.BackBufferCount=1;
        qpp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        qpp.hDeviceWindow=qiHwnd;
        qpp.Windowed=TRUE;
        qpp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

        qiCreate=regular->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            qiHwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|
            D3DCREATE_MULTITHREADED,
            &qpp,&regularDev);

        if(SUCCEEDED(qiCreate) && regularDev)
            qiExHr=regularDev->QueryInterface(
                __uuidof(IDirect3DDevice9Ex),
                (void**)&regularAsEx);
    }

    std::printf(
        "REGULAR_DEVICE_QI_EX_HR=0x%08lX QI_EX=%d\n",
        (unsigned long)qiExHr,
        regularAsEx?1:0);

    if(regularAsEx) regularAsEx->Release();
    if(regularDev) regularDev->Release();
    if(qiHwnd) DestroyWindow(qiHwnd);
    if(regular) regular->Release();

    IDirect3D9Ex* d3d=0;
    HRESULT hr=create9Ex(D3D_SDK_VERSION,&d3d);
    if(FAILED(hr)||!d3d)
        return 4;

    HWND hwnd=MakeWindow();
    if(!hwnd)
        return 5;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=320;
    pp.BackBufferHeight=180;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

    IDirect3DDevice9Ex* dev=0;
    hr=d3d->CreateDeviceEx(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED,
        &pp,0,&dev);

    if(FAILED(hr))
    {
        hr=d3d->CreateDeviceEx(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_REF,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|
            D3DCREATE_MULTITHREADED,
            &pp,0,&dev);
    }

    if(FAILED(hr)||!dev)
    {
        std::printf(
            "PRESENTEX_CREATE_FAIL=0x%08lX\n",
            (unsigned long)hr);
        return 6;
    }

    LARGE_INTEGER freq={};
    LARGE_INTEGER t0={};
    LARGE_INTEGER t1={};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    unsigned long ok=0;
    unsigned long busy=0;
    unsigned long other=0;

    for(int i=0;i<60;++i)
    {
        dev->Clear(
            0,0,D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(i*3,i*5,i*7),
            1.0f,0);

        HRESULT p=dev->PresentEx(
            0,0,0,0,
            D3DPRESENT_DONOTWAIT);

        if(SUCCEEDED(p))
            ++ok;
        else if(p==D3DERR_WASSTILLDRAWING)
            ++busy;
        else
            ++other;

        Sleep(5);
    }

    QueryPerformanceCounter(&t1);

    const double ms=
        (double)(t1.QuadPart-t0.QuadPart)*
        1000.0/(double)freq.QuadPart;

    std::printf("PRESENTEX_AVAILABLE=1\n");
    std::printf("PRESENTEX_60_CALLS_MS=%.3f\n",ms);
    std::printf("PRESENTEX_OK=%lu\n",ok);
    std::printf("PRESENTEX_BUSY=%lu\n",busy);
    std::printf("PRESENTEX_OTHER=%lu\n",other);

    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(d3d9);

    if(other!=0)
        return 7;
    if(ms>650.0)
        return 8;
    if(ok+busy!=60)
        return 9;

    std::printf("D3D9_PRESENTEX_NOWAIT_SMOKE=PASS\n");
    return 0;
}
