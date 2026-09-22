#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT,IDirect3D9Ex**);

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_EX_MANAGED_TRANSLATION_PROBE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR Ex managed translation probe",
        WS_OVERLAPPEDWINDOW,0,0,640,480,0,0,inst,0);
}

static bool FillTexture(IDirect3DTexture9* t,DWORD value)
{
    if(!t) return false;
    const UINT levels=t->GetLevelCount();
    for(UINT level=0;level<levels;++level)
    {
        D3DLOCKED_RECT lr={};
        HRESULT hr=t->LockRect(level,&lr,0,D3DLOCK_DISCARD);
        if(FAILED(hr))
        {
            std::printf("TEXTURE_LOCK_FAIL level=%u hr=0x%08lX\n",level,(unsigned long)hr);
            return false;
        }
        D3DSURFACE_DESC d={};
        t->GetLevelDesc(level,&d);
        for(UINT y=0;y<d.Height;++y)
        {
            DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
            for(UINT x=0;x<d.Width;++x) row[x]=value+level;
        }
        hr=t->UnlockRect(level);
        if(FAILED(hr)) return false;
    }
    return true;
}

static bool FillCube(IDirect3DCubeTexture9* t,DWORD value)
{
    if(!t) return false;
    for(int face=0;face<6;++face)
    {
        const UINT levels=t->GetLevelCount();
        for(UINT level=0;level<levels;++level)
        {
            D3DLOCKED_RECT lr={};
            HRESULT hr=t->LockRect(
                (D3DCUBEMAP_FACES)face,level,&lr,0,D3DLOCK_DISCARD);
            if(FAILED(hr))
            {
                std::printf("CUBE_LOCK_FAIL face=%d level=%u hr=0x%08lX\n",face,level,(unsigned long)hr);
                return false;
            }
            D3DSURFACE_DESC d={};
            t->GetLevelDesc(level,&d);
            for(UINT y=0;y<d.Height;++y)
            {
                DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
                for(UINT x=0;x<d.Width;++x) row[x]=value+face+level;
            }
            hr=t->UnlockRect((D3DCUBEMAP_FACES)face,level);
            if(FAILED(hr)) return false;
        }
    }
    return true;
}

