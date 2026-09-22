#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Create9)(UINT);

static HWND CreateSmokeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    const wchar_t* cls=L"PTAR_ISOLATED_FG_PROXY_SMOKE_WINDOW";

    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=cls;
    RegisterClassW(&wc);

    HWND hwnd=CreateWindowExW(
        0,cls,L"PTAR isolated FG proxy smoke",
        WS_OVERLAPPEDWINDOW,
        0,0,640,360,
        0,0,inst,0);

    if(hwnd)
    {
        ShowWindow(hwnd,SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
    return hwnd;
}

static HRESULT DrawOne(IDirect3DDevice9* dev,int ordinal)
{
    if(!dev)
        return D3DERR_INVALIDCALL;

    HRESULT hr=dev->Clear(
        0,0,D3DCLEAR_TARGET,
        D3DCOLOR_XRGB(
            (ordinal*37)&255,
            (ordinal*67)&255,
            (ordinal*97)&255),
        1.0f,0);
    if(FAILED(hr))
        return hr;

    return dev->Present(0,0,0,0);
}

static bool SetKey(WORD vk,bool down)
{
    INPUT in={};
    in.type=INPUT_KEYBOARD;
    in.ki.wVk=vk;
    in.ki.dwFlags=down?0:KEYEVENTF_KEYUP;
    return SendInput(1,&in,sizeof(in))==1;
}

static bool ToggleFgThroughProductionHotkey(
    IDirect3DDevice9* dev,
    int ordinal)
{
    if(!SetKey(VK_CONTROL,true))
        return false;
    if(!SetKey(VK_F6,true))
    {
        SetKey(VK_CONTROL,false);
        return false;
    }

    Sleep(20);

    const bool ctrlDown=
        (GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
    const bool f6Down=
        (GetAsyncKeyState(VK_F6)&0x8000)!=0;

    std::printf(
        "HOTKEY_SYNTH ctrl=%d f6=%d\n",
        ctrlDown?1:0,
        f6Down?1:0);

    HRESULT hr=DrawOne(dev,ordinal);

    SetKey(VK_F6,false);
    SetKey(VK_CONTROL,false);
    Sleep(25);

    // One released-key frame resets the edge detector exactly as a user key
    // release would. This also becomes the first governed source frame after
    // the mode transition.
    HRESULT settleHr=DrawOne(dev,ordinal+1);

    return
        SUCCEEDED(hr) &&
        SUCCEEDED(settleHr);
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
    pp.BackBufferHeight=360;
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
        "FG_PROXY_CREATE hr=0x%08lX dev=%p\n",
        (unsigned long)hr,dev);

    if(FAILED(hr)||!dev)
        return 6;

    // Warm up the isolated presenter in FG-OFF / REAL_ONLY mode.
    for(int i=0;i<4;++i)
    {
        hr=DrawOne(dev,i);
        if(FAILED(hr))
        {
            std::printf(
                "FG_PROXY_WARMUP_FAIL frame=%d hr=0x%08lX\n",
                i,(unsigned long)hr);
            return 10;
        }
    }

    // Exercise the exact user contract rather than a private test switch:
    // CTRL+F6 must transition FG OFF -> ON inside PtHudUpdateInput().
    if(!ToggleFgThroughProductionHotkey(dev,100))
    {
        std::printf("FAIL production CTRL+F6 hotkey synthesis\n");
        return 11;
    }

    LARGE_INTEGER freq={};
    LARGE_INTEGER t0={};
    LARGE_INTEGER t1={};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    const int fgOnSourceFrames=16;
    for(int i=0;i<fgOnSourceFrames;++i)
    {
        hr=DrawOne(dev,200+i);
        if(FAILED(hr))
        {
            std::printf(
                "FG_PROXY_FGON_PRESENT_FAIL frame=%d hr=0x%08lX\n",
                i,(unsigned long)hr);
            return 12;
        }
    }

    QueryPerformanceCounter(&t1);
    const double ms=
        (double)(t1.QuadPart-t0.QuadPart)*
        1000.0/(double)freq.QuadPart;

    std::printf(
        "FG_PROXY_%d_REAL_FGON_MS=%.3f\n",
        fgOnSourceFrames,ms);

    // At target REAL30, 16 source frames should take about 0.53 s. Keep this
    // loose enough for virtualized CI while still catching a return to the
    // old source-thread-generated two-Present path.
    if(ms<300.0 || ms>1200.0)
    {
        std::printf(
            "FAIL FG-on source governor/presenter isolation timing ms=%.3f\n",
            ms);
        return 13;
    }

    // Drain GENERATED -> REAL pairs before toggling back off.
    Sleep(900);

    if(!ToggleFgThroughProductionHotkey(dev,400))
    {
        std::printf("FAIL production CTRL+F6 hotkey OFF synthesis\n");
        return 14;
    }

    Sleep(300);

    // Reset releases the current presenter and therefore emits the complete
    // presented_real/presented_generated counters into the runtime log.
    pp.BackBufferWidth=640;
    pp.BackBufferHeight=360;
    hr=dev->Reset(&pp);

    std::printf(
        "FG_PROXY_FLUSH_RESET hr=0x%08lX\n",
        (unsigned long)hr);

    if(FAILED(hr))
        return 15;

    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(proxy);

    std::printf("D3D9_ISOLATED_FG_PROXY_SMOKE=PASS\n");
    return 0;
}
