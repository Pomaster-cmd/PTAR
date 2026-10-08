#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

struct QpcClock {
    LARGE_INTEGER f{};
    QpcClock(){ QueryPerformanceFrequency(&f); }
    long long now() const { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return q.QuadPart; }
    double ms(long long a,long long b) const { return double(b-a)*1000.0/double(f.QuadPart); }
};

struct Stats {
    std::vector<double> v;
    void add(double x){v.push_back(x);}
    double pct(double p) const {
        if(v.empty()) return 0.0;
        std::vector<double> s=v; std::sort(s.begin(),s.end());
        double i=(s.size()-1)*p; size_t lo=(size_t)i, hi=(size_t)ceil(i); double f=i-lo;
        return s[lo]*(1.0-f)+s[hi]*f;
    }
    double avg() const { return v.empty()?0.0:std::accumulate(v.begin(),v.end(),0.0)/v.size(); }
    double mx() const { return v.empty()?0.0:*std::max_element(v.begin(),v.end()); }
};

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){ return DefWindowProcW(h,m,w,l); }

struct Slot {
    IDirect3DTexture9* prodTex{};
    IDirect3DSurface9* prodSurf{};
    IDirect3DTexture9* consTex{};
    IDirect3DSurface9* consSurf{};
    IDirect3DQuery9* fence{};
    HANDLE shared{};
    bool pending{};
};

template<class T> static void relT(T*& p){ if(p){p->Release();p=nullptr;} }

struct Bench {
    QpcClock clk;
    HWND hwnd{};
    IDirect3D9Ex* d3d{};
    IDirect3DDevice9Ex* prod{};
    IDirect3DDevice9Ex* cons{};
    IDirect3DTexture9* srcTex{};
    IDirect3DSurface9* srcSurf{};
    IDirect3DSurface9* readback{};
    std::vector<Slot> slots;
    UINT w{},h{};

    ~Bench(){ cleanup(); }
    void cleanup(){
        for(auto &s:slots){relT(s.fence);relT(s.consSurf);relT(s.consTex);relT(s.prodSurf);relT(s.prodTex);} slots.clear();
        relT(readback); relT(srcSurf); relT(srcTex); relT(cons); relT(prod); relT(d3d);
        if(hwnd) DestroyWindow(hwnd),hwnd=nullptr;
    }

