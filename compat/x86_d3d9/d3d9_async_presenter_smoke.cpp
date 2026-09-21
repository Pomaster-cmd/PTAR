#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#pragma warning(disable:4505)
#include "ptar_diag.h"
#include "ptar_runtime_metrics.h"
#include "ptar_fg_pacer.h"
#include "ptar_async_presenter.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_ASYNC_PRESENTER_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR Async Presenter Smoke",
        WS_OVERLAPPEDWINDOW,
        0,0,320,180,
        0,0,inst,0);
}

static int SubmitSolid(
    IDirect3DDevice9* dev,
    bool generated,
    unsigned long sequence,
    D3DCOLOR color)
{
    const int slot=PtAsyncPresenterAcquireSlot(
        generated,sequence);
    if(slot<0)
        return generated?0:20;

    IDirect3DSurface9* s=
        PtAsyncPresenterSlotSurface(slot);
    if(!s)
    {
        PtAsyncPresenterCancelSlot(slot);
        return 21;
    }

    PtAsyncEnterDevice();
    HRESULT hr=dev->SetRenderTarget(0,s);
    if(SUCCEEDED(hr))
        hr=dev->Clear(
            0,0,D3DCLEAR_TARGET,
            color,1.0f,0);
    PtAsyncLeaveDevice();

    if(FAILED(hr))
    {
        PtAsyncPresenterCancelSlot(slot);
        return 22;
    }

    PtAsyncPresenterCommitSlot(slot,0);
    return 0;
}

int main()
{
    wchar_t sys[MAX_PATH]={0};
    if(!GetSystemDirectoryW(sys,MAX_PATH))
        return 2;
    wcscat_s(sys,L"\\d3d9.dll");

    HMODULE d3d9=LoadLibraryW(sys);
    if(!d3d9)
        return 3;

    PFN_Direct3DCreate9 create9=
        (PFN_Direct3DCreate9)GetProcAddress(
            d3d9,"Direct3DCreate9");
    if(!create9)
        return 4;

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d)
        return 5;

    HWND hwnd=MakeWindow();
    if(!hwnd)
        return 6;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=320;
    pp.BackBufferHeight=180;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED,
        &pp,&dev);

    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_REF,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|
            D3DCREATE_MULTITHREADED,
            &pp,&dev);
    }

    if(FAILED(hr)||!dev)
    {
        std::printf(
            "FAIL CreateDevice hr=0x%08lX\n",
            (unsigned long)hr);
        return 7;
    }

    IDirect3DSurface9* back=0;
    hr=dev->GetBackBuffer(
        0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(FAILED(hr)||!back)
        return 8;

    void** vt=*(void***)dev;
    PTAR_PFN_PRESENT realPresent=
        (PTAR_PFN_PRESENT)vt[17];

    PtFgPacerReset();

    hr=PtAsyncPresenterInitialize(
        dev,back,
        320,180,
        D3DFMT_X8R8G8B8,
        realPresent,
        60.0);

    // D3DFMT_UNKNOWN windowed backbuffers often resolve to X8R8G8B8, but use
    // the actual format if the first initialization was rejected.
    if(FAILED(hr))
    {
        D3DSURFACE_DESC desc={};
        if(SUCCEEDED(back->GetDesc(&desc)))
        {
            hr=PtAsyncPresenterInitialize(
                dev,back,
                desc.Width,desc.Height,
                desc.Format,
                realPresent,
                60.0);
        }
    }

    if(FAILED(hr)||!PtAsyncPresenterIsActive())
    {
        std::printf(
            "FAIL presenter init hr=0x%08lX\n",
            (unsigned long)hr);
        return 9;
    }

    LARGE_INTEGER freq={};
    LARGE_INTEGER t0={};
    LARGE_INTEGER t1={};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    // Source producer at ~200 FPS. This would take around one second if each
    // game Present still waited for a 60-Hz scanout. The mailbox must coalesce
    // instead of blocking the producer.
    for(unsigned long i=1;i<=60;++i)
    {
        const int rc=SubmitSolid(
            dev,false,i,
            D3DCOLOR_XRGB(
                (i*3u)&255u,
                (i*5u)&255u,
                (i*7u)&255u));
        if(rc) return rc;
        Sleep(5);
    }

    QueryPerformanceCounter(&t1);
    const double producerMs=
        (double)(t1.QuadPart-t0.QuadPart)*
        1000.0/(double)freq.QuadPart;

    std::printf(
        "ASYNC_PRODUCER_60_FRAMES_MS=%.3f\n",
        producerMs);

    if(producerMs>700.0)
    {
        std::printf("FAIL producer still scanout-blocked\n");
        return 10;
    }

    // Give presenter time to drain/coalesce the high-rate producer.
    Sleep(450);

    const unsigned long highRatePresented=
        PtAsyncPresenterPresentedReal();
    const unsigned long highRateDropped=
        PtAsyncPresenterDroppedReal();

    std::printf(
        "ASYNC_HIGH_RATE_PRESENTED_REAL=%lu\n",
        highRatePresented);
    std::printf(
        "ASYNC_HIGH_RATE_DROPPED_REAL=%lu\n",
        highRateDropped);

    if(highRatePresented<5)
        return 11;
    if(highRateDropped==0)
        return 12;

    // Low-rate source pair: verify GENERATED then REAL can both reach the
    // presenter when the output clock has headroom.
    const unsigned long gen0=
        PtAsyncPresenterPresentedGenerated();
    const unsigned long real0=
        PtAsyncPresenterPresentedReal();

    unsigned long base=1000;
    for(unsigned long i=1;i<=8;++i)
    {
        int rc=SubmitSolid(
            dev,true,base+i,
            D3DCOLOR_XRGB(20,160,220));
        if(rc) return rc;

        rc=SubmitSolid(
            dev,false,base+i,
            D3DCOLOR_XRGB(220,160,20));
        if(rc) return rc;

        Sleep(34);
    }

    Sleep(400);

    const unsigned long gen1=
        PtAsyncPresenterPresentedGenerated();
    const unsigned long real1=
        PtAsyncPresenterPresentedReal();

    std::printf(
        "ASYNC_LOW_RATE_GENERATED_DELTA=%lu\n",
        gen1-gen0);
    std::printf(
        "ASYNC_LOW_RATE_REAL_DELTA=%lu\n",
        real1-real0);

    if(gen1<=gen0)
        return 13;
    if(real1<=real0)
        return 14;

    PtAsyncPresenterReleaseResources();

    back->Release();
    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(d3d9);

    std::printf("D3D9_ASYNC_PRESENTER_SMOKE=PASS\n");
    return 0;
}
