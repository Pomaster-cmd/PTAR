#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT,IDirect3D9Ex**);

static HWND MakeWindow(const wchar_t* cls,int x)
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=cls;
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,cls,cls,WS_OVERLAPPEDWINDOW,
        x,0,320,180,0,0,inst,0);
}

static bool TryTexture(
    IDirect3DDevice9Ex* exDev,
    IDirect3DDevice9* regularDev,
    D3DFORMAT fmt)
{
    HANDLE shared=0;
    IDirect3DTexture9* exTex=0;

    HRESULT hr=exDev->CreateTexture(
        320,180,1,D3DUSAGE_RENDERTARGET,
        fmt,D3DPOOL_DEFAULT,&exTex,&shared);

    std::printf(
        "EX_CREATE_TEXTURE fmt=%u hr=0x%08lX handle=%p tex=%p\n",
        (unsigned)fmt,(unsigned long)hr,shared,exTex);

    if(FAILED(hr)||!exTex||!shared)
    {
        if(exTex) exTex->Release();
        return false;
    }

    HANDLE open=shared;
    IDirect3DTexture9* regularTex=0;
    hr=regularDev->CreateTexture(
        320,180,1,D3DUSAGE_RENDERTARGET,
        fmt,D3DPOOL_DEFAULT,&regularTex,&open);

    std::printf(
        "REGULAR_OPEN_TEXTURE fmt=%u hr=0x%08lX handle=%p tex=%p\n",
        (unsigned)fmt,(unsigned long)hr,open,regularTex);

    bool ok=SUCCEEDED(hr)&&regularTex!=0;

    if(ok)
    {
        IDirect3DSurface9* rs=0;
        IDirect3DSurface9* xs=0;
        regularTex->GetSurfaceLevel(0,&rs);
        exTex->GetSurfaceLevel(0,&xs);

        if(rs&&xs)
        {
            hr=regularDev->SetRenderTarget(0,rs);
            if(SUCCEEDED(hr))
                hr=regularDev->Clear(
                    0,0,D3DCLEAR_TARGET,
                    D3DCOLOR_XRGB(31,117,229),
                    1.0f,0);

            IDirect3DQuery9* q=0;
            if(SUCCEEDED(hr))
                hr=regularDev->CreateQuery(D3DQUERYTYPE_EVENT,&q);
            if(SUCCEEDED(hr)&&q)
            {
                q->Issue(D3DISSUE_END);
                q->GetData(0,0,D3DGETDATA_FLUSH);
                for(int i=0;i<500;++i)
                {
                    hr=q->GetData(0,0,0);
                    if(hr==S_OK) break;
                    if(hr!=S_FALSE) break;
                    Sleep(1);
                }
                q->Release();
            }

            IDirect3DSurface9* back=0;
            if(SUCCEEDED(hr))
                hr=exDev->GetBackBuffer(
                    0,0,D3DBACKBUFFER_TYPE_MONO,&back);
            if(SUCCEEDED(hr)&&back)
            {
                hr=exDev->StretchRect(
                    xs,0,back,0,D3DTEXF_NONE);
                std::printf(
                    "REVERSE_SHARED_CROSS_COPY fmt=%u hr=0x%08lX\n",
                    (unsigned)fmt,(unsigned long)hr);
                back->Release();
            }

            if(rs) rs->Release();
            if(xs) xs->Release();
        }
        else
        {
            ok=false;
        }
    }

    if(regularTex) regularTex->Release();
    exTex->Release();
    return ok&&SUCCEEDED(hr);
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

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    IDirect3D9Ex* ex=0;
    HRESULT hr=create9Ex(D3D_SDK_VERSION,&ex);
    if(!d3d||FAILED(hr)||!ex) return 4;

    HWND hwRegular=MakeWindow(L"PTAR_REVERSE_SHARED_REGULAR",0);
    HWND hwEx=MakeWindow(L"PTAR_REVERSE_SHARED_EX",340);
    if(!hwRegular||!hwEx) return 5;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=320;
    pp.BackBufferHeight=180;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* regularDev=0;
    pp.hDeviceWindow=hwRegular;
    hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwRegular,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
        &pp,&regularDev);
    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwRegular,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
            &pp,&regularDev);
    }
    if(FAILED(hr)||!regularDev) return 6;

    IDirect3DDevice9Ex* exDev=0;
    pp.hDeviceWindow=hwEx;
    hr=ex->CreateDeviceEx(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwEx,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
        &pp,0,&exDev);
    if(FAILED(hr))
    {
        hr=ex->CreateDeviceEx(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwEx,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
            &pp,0,&exDev);
    }
    if(FAILED(hr)||!exDev) return 7;

    const D3DFORMAT formats[]={
        D3DFMT_X8R8G8B8,
        D3DFMT_A8R8G8B8,
        D3DFMT_R5G6B5
    };

    bool any=false;
    for(int i=0;i<3;++i)
        any=TryTexture(exDev,regularDev,formats[i])||any;

    std::printf(
        "D3D9EX_TO_REGULAR_SHARED_ANY=%d\n",
        any?1:0);

    exDev->Release();
    regularDev->Release();
    DestroyWindow(hwEx);
    DestroyWindow(hwRegular);
    ex->Release();
    d3d->Release();
    FreeLibrary(dll);

    if(any)
    {
        std::printf("D3D9EX_TO_REGULAR_SHARED_RESOURCE=PASS\n");
        return 0;
    }

    std::printf("D3D9EX_TO_REGULAR_SHARED_RESOURCE=UNSUPPORTED\n");
    return 8;
}
