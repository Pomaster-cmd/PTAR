#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

typedef IDirect3D9* (WINAPI *PFN_Create9)(UINT);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_EX_MANAGED_PROXY_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR Ex managed proxy smoke",
        WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,inst,0);
}

static bool FillTexture(IDirect3DTexture9* t,DWORD value)
{
    if(!t) return false;
    const UINT levels=t->GetLevelCount();
    for(UINT level=0;level<levels;++level)
    {
        D3DLOCKED_RECT lr={};
        HRESULT hr=t->LockRect(level,&lr,0,0);
        if(FAILED(hr))
        {
            std::printf(
                "PROXY_MANAGED_TEX_LOCK_FAIL level=%u hr=0x%08lX\n",
                level,(unsigned long)hr);
            return false;
        }
        D3DSURFACE_DESC d={};
        t->GetLevelDesc(level,&d);
        for(UINT y=0;y<d.Height;++y)
        {
            DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
            for(UINT x=0;x<d.Width;++x)
                row[x]=value+level;
        }
        t->UnlockRect(level);
    }
    return true;
}

int main()
{
    HMODULE proxy=LoadLibraryW(L"d3d9.dll");
    if(!proxy) return 2;

    PFN_Create9 create9=
        (PFN_Create9)GetProcAddress(proxy,"Direct3DCreate9");
    if(!create9) return 3;

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d) return 4;

    IDirect3D9Ex* factoryEx=0;
    HRESULT fqi=d3d->QueryInterface(
        __uuidof(IDirect3D9Ex),(void**)&factoryEx);
    std::printf(
        "PROXY_FACTORY_QI_EX=0x%08lX ptr=%p\n",
        (unsigned long)fqi,factoryEx);
    if(factoryEx) factoryEx->Release();
    if(FAILED(fqi)) return 5;

    HWND hwnd=MakeWindow();
    if(!hwnd) return 6;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=640;
    pp.BackBufferHeight=360;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.EnableAutoDepthStencil=FALSE;
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

    std::printf(
        "PROXY_DEVICE_CREATE=0x%08lX ptr=%p\n",
        (unsigned long)hr,dev);
    if(FAILED(hr)||!dev) return 7;

    IDirect3DDevice9Ex* devEx=0;
    HRESULT dqi=dev->QueryInterface(
        __uuidof(IDirect3DDevice9Ex),(void**)&devEx);
    std::printf(
        "PROXY_DEVICE_QI_EX=0x%08lX ptr=%p\n",
        (unsigned long)dqi,devEx);
    if(devEx) devEx->Release();
    if(FAILED(dqi)) return 8;

    IDirect3DTexture9* tex=0;
    IDirect3DCubeTexture9* cube=0;
    IDirect3DVolumeTexture9* volume=0;
    IDirect3DVertexBuffer9* vb=0;
    IDirect3DIndexBuffer9* ib=0;

    HRESULT th=dev->CreateTexture(
        128,128,4,0,
        D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex,0);
    HRESULT ch=dev->CreateCubeTexture(
        64,3,0,
        D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube,0);
    HRESULT vh=dev->CreateVolumeTexture(
        32,32,8,3,0,
        D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&volume,0);
    HRESULT vbh=dev->CreateVertexBuffer(
        4096,D3DUSAGE_WRITEONLY,0,
        D3DPOOL_MANAGED,&vb,0);
    HRESULT ibh=dev->CreateIndexBuffer(
        4096,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,
        D3DPOOL_MANAGED,&ib,0);

    std::printf(
        "PROXY_MANAGED_CREATE tex=0x%08lX cube=0x%08lX volume=0x%08lX vb=0x%08lX ib=0x%08lX\n",
        (unsigned long)th,(unsigned long)ch,(unsigned long)vh,
        (unsigned long)vbh,(unsigned long)ibh);

    if(FAILED(th)||FAILED(ch)||FAILED(vh)||FAILED(vbh)||FAILED(ibh))
        return 9;

    if(!FillTexture(tex,0xFF123456u))
        return 10;

    void* vp=0;
    hr=vb->Lock(0,4096,&vp,0);
    std::printf(
        "PROXY_MANAGED_VB_LOCK=0x%08lX ptr=%p\n",
        (unsigned long)hr,vp);
    if(FAILED(hr)||!vp) return 11;
    std::memset(vp,0x5A,4096);
    vb->Unlock();

    void* ip=0;
    hr=ib->Lock(0,4096,&ip,0);
    std::printf(
        "PROXY_MANAGED_IB_LOCK=0x%08lX ptr=%p\n",
        (unsigned long)hr,ip);
    if(FAILED(hr)||!ip) return 12;
    std::memset(ip,0xA5,4096);
    ib->Unlock();

    HRESULT bindT=dev->SetTexture(0,tex);
    HRESULT bindV=dev->SetStreamSource(0,vb,0,16);
    HRESULT bindI=dev->SetIndices(ib);
    std::printf(
        "PROXY_MANAGED_BIND tex=0x%08lX vb=0x%08lX ib=0x%08lX\n",
        (unsigned long)bindT,
        (unsigned long)bindV,
        (unsigned long)bindI);
    if(FAILED(bindT)||FAILED(bindV)||FAILED(bindI))
        return 13;

    hr=dev->EvictManagedResources();
    std::printf(
        "PROXY_MANAGED_EVICT=0x%08lX\n",
        (unsigned long)hr);
    if(FAILED(hr)) return 14;

    // Managed-equivalent resources must remain usable across the base Reset
    // contract exposed to legacy D3D9 games.
    pp.BackBufferWidth=800;
    pp.BackBufferHeight=450;
    hr=dev->Reset(&pp);
    std::printf(
        "PROXY_MANAGED_RESET=0x%08lX\n",
        (unsigned long)hr);
    if(FAILED(hr)) return 15;

    if(!FillTexture(tex,0xFF654321u))
        return 16;

    vp=0;
    hr=vb->Lock(0,4096,&vp,0);
    std::printf(
        "PROXY_MANAGED_POST_RESET_VB_LOCK=0x%08lX ptr=%p\n",
        (unsigned long)hr,vp);
    if(FAILED(hr)||!vp) return 17;
    vb->Unlock();

    ip=0;
    hr=ib->Lock(0,4096,&ip,0);
    std::printf(
        "PROXY_MANAGED_POST_RESET_IB_LOCK=0x%08lX ptr=%p\n",
        (unsigned long)hr,ip);
    if(FAILED(hr)||!ip) return 18;
    ib->Unlock();

    ib->Release();
    vb->Release();
    volume->Release();
    cube->Release();
    tex->Release();

    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(proxy);

    std::printf("D3D9EX_MANAGED_PROXY_SMOKE=PASS\n");
    return 0;
}
