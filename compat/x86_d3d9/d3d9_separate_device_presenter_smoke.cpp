#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

typedef HRESULT (WINAPI *PFN_Direct3DCreate9Ex)(UINT,IDirect3D9Ex**);

struct Slot
{
    IDirect3DTexture9* producerTex;
    IDirect3DSurface9* producerSurf;
    IDirect3DTexture9* presenterTex;
    IDirect3DSurface9* presenterSurf;
    IDirect3DQuery9* fence;
    HANDLE shared;
    volatile LONG state; // 0 free, 1 ready
    unsigned long seq;
};

struct Ctx
{
    IDirect3DDevice9Ex* producer;
    IDirect3DDevice9Ex* presenter;
    IDirect3DSurface9* presenterBack;
    HWND presenterHwnd;
    Slot slots[6];
    HANDLE stopEvent;
    HANDLE wakeEvent;
    HANDLE thread;
    LARGE_INTEGER freq;
    volatile LONG presented;
    volatile LONG errors;
};

static Ctx g={};

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

static int FindReady()
{
    int best=-1;
    unsigned long seq=0xFFFFFFFFul;
    for(int i=0;i<6;++i)
    {
        if(InterlockedCompareExchange(&g.slots[i].state,1,1)==1 &&
           g.slots[i].seq<seq)
        {
            best=i;
            seq=g.slots[i].seq;
        }
    }
    return best;
}

static DWORD WINAPI PresenterThread(LPVOID)
{
    for(;;)
    {
        HANDLE ev[2]={g.stopEvent,g.wakeEvent};
        DWORD wr=WaitForMultipleObjects(2,ev,FALSE,20);
        if(wr==WAIT_OBJECT_0)
            break;

        int idx=FindReady();
        if(idx<0)
            continue;

        Slot& s=g.slots[idx];

        // Shared D3D9Ex texture producer completion is guarded by an EVENT query.
        // Poll without D3DGETDATA_FLUSH here: producer already flushed once.
        HRESULT q=S_FALSE;
        for(int spin=0;spin<200 && q==S_FALSE;++spin)
        {
            q=s.fence->GetData(0,0,0);
            if(q==S_FALSE)
                Sleep(0);
        }
        if(q!=S_OK)
        {
            InterlockedIncrement(&g.errors);
            InterlockedExchange(&s.state,0);
            continue;
        }

        HRESULT hr=g.presenter->StretchRect(
            s.presenterSurf,0,
            g.presenterBack,0,
            D3DTEXF_NONE);
        if(SUCCEEDED(hr))
        {
            // Deliberately Sync1: the isolated presenter is allowed to block.
            // The producer must remain unaffected because it is a separate device.
            hr=g.presenter->PresentEx(0,0,g.presenterHwnd,0,0);
        }

        if(FAILED(hr))
            InterlockedIncrement(&g.errors);
        else
            InterlockedIncrement(&g.presented);

        InterlockedExchange(&s.state,0);
    }
    return 0;
}

