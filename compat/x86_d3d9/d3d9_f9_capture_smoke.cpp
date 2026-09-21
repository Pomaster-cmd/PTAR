#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include "ptar_capture.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_CAPTURE_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR Capture Smoke",
        WS_OVERLAPPEDWINDOW,0,0,320,180,0,0,inst,0);
}

static bool ReadBmpDimensions(
    const wchar_t* path,
    LONG* width,
    LONG* height)
{
    if(!path || !width || !height)
        return false;

    HANDLE h=CreateFileW(
        path,GENERIC_READ,FILE_SHARE_READ,
        0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(h==INVALID_HANDLE_VALUE)
        return false;

    PTARBmpFileHeader fh={};
    PTARBmpInfoHeader ih={};
    DWORD got=0;
    bool ok=
        ReadFile(h,&fh,sizeof(fh),&got,0) &&
        got==sizeof(fh) &&
        ReadFile(h,&ih,sizeof(ih),&got,0) &&
        got==sizeof(ih);

    CloseHandle(h);
    if(!ok || fh.type!=0x4D42u || ih.bitCount!=24u)
        return false;

    *width=ih.width;
    *height=ih.height;
    return true;
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
        (PFN_Direct3DCreate9)GetProcAddress(d3d9,"Direct3DCreate9");
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

    if(FAILED(hr) || !dev)
    {
        std::printf("FAIL CreateDevice hr=0x%08lX\n",(unsigned long)hr);
        return 7;
    }

    IDirect3DSurface9* back=0;
    hr=dev->GetBackBuffer(
        0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(FAILED(hr) || !back)
        return 8;

    dev->SetRenderTarget(0,back);
    dev->Clear(
        0,0,D3DCLEAR_TARGET,
        D3DCOLOR_XRGB(21,113,207),
        1.0f,0);

    wchar_t path[MAX_PATH]={0};
    hr=PtCaptureSavePostOverlayBmp(
        dev,back,GetModuleHandleW(0),
        path,MAX_PATH);

    std::printf(
        "CAPTURE_HR=0x%08lX\n",
        (unsigned long)hr);
    wprintf(L"CAPTURE_PATH=%ls\n",path);

    if(FAILED(hr) || !path[0])
        return 9;

    LONG width=0,height=0;
    if(!ReadBmpDimensions(path,&width,&height))
        return 10;

    std::printf(
        "CAPTURE_DIMENSIONS=%ldx%ld\n",
        width,height);

    if(width!=320 || height!=180)
        return 11;

    WIN32_FILE_ATTRIBUTE_DATA fad={};
    if(!GetFileAttributesExW(
        path,GetFileExInfoStandard,&fad))
        return 12;

    if(fad.nFileSizeHigh!=0 || fad.nFileSizeLow<320u*180u)
        return 13;

    DeleteFileW(path);
    back->Release();
    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(d3d9);

    std::printf("D3D9_F9_CAPTURE_SMOKE=PASS\n");
    return 0;
}
