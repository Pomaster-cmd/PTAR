#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Create9)(UINT);

static int Exercise(IDirect3D9* d3d,int index)
{
    if(!d3d) return 10;

    const UINT adapters=d3d->GetAdapterCount();
    std::printf("OBJECT=%d PTR=%p ADAPTERS=%u\n",index,d3d,adapters);

    if(adapters>0)
    {
        D3DDISPLAYMODE mode={};
        const HRESULT hr=d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&mode);
        std::printf(
            "OBJECT=%d DISPLAY hr=0x%08lX %ux%u fmt=%u refresh=%u\n",
            index,(unsigned long)hr,mode.Width,mode.Height,
            (unsigned)mode.Format,mode.RefreshRate);
    }

    D3DADAPTER_IDENTIFIER9 ident={};
    const HRESULT idhr=d3d->GetAdapterIdentifier(
        D3DADAPTER_DEFAULT,0,&ident);
    std::printf(
        "OBJECT=%d IDENT hr=0x%08lX desc=%s\n",
        index,(unsigned long)idhr,
        SUCCEEDED(idhr)?ident.Description:"<unavailable>");

    return 0;
}

static HWND CreateSmokeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    const wchar_t* cls=L"PTAR_D3D9_SMOKE_WINDOW";

    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=cls;
    RegisterClassW(&wc);

    return CreateWindowExW(
        0,cls,L"PTAR D3D9 Smoke",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,640,480,
        0,0,inst,0);
}

static int ExerciseCreateDevice(IDirect3D9* d3d)
{
    HWND hwnd=CreateSmokeWindow();
    if(!hwnd)
    {
        std::printf("FAIL CreateSmokeWindow gle=%lu\n",(unsigned long)GetLastError());
        return 60;
    }

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
        &pp,
        &dev);

    std::printf(
        "CREATEDEVICE_HAL hr=0x%08lX dev=%p\n",
        (unsigned long)hr,dev);

    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_REF,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp,
            &dev);
        std::printf(
            "CREATEDEVICE_REF hr=0x%08lX dev=%p\n",
            (unsigned long)hr,dev);
    }

    if(dev)
    {
        dev->Release();
        dev=0;
    }
    DestroyWindow(hwnd);

    // The regression being guarded is an EIP=0 call through a lost
    // g_realCreateDevice pointer. Any HRESULT return proves that call target
    // remained valid; successful device creation is not required on headless CI.
    std::printf("CREATEDEVICE_CALL_SURVIVED=PASS\n");
    return 0;
}

int main()
{
    HMODULE proxy=LoadLibraryW(L"d3d9.dll");
    if(!proxy)
    {
        std::printf("FAIL LoadLibrary gle=%lu\n",(unsigned long)GetLastError());
        return 2;
    }

    PFN_Create9 create9=(PFN_Create9)GetProcAddress(proxy,"Direct3DCreate9");
    if(!create9)
    {
        std::printf("FAIL GetProcAddress gle=%lu\n",(unsigned long)GetLastError());
        return 3;
    }

    // Sequential create/use/release cycles: matches the field trace that
    // exposed the truncated-vtable lifetime crash.
    for(int i=0;i<12;++i)
    {
        IDirect3D9* d3d=create9(D3D_SDK_VERSION);
        if(!d3d)
        {
            std::printf("FAIL create sequential i=%d\n",i);
            return 10+i;
        }
        const int rc=Exercise(d3d,i);
        if(rc) return rc;
        const ULONG refs=d3d->Release();
        std::printf("OBJECT=%d RELEASE refs=%lu\n",i,(unsigned long)refs);
    }

    // Several objects alive simultaneously, all sharing the system vtable.
    IDirect3D9* live[4]={};
    for(int i=0;i<4;++i)
    {
        live[i]=create9(D3D_SDK_VERSION);
        if(!live[i])
        {
            std::printf("FAIL create live i=%d\n",i);
            return 30+i;
        }
        if(Exercise(live[i],100+i)) return 40+i;
    }

    // Exact field regression: the shared IDirect3D9 vtable is already patched
    // by earlier objects, then CreateDevice is called through a later object.
    // The broken build zeroed g_realCreateDevice on VTABLE_PATCH_ALREADY and
    // crashed with EIP=0 here.
    {
        const int rc=ExerciseCreateDevice(live[3]);
        if(rc) return rc;
    }

    for(int i=3;i>=0;--i)
    {
        const ULONG refs=live[i]->Release();
        std::printf("LIVE=%d RELEASE refs=%lu\n",i,(unsigned long)refs);
    }

    FreeLibrary(proxy);
    std::printf("D3D9_OBJECT_LIFETIME_SMOKE=PASS\n");
    return 0;
}