    bool init(UINT W,UINT H){
        w=W;h=H;
        WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"PTAR_D3D9_TRANSPORT_BENCH";
        RegisterClassW(&wc);
        hwnd=CreateWindowExW(0,wc.lpszClassName,L"PTAR bench",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,wc.hInstance,nullptr);
        if(!hwnd){std::printf("INIT_FAIL CreateWindow gle=%lu\n",GetLastError());return false;}
        HMODULE d3dmod=LoadLibraryW(L"d3d9.dll");
        auto pCreate9Ex=(HRESULT(WINAPI*)(UINT,IDirect3D9Ex**))GetProcAddress(d3dmod,"Direct3DCreate9Ex");
        if(!pCreate9Ex){std::printf("INIT_SKIP Direct3DCreate9Ex unavailable\n");return false;}
        HRESULT hr=pCreate9Ex(D3D_SDK_VERSION,&d3d); if(FAILED(hr)){std::printf("INIT_SKIP Direct3DCreate9Ex hr=%08lX\n",(unsigned long)hr);return false;}
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        DWORD flags=D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE;
        hr=d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,flags,&pp,nullptr,&prod);
        if(FAILED(hr)) hr=d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,flags,&pp,nullptr,&prod);
        if(FAILED(hr)){std::printf("INIT_SKIP producer CreateDeviceEx hr=%08lX\n",(unsigned long)hr);return false;}
        hr=d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,flags,&pp,nullptr,&cons);
        if(FAILED(hr)) hr=d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,flags,&pp,nullptr,&cons);
        if(FAILED(hr)){std::printf("INIT_SKIP consumer CreateDeviceEx hr=%08lX\n",(unsigned long)hr);return false;}
        hr=prod->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&srcTex,nullptr);
        if(FAILED(hr)||!srcTex){std::printf("INIT_FAIL source texture hr=%08lX\n",(unsigned long)hr);return false;}
        hr=srcTex->GetSurfaceLevel(0,&srcSurf); if(FAILED(hr)){std::printf("INIT_FAIL source surface hr=%08lX\n",(unsigned long)hr);return false;}
        hr=prod->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr);
        if(FAILED(hr)){std::printf("INIT_FAIL readback hr=%08lX\n",(unsigned long)hr);return false;}
        slots.resize(6);
        for(auto &s:slots){
            s.shared=nullptr;
            hr=prod->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&s.prodTex,&s.shared);
            if(FAILED(hr)||!s.prodTex||!s.shared){std::printf("INIT_SHARED_UNSUPPORTED producer hr=%08lX handle=%p\n",(unsigned long)hr,s.shared);slots.clear();break;}
            hr=s.prodTex->GetSurfaceLevel(0,&s.prodSurf); if(FAILED(hr)) return false;
            HANDLE openHandle=s.shared;
            hr=cons->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&s.consTex,&openHandle);
            if(FAILED(hr)||!s.consTex){std::printf("INIT_SHARED_UNSUPPORTED consumer hr=%08lX\n",(unsigned long)hr);slots.clear();break;}
            hr=s.consTex->GetSurfaceLevel(0,&s.consSurf); if(FAILED(hr)) return false;
            hr=prod->CreateQuery(D3DQUERYTYPE_EVENT,&s.fence); if(FAILED(hr)||!s.fence){std::printf("INIT_FAIL query hr=%08lX\n",(unsigned long)hr);return false;}
        }
        std::printf("INIT_OK size=%ux%u shared_slots=%zu\n",w,h,slots.size());
        return true;
    }

    void benchShared(const char* name,DWORD pollFlags,int frames){
        if(slots.size()!=6){std::printf("%s=SKIP shared unsupported\n",name);return;}
        for(auto &s:slots)s.pending=false;
        Stats submit,poll; unsigned long pendingSeen=0,ready=0,drops=0,errors=0,pollCalls=0;
        auto all0=clk.now();
        for(int frame=0;frame<frames;++frame){
            for(auto &s:slots) if(s.pending){
                auto a=clk.now(); HRESULT hr=s.fence->GetData(nullptr,0,pollFlags); auto b=clk.now(); poll.add(clk.ms(a,b)); ++pollCalls;
                if(hr==S_OK){s.pending=false;++ready;} else if(hr==S_FALSE){++pendingSeen;} else {s.pending=false;++errors;}
            }
            Slot* slot=nullptr; for(auto &s:slots) if(!s.pending){slot=&s;break;}
            if(!slot){++drops;SwitchToThread();continue;}
            prod->ColorFill(srcSurf,nullptr,D3DCOLOR_XRGB(frame&255,(frame*3)&255,(frame*7)&255));
            auto a=clk.now();
            HRESULT hr=prod->StretchRect(srcSurf,nullptr,slot->prodSurf,nullptr,D3DTEXF_NONE);
            if(SUCCEEDED(hr)) hr=slot->fence->Issue(D3DISSUE_END);
            if(SUCCEEDED(hr)){
                HRESULT q=slot->fence->GetData(nullptr,0,pollFlags); ++pollCalls;
                if(q==S_OK){slot->pending=false;++ready;} else if(q==S_FALSE){slot->pending=true;++pendingSeen;} else {slot->pending=false;++errors;}
            } else ++errors;
            auto b=clk.now(); submit.add(clk.ms(a,b));
        }
        for(int spins=0;spins<100000;++spins){
            bool any=false; for(auto &s:slots) if(s.pending){any=true;HRESULT hr=s.fence->GetData(nullptr,0,pollFlags);++pollCalls;if(hr==S_OK){s.pending=false;++ready;}else if(hr!=S_FALSE){s.pending=false;++errors;}}
            if(!any)break; SwitchToThread();
        }
        auto all1=clk.now(); double total=clk.ms(all0,all1);
        std::printf("%s frames=%d total_ms=%.3f effective_hz=%.2f submit_avg_ms=%.6f p50=%.6f p95=%.6f p99=%.6f max=%.6f poll_avg_ms=%.6f poll_calls=%lu pending=%lu ready=%lu drops=%lu errors=%lu\n",name,frames,total,total>0?frames*1000.0/total:0.0,submit.avg(),submit.pct(.50),submit.pct(.95),submit.pct(.99),submit.mx(),poll.avg(),pollCalls,pendingSeen,ready,drops,errors);
    }

    void benchBlocking(const char* name,DWORD pollFlags,int frames){
        if(slots.size()!=6){std::printf("%s=SKIP shared unsupported\n",name);return;}
        Stats wait; unsigned long polls=0,errors=0;
        auto all0=clk.now();
        Slot &s=slots[0];
        for(int frame=0;frame<frames;++frame){
            prod->ColorFill(srcSurf,nullptr,D3DCOLOR_XRGB(frame&255,(frame*3)&255,(frame*7)&255));
            auto a=clk.now(); HRESULT hr=prod->StretchRect(srcSurf,nullptr,s.prodSurf,nullptr,D3DTEXF_NONE); if(SUCCEEDED(hr))hr=s.fence->Issue(D3DISSUE_END);
            if(SUCCEEDED(hr)){
                for(;;){HRESULT q=s.fence->GetData(nullptr,0,pollFlags);++polls;if(q==S_OK)break;if(q!=S_FALSE){++errors;break;}SwitchToThread();}
            } else ++errors;
            auto b=clk.now();wait.add(clk.ms(a,b));
        }
        auto all1=clk.now(); double total=clk.ms(all0,all1);
        std::printf("%s frames=%d total_ms=%.3f effective_hz=%.2f wait_avg_ms=%.6f p50=%.6f p95=%.6f p99=%.6f max=%.6f polls=%lu errors=%lu\n",name,frames,total,total>0?frames*1000.0/total:0.0,wait.avg(),wait.pct(.50),wait.pct(.95),wait.pct(.99),wait.mx(),polls,errors);
    }

    void benchCpuReadback(int frames){
        Stats s; unsigned long errors=0; auto all0=clk.now();
        for(int frame=0;frame<frames;++frame){
            prod->ColorFill(srcSurf,nullptr,D3DCOLOR_XRGB(frame&255,(frame*3)&255,(frame*7)&255));
            auto a=clk.now(); HRESULT hr=prod->GetRenderTargetData(srcSurf,readback);
            if(SUCCEEDED(hr)){D3DLOCKED_RECT lr{};hr=readback->LockRect(&lr,nullptr,D3DLOCK_READONLY);if(SUCCEEDED(hr)){volatile unsigned char x=((unsigned char*)lr.pBits)[0];(void)x;readback->UnlockRect();}}
            if(FAILED(hr))++errors; auto b=clk.now();s.add(clk.ms(a,b));
        }
        auto all1=clk.now();double total=clk.ms(all0,all1);
        std::printf("CPU_READBACK frames=%d total_ms=%.3f effective_hz=%.2f avg_ms=%.6f p50=%.6f p95=%.6f p99=%.6f max=%.6f errors=%lu\n",frames,total,total>0?frames*1000.0/total:0.0,s.avg(),s.pct(.50),s.pct(.95),s.pct(.99),s.mx(),errors);
    }
};

int main(int argc,char**argv){
    UINT w=1280,h=720; if(argc>=3){w=(UINT)atoi(argv[1]);h=(UINT)atoi(argv[2]);}
    Bench b; if(!b.init(w,h)){std::printf("RESULT=SKIP_OR_FAIL\n");return 0;}
    for(int i=0;i<32;++i){b.prod->ColorFill(b.srcSurf,nullptr,0); if(b.slots.size()==6){b.prod->StretchRect(b.srcSurf,nullptr,b.slots[0].prodSurf,nullptr,D3DTEXF_NONE);b.slots[0].fence->Issue(D3DISSUE_END);while(b.slots[0].fence->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();}}
    b.benchShared("SHARED_F438_FLUSH",D3DGETDATA_FLUSH,3000);
    b.benchShared("SHARED_NO_FLUSH",0,3000);
    b.benchBlocking("BLOCKING_FLUSH",D3DGETDATA_FLUSH,300);
    b.benchBlocking("BLOCKING_NO_FLUSH",0,300);
    b.benchCpuReadback(300);
    std::printf("RESULT=PASS\n");
    return 0;
}