static void ReleaseAll()
{
    if(g.thread)
    {
        SetEvent(g.stopEvent);
        WaitForSingleObject(g.thread,3000);
        CloseHandle(g.thread);
        g.thread=0;
    }
    if(g.wakeEvent){CloseHandle(g.wakeEvent);g.wakeEvent=0;}
    if(g.stopEvent){CloseHandle(g.stopEvent);g.stopEvent=0;}

    for(int i=0;i<6;++i)
    {
        if(g.slots[i].fence) g.slots[i].fence->Release();
        if(g.slots[i].presenterSurf) g.slots[i].presenterSurf->Release();
        if(g.slots[i].presenterTex) g.slots[i].presenterTex->Release();
        if(g.slots[i].producerSurf) g.slots[i].producerSurf->Release();
        if(g.slots[i].producerTex) g.slots[i].producerTex->Release();
        g.slots[i]=Slot{};
    }

    if(g.presenterBack){g.presenterBack->Release();g.presenterBack=0;}
    if(g.presenter){g.presenter->Release();g.presenter=0;}
    if(g.producer){g.producer->Release();g.producer=0;}
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

    HWND producerHwnd=MakeWindow(L"PTAR_SHARED_PRODUCER",L"producer",0);
    HWND presenterHwnd=MakeWindow(L"PTAR_SHARED_PRESENTER",L"presenter",340);
    if(!producerHwnd||!presenterHwnd) return 5;
    g.presenterHwnd=presenterHwnd;

    D3DPRESENT_PARAMETERS ppProd={};
    ppProd.BackBufferWidth=320;
    ppProd.BackBufferHeight=180;
    ppProd.BackBufferFormat=D3DFMT_UNKNOWN;
    ppProd.BackBufferCount=1;
    ppProd.SwapEffect=D3DSWAPEFFECT_DISCARD;
    ppProd.hDeviceWindow=producerHwnd;
    ppProd.Windowed=TRUE;
    ppProd.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    D3DPRESENT_PARAMETERS ppPresent=ppProd;
    ppPresent.hDeviceWindow=presenterHwnd;
    ppPresent.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

    DWORD flags=D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED;

    hr=d3d->CreateDeviceEx(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,
        producerHwnd,flags,&ppProd,0,&g.producer);
    if(FAILED(hr))
    {
        hr=d3d->CreateDeviceEx(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,
            producerHwnd,flags,&ppProd,0,&g.producer);
    }
    if(FAILED(hr)||!g.producer)
    {
        std::printf("FAIL producer device hr=0x%08lX\n",(unsigned long)hr);
        return 6;
    }

    hr=d3d->CreateDeviceEx(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,
        presenterHwnd,flags,&ppPresent,0,&g.presenter);
    if(FAILED(hr))
    {
        hr=d3d->CreateDeviceEx(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,
            presenterHwnd,flags,&ppPresent,0,&g.presenter);
    }
    if(FAILED(hr)||!g.presenter)
    {
        std::printf("FAIL presenter device hr=0x%08lX\n",(unsigned long)hr);
        return 7;
    }

    hr=g.presenter->GetBackBuffer(
        0,0,D3DBACKBUFFER_TYPE_MONO,&g.presenterBack);
    if(FAILED(hr)||!g.presenterBack) return 8;

    for(int i=0;i<6;++i)
    {
        Slot& s=g.slots[i];
        s.shared=0;

        hr=g.producer->CreateTexture(
            320,180,1,D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
            &s.producerTex,&s.shared);
        if(FAILED(hr)||!s.producerTex||!s.shared)
        {
            std::printf(
                "FAIL producer shared texture %d hr=0x%08lX handle=%p\n",
                i,(unsigned long)hr,s.shared);
            return 9;
        }

        HANDLE openHandle=s.shared;
        hr=g.presenter->CreateTexture(
            320,180,1,D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,
            &s.presenterTex,&openHandle);
        if(FAILED(hr)||!s.presenterTex)
        {
            std::printf(
                "FAIL presenter open shared texture %d hr=0x%08lX\n",
                i,(unsigned long)hr);
            return 10;
        }

        hr=s.producerTex->GetSurfaceLevel(0,&s.producerSurf);
        if(FAILED(hr)) return 11;
        hr=s.presenterTex->GetSurfaceLevel(0,&s.presenterSurf);
        if(FAILED(hr)) return 12;

        hr=g.producer->CreateQuery(D3DQUERYTYPE_EVENT,&s.fence);
        if(FAILED(hr)||!s.fence) return 13;
    }

    QueryPerformanceFrequency(&g.freq);
    g.stopEvent=CreateEventW(0,TRUE,FALSE,0);
    g.wakeEvent=CreateEventW(0,FALSE,FALSE,0);
    if(!g.stopEvent||!g.wakeEvent) return 14;

    g.thread=CreateThread(0,0,PresenterThread,0,0,0);
    if(!g.thread) return 15;

    LARGE_INTEGER t0={};
    LARGE_INTEGER t1={};
    QueryPerformanceCounter(&t0);

    unsigned long submitted=0;
    unsigned long producerDrops=0;

    for(unsigned long seq=1;seq<=60;++seq)
    {
        int idx=-1;
        for(int i=0;i<6;++i)
        {
            if(InterlockedCompareExchange(&g.slots[i].state,1,0)==0)
            {
                idx=i;
                break;
            }
        }

        if(idx<0)
        {
            ++producerDrops;
            Sleep(5);
            continue;
        }

        Slot& s=g.slots[idx];
        s.seq=seq;

        hr=g.producer->SetRenderTarget(0,s.producerSurf);
        if(SUCCEEDED(hr))
        {
            hr=g.producer->Clear(
                0,0,D3DCLEAR_TARGET,
                D3DCOLOR_XRGB(
                    (seq*3u)&255u,
                    (seq*5u)&255u,
                    (seq*7u)&255u),
                1.0f,0);
        }

        if(SUCCEEDED(hr))
            hr=s.fence->Issue(D3DISSUE_END);

        if(SUCCEEDED(hr))
        {
            // Submit producer commands but never wait for the GPU here.
            s.fence->GetData(0,0,D3DGETDATA_FLUSH);
            ++submitted;
            SetEvent(g.wakeEvent);
        }
        else
        {
            InterlockedExchange(&s.state,0);
            ++producerDrops;
        }

        Sleep(5);
    }

    QueryPerformanceCounter(&t1);

    const double producerMs=
        (double)(t1.QuadPart-t0.QuadPart)*
        1000.0/(double)g.freq.QuadPart;

    std::printf("SHARED_HANDLE_SUPPORTED=1\n");
    std::printf("SEPARATE_DEVICE_PRODUCER_MS=%.3f\n",producerMs);
    std::printf("SEPARATE_DEVICE_SUBMITTED=%lu\n",submitted);
    std::printf("SEPARATE_DEVICE_PRODUCER_DROPS=%lu\n",producerDrops);

    // The isolated presenter may still be draining at Sync1.
    Sleep(1000);

    std::printf(
        "SEPARATE_DEVICE_PRESENTED=%ld\n",
        InterlockedCompareExchange(&g.presented,0,0));
    std::printf(
        "SEPARATE_DEVICE_ERRORS=%ld\n",
        InterlockedCompareExchange(&g.errors,0,0));

    const LONG presented=InterlockedCompareExchange(&g.presented,0,0);
    const LONG errors=InterlockedCompareExchange(&g.errors,0,0);

    ReleaseAll();

    DestroyWindow(producerHwnd);
    DestroyWindow(presenterHwnd);
    d3d->Release();
    FreeLibrary(dll);

    // 60 producer iterations contain 300 ms of deliberate Sleep(5). If this
    // exceeds 650 ms, isolated presenter scanout is leaking back into producer.
    if(producerMs>650.0)
        return 16;
    if(submitted<10)
        return 17;
    if(presented<5)
        return 18;
    if(errors!=0)
        return 19;

    std::printf("D3D9_SEPARATE_DEVICE_SHARED_PRESENTER_SMOKE=PASS\n");
    return 0;
}
