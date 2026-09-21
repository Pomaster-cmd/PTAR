#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT,IDirect3D9Ex**);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_D3D9EX_MANAGED_PROBE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR D3D9Ex managed probe",
        WS_OVERLAPPEDWINDOW,0,0,320,180,0,0,inst,0);
}

int main()
{
    HMODULE dll=LoadLibraryW(L"d3d9.dll");
    if(!dll) return 2;
    PFN_Direct3DCreate9Ex create9Ex=
        (PFN_Direct3DCreate9Ex)GetProcAddress(dll,"Direct3DCreate9Ex");
    if(!create9Ex) return 3;

    IDirect3D9Ex* d3d=0;
    HRESULT hr=create9Ex(D3D_SDK_VERSION,&d3d);
    if(FAILED(hr)||!d3d) return 4;

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
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex* dev=0;
    hr=d3d->CreateDeviceEx(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
        &pp,0,&dev);
    if(FAILED(hr))
    {
        hr=d3d->CreateDeviceEx(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
            &pp,0,&dev);
    }
    if(FAILED(hr)||!dev) return 6;

    IDirect3DTexture9* tex=0;
    HRESULT managed=dev->CreateTexture(
        64,64,1,0,D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,&tex,0);
    std::printf(
        "D3D9EX_MANAGED_TEXTURE_HR=0x%08lX TEX=%p\n",
        (unsigned long)managed,tex);
    if(tex) tex->Release();

    IDirect3DVertexBuffer9* vb=0;
    HRESULT managedVb=dev->CreateVertexBuffer(
        4096,0,0,D3DPOOL_MANAGED,&vb,0);
    std::printf(
        "D3D9EX_MANAGED_VB_HR=0x%08lX VB=%p\n",
        (unsigned long)managedVb,vb);
    if(vb) vb->Release();

    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(dll);

    if(SUCCEEDED(managed) || SUCCEEDED(managedVb))
        std::printf("D3D9EX_MANAGED_RESOURCES=SUPPORTED\n");
    else
        std::printf("D3D9EX_MANAGED_RESOURCES=UNSUPPORTED\n");

    // Probe only; unsupported is a valid architectural finding.
    return 0;
}
