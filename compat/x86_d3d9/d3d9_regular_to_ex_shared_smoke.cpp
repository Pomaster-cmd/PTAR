#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT,IDirect3D9Ex**);

static HWND MakeWindow(const wchar_t* cls,const wchar_t* title,int x)
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=cls;
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,cls,title,WS_OVERLAPPEDWINDOW,
        x,0,320,180,0,0,inst,0);
}

static bool TrySharedTexture(
    IDirect3DDevice9* producer,
    IDirect3DDevice9Ex* presenter,
    D3DFORMAT fmt)
{
    HANDLE shared=0;
    IDirect3DTexture9* ptex=0;
    HRESULT hr=producer->CreateTexture(
        320,180,1,D3DUSAGE_RENDERTARGET,
        fmt,D3DPOOL_DEFAULT,&ptex,&shared);

    std::printf(
        "TRY_TEXTURE fmt=%u create_hr=0x%08lX handle=%p tex=%p\n",
        (unsigned)fmt,(unsigned long)hr,shared,ptex);

    if(FAILED(hr)||!ptex||!shared)
    {
        if(ptex) ptex->Release();
        return false;
    }

    HANDLE h2=shared;
    IDirect3DTexture9* xtex=0;
    hr=presenter->CreateTexture(
        320,180,1,D3DUSAGE_RENDERTARGET,
        fmt,D3DPOOL_DEFAULT,&xtex,&h2);

    std::printf(
        "TRY_TEXTURE_OPEN fmt=%u hr=0x%08lX tex=%p\n",
        (unsigned)fmt,(unsigned long)hr,xtex);

    if(xtex) xtex->Release();
    ptex->Release();
    return SUCCEEDED(hr)&&xtex!=0;
}

static bool TrySharedRenderTarget(
    IDirect3DDevice9* producer,
    IDirect3DDevice9Ex* presenter,
    D3DFORMAT fmt)
{
    HANDLE shared=0;
    IDirect3DSurface9* ps=0;
    HRESULT hr=producer->CreateRenderTarget(
        320,180,fmt,
        D3DMULTISAMPLE_NONE,0,FALSE,
        &ps,&shared);

    std::printf(
        "TRY_RT fmt=%u create_hr=0x%08lX handle=%p surf=%p\n",
        (unsigned)fmt,(unsigned long)hr,shared,ps);

    if(FAILED(hr)||!ps||!shared)
    {
        if(ps) ps->Release();
        return false;
    }

    // D3D9 has no OpenSharedResource call. For a surface handle, the only
    // matching open path is another CreateRenderTarget call with the handle.
    HANDLE h2=shared;
    IDirect3DSurface9* xs=0;
    hr=presenter->CreateRenderTarget(
        320,180,fmt,
        D3DMULTISAMPLE_NONE,0,FALSE,
        &xs,&h2);

    std::printf(
        "TRY_RT_OPEN fmt=%u hr=0x%08lX surf=%p\n",
        (unsigned)fmt,(unsigned long)hr,xs);

    if(xs) xs->Release();
    ps->Release();
    return SUCCEEDED(hr)&&xs!=0;
}

int main()
{
    HMODULE dll=LoadLibraryW(L"d3d9.dll");
    if(!dll) return 2;

    PFN_Direct3DCreate9 create9=
        (PFN_Direct3DCreate9)GetProcAddress(dll,"Direct3DCreate9");
    PFN_Direct3DCreate9Ex create9Ex=
        (PFN_Direct3DCreate9Ex)GetProcAddress(dll,"Direct3DCreate9Ex");
    if(!create9||!create9Ex) return 3;

    IDirect3D9* regular=create9(D3D_SDK_VERSION);
    IDirect3D9Ex* ex=0;
    HRESULT hr=create9Ex(D3D_SDK_VERSION,&ex);
    if(!regular||FAILED(hr)||!ex) return 4;

    HWND hw1=MakeWindow(L"PTAR_REGULAR_PRODUCER",L"regular",0);
    HWND hw2=MakeWindow(L"PTAR_EX_PRESENTER",L"ex",340);
    if(!hw1||!hw2) return 5;

    D3DPRESENT_PARAMETERS pp1={};
    pp1.BackBufferWidth=320;
    pp1.BackBufferHeight=180;
    pp1.BackBufferFormat=D3DFMT_UNKNOWN;
    pp1.BackBufferCount=1;
    pp1.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp1.hDeviceWindow=hw1;
    pp1.Windowed=TRUE;
    pp1.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    D3DPRESENT_PARAMETERS pp2=pp1;
    pp2.hDeviceWindow=hw2;

    IDirect3DDevice9* producer=0;
    hr=regular->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hw1,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
        &pp1,&producer);
    if(FAILED(hr))
    {
        hr=regular->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hw1,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
            &pp1,&producer);
    }
    if(FAILED(hr)||!producer) return 6;

    IDirect3DDevice9Ex* presenter=0;
    hr=ex->CreateDeviceEx(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hw2,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
        &pp2,0,&presenter);
    if(FAILED(hr))
    {
        hr=ex->CreateDeviceEx(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hw2,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
            &pp2,0,&presenter);
    }
    if(FAILED(hr)||!presenter) return 7;

    const D3DFORMAT formats[]={
        D3DFMT_X8R8G8B8,
        D3DFMT_A8R8G8B8,
        D3DFMT_R5G6B5
    };

    bool any=false;
    for(int i=0;i<3;++i)
    {
        any=TrySharedTexture(producer,presenter,formats[i])||any;
        any=TrySharedRenderTarget(producer,presenter,formats[i])||any;
    }

    presenter->Release();
    producer->Release();
    DestroyWindow(hw2);
    DestroyWindow(hw1);
    ex->Release();
    regular->Release();
    FreeLibrary(dll);

    std::printf(
        "REGULAR_D3D9_SHARED_ANY=%d\n",
        any?1:0);

    if(!any)
    {
        std::printf("REGULAR_D3D9_TO_D3D9EX_SHARED_RESOURCE=UNSUPPORTED\n");
        return 8;
    }

    std::printf("REGULAR_D3D9_TO_D3D9EX_SHARED_RESOURCE=PASS\n");
    return 0;
}