static bool FillVolume(IDirect3DVolumeTexture9* t,BYTE value)
{
    if(!t) return false;
    const UINT levels=t->GetLevelCount();
    for(UINT level=0;level<levels;++level)
    {
        D3DLOCKED_BOX lb={};
        HRESULT hr=t->LockBox(level,&lb,0,D3DLOCK_DISCARD);
        if(FAILED(hr))
        {
            std::printf("VOLUME_LOCK_FAIL level=%u hr=0x%08lX\n",level,(unsigned long)hr);
            return false;
        }
        D3DVOLUME_DESC d={};
        t->GetLevelDesc(level,&d);
        for(UINT z=0;z<d.Depth;++z)
        {
            BYTE* slice=(BYTE*)lb.pBits+z*lb.SlicePitch;
            for(UINT y=0;y<d.Height;++y)
                std::memset(slice+y*lb.RowPitch,value+level+z,(size_t)d.Width*4u);
        }
        hr=t->UnlockBox(level);
        if(FAILED(hr)) return false;
    }
    return true;
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
    pp.BackBufferWidth=640;
    pp.BackBufferHeight=480;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.EnableAutoDepthStencil=FALSE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex* ex=0;
    hr=d3d->CreateDeviceEx(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
        &pp,0,&ex);
    if(FAILED(hr))
    {
        hr=d3d->CreateDeviceEx(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,
            &pp,0,&ex);
    }
    if(FAILED(hr)||!ex) return 6;

    IDirect3DDevice9* dev=(IDirect3DDevice9*)ex;

    IDirect3DTexture9* tex=0;
    IDirect3DCubeTexture9* cube=0;
    IDirect3DVolumeTexture9* volume=0;
    IDirect3DVertexBuffer9* vb=0;
    IDirect3DIndexBuffer9* ib=0;

    HRESULT texHr=dev->CreateTexture(
        128,128,4,D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&tex,0);
    HRESULT cubeHr=dev->CreateCubeTexture(
        64,3,D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&cube,0);
    HRESULT volHr=dev->CreateVolumeTexture(
        32,32,8,3,D3DUSAGE_DYNAMIC,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&volume,0);
    HRESULT vbHr=dev->CreateVertexBuffer(
        4096,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,
        0,D3DPOOL_DEFAULT,&vb,0);
    HRESULT ibHr=dev->CreateIndexBuffer(
        4096,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,
        D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,0);

    std::printf("TRANSLATE_TEXTURE_CREATE=0x%08lX ptr=%p\n",(unsigned long)texHr,tex);
    std::printf("TRANSLATE_CUBE_CREATE=0x%08lX ptr=%p\n",(unsigned long)cubeHr,cube);
    std::printf("TRANSLATE_VOLUME_CREATE=0x%08lX ptr=%p\n",(unsigned long)volHr,volume);
    std::printf("TRANSLATE_VB_CREATE=0x%08lX ptr=%p\n",(unsigned long)vbHr,vb);
    std::printf("TRANSLATE_IB_CREATE=0x%08lX ptr=%p\n",(unsigned long)ibHr,ib);

    if(FAILED(texHr)||FAILED(cubeHr)||FAILED(volHr)||FAILED(vbHr)||FAILED(ibHr))
        return 7;

    if(!FillTexture(tex,0xFF112233u)) return 8;
    if(!FillCube(cube,0xFF334455u)) return 9;
    if(!FillVolume(volume,0x66u)) return 10;

    void* vp=0;
    hr=vb->Lock(0,4096,&vp,D3DLOCK_DISCARD);
    std::printf("TRANSLATE_VB_LOCK=0x%08lX ptr=%p\n",(unsigned long)hr,vp);
    if(FAILED(hr)||!vp) return 11;
    std::memset(vp,0x5A,4096);
    vb->Unlock();

    void* ip=0;
    hr=ib->Lock(0,4096,&ip,D3DLOCK_DISCARD);
    std::printf("TRANSLATE_IB_LOCK=0x%08lX ptr=%p\n",(unsigned long)hr,ip);
    if(FAILED(hr)||!ip) return 12;
    std::memset(ip,0xA5,4096);
    ib->Unlock();

    // Native objects must bind through the base IDirect3DDevice9 interface.
    HRESULT bindTex=dev->SetTexture(0,tex);
    HRESULT bindVb=dev->SetStreamSource(0,vb,0,16);
    HRESULT bindIb=dev->SetIndices(ib);
    std::printf(
        "TRANSLATE_BIND tex=0x%08lX vb=0x%08lX ib=0x%08lX\n",
        (unsigned long)bindTex,(unsigned long)bindVb,(unsigned long)bindIb);
    if(FAILED(bindTex)||FAILED(bindVb)||FAILED(bindIb)) return 13;

    // Test base Reset first: this is the API seen by legacy games.
    pp.BackBufferWidth=800;
    pp.BackBufferHeight=600;
    HRESULT resetHr=dev->Reset(&pp);
    std::printf("TRANSLATE_BASE_RESET=0x%08lX\n",(unsigned long)resetHr);
    if(FAILED(resetHr)) return 14;

    // Resource COM identities should still be valid after Reset on an Ex device.
    D3DSURFACE_DESC td={};
    HRESULT descHr=tex->GetLevelDesc(0,&td);
    std::printf(
        "TRANSLATE_POST_RESET_TEXTURE_DESC=0x%08lX %ux%u\n",
        (unsigned long)descHr,td.Width,td.Height);
    if(FAILED(descHr)||td.Width!=128||td.Height!=128) return 15;

    // Re-lock/write after Reset validates the practical managed-resource use case.
    if(!FillTexture(tex,0xFF556677u)) return 16;
    if(!FillCube(cube,0xFF778899u)) return 17;
    if(!FillVolume(volume,0x99u)) return 18;

    vp=0;
    hr=vb->Lock(0,4096,&vp,D3DLOCK_DISCARD);
    std::printf("TRANSLATE_POST_RESET_VB_LOCK=0x%08lX ptr=%p\n",(unsigned long)hr,vp);
    if(FAILED(hr)||!vp) return 19;
    std::memset(vp,0x3C,4096);
    vb->Unlock();

    ip=0;
    hr=ib->Lock(0,4096,&ip,D3DLOCK_DISCARD);
    std::printf("TRANSLATE_POST_RESET_IB_LOCK=0x%08lX ptr=%p\n",(unsigned long)hr,ip);
    if(FAILED(hr)||!ip) return 20;
    std::memset(ip,0xC3,4096);
    ib->Unlock();

    // Rebind after Reset too.
    bindTex=dev->SetTexture(0,tex);
    bindVb=dev->SetStreamSource(0,vb,0,16);
    bindIb=dev->SetIndices(ib);
    std::printf(
        "TRANSLATE_POST_RESET_BIND tex=0x%08lX vb=0x%08lX ib=0x%08lX\n",
        (unsigned long)bindTex,(unsigned long)bindVb,(unsigned long)bindIb);
    if(FAILED(bindTex)||FAILED(bindVb)||FAILED(bindIb)) return 21;

    ib->Release();
    vb->Release();
    volume->Release();
    cube->Release();
    tex->Release();

    ex->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(dll);

    std::printf("D3D9EX_MANAGED_TRANSLATION_PROBE=PASS\n");
    return 0;
}
