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
    pp2.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

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
    if(FAILED(hr)||!producer)
    {
        std::printf("REGULAR_PRODUCER_CREATE_FAIL=0x%08lX\n",(unsigned long)hr);
        return 6;
    }

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
    if(FAILED(hr)||!presenter)
    {
        std::printf("EX_PRESENTER_CREATE_FAIL=0x%08lX\n",(unsigned long)hr);
        return 7;
    }

    HANDLE shared=0;
    IDirect3DTexture9* producerTex=0;
    hr=producer->CreateTexture(
        320,180,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
        &producerTex,&shared);

    std::printf(
        "REGULAR_SHARED_CREATE_HR=0x%08lX HANDLE=%p TEX=%p\n",
        (unsigned long)hr,shared,producerTex);

    if(FAILED(hr)||!producerTex||!shared)
        return 8;

    HANDLE openHandle=shared;
    IDirect3DTexture9* presenterTex=0;
    hr=presenter->CreateTexture(
        320,180,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
        &presenterTex,&openHandle);

    std::printf(
        "EX_SHARED_OPEN_HR=0x%08lX TEX=%p\n",
        (unsigned long)hr,presenterTex);

    if(FAILED(hr)||!presenterTex)
        return 9;

    IDirect3DSurface9* ps=0;
    IDirect3DSurface9* xs=0;
    producerTex->GetSurfaceLevel(0,&ps);
    presenterTex->GetSurfaceLevel(0,&xs);
    if(!ps||!xs) return 10;

    hr=producer->SetRenderTarget(0,ps);
    if(SUCCEEDED(hr))
        hr=producer->Clear(
            0,0,D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(17,123,231),
            1.0f,0);
    if(FAILED(hr)) return 11;

    IDirect3DQuery9* q=0;
    hr=producer->CreateQuery(D3DQUERYTYPE_EVENT,&q);
    if(FAILED(hr)||!q) return 12;

    q->Issue(D3DISSUE_END);
    q->GetData(0,0,D3DGETDATA_FLUSH);

    for(int i=0;i<500;++i)
    {
        hr=q->GetData(0,0,0);
        if(hr==S_OK) break;
        if(hr!=S_FALSE) return 13;
        Sleep(1);
    }
    if(hr!=S_OK) return 14;

    IDirect3DSurface9* back=0;
    hr=presenter->GetBackBuffer(
        0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(FAILED(hr)||!back) return 15;

    hr=presenter->StretchRect(
        xs,0,back,0,D3DTEXF_NONE);
    std::printf("CROSS_DEVICE_COPY_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr)) return 16;

    // Validate the presenter device actually sees the regular-device write.
    IDirect3DSurface9* read=0;
    D3DSURFACE_DESC bd={};
    back->GetDesc(&bd);
    hr=presenter->CreateOffscreenPlainSurface(
        bd.Width,bd.Height,bd.Format,D3DPOOL_SYSTEMMEM,&read,0);
    if(FAILED(hr)||!read) return 17;
    hr=presenter->GetRenderTargetData(back,read);
    if(FAILED(hr)) return 18;

    D3DLOCKED_RECT lr={};
    hr=read->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)) return 19;

    const DWORD pixel=*(const DWORD*)lr.pBits;
    read->UnlockRect();

    const unsigned b=pixel&255u;
    const unsigned g=(pixel>>8)&255u;
    const unsigned r=(pixel>>16)&255u;

    std::printf(
        "CROSS_DEVICE_PIXEL=%u,%u,%u FORMAT=%u\n",
        r,g,b,(unsigned)bd.Format);

    const bool colorOk=
        r>=12u&&r<=22u&&
        g>=118u&&g<=128u&&
        b>=226u&&b<=236u;

    read->Release();
    back->Release();
    q->Release();
    xs->Release();
    ps->Release();
    presenterTex->Release();
    producerTex->Release();
    presenter->Release();
    producer->Release();
    DestroyWindow(hw2);
    DestroyWindow(hw1);
    ex->Release();
    regular->Release();
    FreeLibrary(dll);

    if(!colorOk)
        return 20;

    std::printf("REGULAR_D3D9_TO_D3D9EX_SHARED_TEXTURE=PASS\n");
    return 0;
}
