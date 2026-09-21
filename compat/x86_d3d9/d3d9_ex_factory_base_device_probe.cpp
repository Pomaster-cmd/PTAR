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
    wc.lpszClassName=L"PTAR_EX_FACTORY_BASE_DEVICE_PROBE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR Ex factory/base device probe",
        WS_OVERLAPPEDWINDOW,0,0,320,180,0,0,inst,0);
}

int main()
{
    HMODULE dll=LoadLibraryW(L"d3d9.dll");
    if(!dll) return 2;

    PFN_Direct3DCreate9Ex create9Ex=
        (PFN_Direct3DCreate9Ex)GetProcAddress(
            dll,"Direct3DCreate9Ex");
    if(!create9Ex) return 3;

    IDirect3D9Ex* exFactory=0;
    HRESULT hr=create9Ex(D3D_SDK_VERSION,&exFactory);
    if(FAILED(hr)||!exFactory) return 4;

    IDirect3D9* baseFactory=(IDirect3D9*)exFactory;

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

    IDirect3DDevice9* baseDevice=0;
    hr=baseFactory->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED,
        &pp,&baseDevice);

    if(FAILED(hr))
    {
        hr=baseFactory->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|
            D3DCREATE_MULTITHREADED,
            &pp,&baseDevice);
    }

    std::printf(
        "EX_FACTORY_BASE_CREATEDEVICE_HR=0x%08lX DEV=%p\n",
        (unsigned long)hr,baseDevice);

    if(FAILED(hr)||!baseDevice)
        return 6;

    IDirect3DDevice9Ex* asEx=0;
    HRESULT qi=baseDevice->QueryInterface(
        __uuidof(IDirect3DDevice9Ex),
        (void**)&asEx);

    std::printf(
        "EX_FACTORY_BASE_DEVICE_QI_EX_HR=0x%08lX EX=%p\n",
        (unsigned long)qi,asEx);

    IDirect3DTexture9* managedTex=0;
    HRESULT managed=baseDevice->CreateTexture(
        64,64,1,0,D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,&managedTex,0);

    std::printf(
        "EX_FACTORY_BASE_MANAGED_HR=0x%08lX TEX=%p\n",
        (unsigned long)managed,managedTex);

    if(managedTex) managedTex->Release();

    HANDLE shared=0;
    IDirect3DTexture9* sharedTex=0;
    HRESULT sharedHr=baseDevice->CreateTexture(
        64,64,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
        &sharedTex,&shared);

    std::printf(
        "EX_FACTORY_BASE_SHARED_HR=0x%08lX HANDLE=%p TEX=%p\n",
        (unsigned long)sharedHr,shared,sharedTex);

    if(sharedTex) sharedTex->Release();
    if(asEx) asEx->Release();
    baseDevice->Release();
    DestroyWindow(hwnd);
    exFactory->Release();
    FreeLibrary(dll);

    std::printf(
        "EX_FACTORY_BASE_MANAGED=%s\n",
        SUCCEEDED(managed)?"SUPPORTED":"UNSUPPORTED");
    std::printf(
        "EX_FACTORY_BASE_SHARED=%s\n",
        (SUCCEEDED(sharedHr)&&shared)?
            "SUPPORTED":"UNSUPPORTED");

    return 0;
}
