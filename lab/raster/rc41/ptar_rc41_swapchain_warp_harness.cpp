#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "ptar_rc41_context_hooks.h"
#include "ptar_rc41_swapchain_hooks.h"

using namespace ptar_rc41;

template<class T> static void safe_release(T*& p){ if(p){ p->Release(); p=nullptr; } }
static bool nearf(float a,float b,float eps=3.0e-4f){ return std::fabs(a-b)<=eps; }

struct RefreshState {
    ContextHooks* contextHooks=nullptr;
    uint64_t callbacks=0;
    bool ok=true;
};

static void refresh_after_resize(void* user,IDXGISwapChain* swapchain) noexcept {
    auto* s=static_cast<RefreshState*>(user);
    if(!s||!s->contextHooks||!swapchain){ if(s)s->ok=false; return; }
    ID3D11Texture2D* backbuffer=nullptr;
    if(FAILED(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&backbuffer)))||!backbuffer){ s->ok=false; return; }
    D3D11_TEXTURE2D_DESC d{}; backbuffer->GetDesc(&d);
    if(d.Width!=1280||d.Height!=720||!s->contextHooks->update_primary_resource(backbuffer)) s->ok=false;
    backbuffer->Release();
    ++s->callbacks;
}

static LRESULT CALLBACK test_wndproc(HWND h,UINT m,WPARAM w,LPARAM l){
    return DefWindowProcW(h,m,w,l);
}

static bool query_viewport(ID3D11DeviceContext* ctx,float x,float y,float w,float h){
    UINT n=1;D3D11_VIEWPORT v{};ctx->RSGetViewports(&n,&v);
    return n==1&&nearf(v.TopLeftX,x)&&nearf(v.TopLeftY,y)&&nearf(v.Width,w)&&nearf(v.Height,h);
}

static bool physical_buffer_is(IDXGISwapChain* sc,UINT w,UINT h){
    ID3D11Texture2D* t=nullptr;
    if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))||!t)return false;
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);t->Release();return d.Width==w&&d.Height==h;
}

int main(){
    HINSTANCE inst=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=test_wndproc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC41_WARP_WINDOW";
    if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return 10;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"rc41",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,inst,nullptr);
    if(!hwnd)return 11;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width=1280;sd.BufferDesc.Height=720;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=hwnd;
    sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc=nullptr;ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&sc,&dev,&fl,&ctx);
    int rc=0;
    ContextHooks contextHooks;SwapchainHooks swapHooks;
    IDXGISwapChain1* sc1=nullptr;
    do{
        if(FAILED(hr)||!sc||!dev||!ctx){rc=12;break;}
        if(FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain1),reinterpret_cast<void**>(&sc1)))||!sc1){rc=13;break;}

        ID3D11Texture2D* initial=nullptr;
        if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&initial)))||!initial){rc=14;break;}
        if(!contextHooks.configure(Contract{{1920,1080},{1280,720}},initial)){initial->Release();rc=15;break;}
        initial->Release();
        if(!contextHooks.install(ctx)){rc=16;break;}

        RefreshState refresh{&contextHooks,0,true};
        HMODULE runtime=GetModuleHandleW(L"d3d11.dll");HMODULE sidecar=GetModuleHandleW(L"dxgi.dll");
        if(!runtime||!sidecar||!swapHooks.configure(Contract{{1920,1080},{1280,720}},inst,runtime,sidecar,refresh_after_resize,&refresh)){rc=17;break;}
        void** baseBefore=*reinterpret_cast<void***>(sc);
        void** sc1Before=*reinterpret_cast<void***>(sc1);
        if(!swapHooks.install(sc)){rc=18;break;}
        void** baseDuring=*reinterpret_cast<void***>(sc);
        void** sc1During=*reinterpret_cast<void***>(sc1);
        if(baseDuring==baseBefore){rc=19;break;}

        DXGI_SWAP_CHAIN_DESC logical{};if(FAILED(sc->GetDesc(&logical))||logical.BufferDesc.Width!=1920||logical.BufferDesc.Height!=1080){rc=20;break;}
        DXGI_SWAP_CHAIN_DESC physical{};if(FAILED(swapHooks.physical_desc(&physical))||physical.BufferDesc.Width!=1280||physical.BufferDesc.Height!=720){rc=21;break;}
        DXGI_SWAP_CHAIN_DESC1 logical1{};if(FAILED(sc1->GetDesc1(&logical1))||logical1.Width!=1920||logical1.Height!=1080){rc=22;break;}
        DXGI_SWAP_CHAIN_DESC1 physical1{};if(FAILED(swapHooks.physical_desc1(&physical1))||physical1.Width!=1280||physical1.Height!=720){rc=23;break;}
        if(!physical_buffer_is(sc,1280,720)){rc=24;break;}

        constexpr uint32_t kResizes=128;
        for(uint32_t i=0;i<kResizes;++i){
            ID3D11Texture2D* backbuffer=nullptr;ID3D11RenderTargetView* rtv=nullptr;
            if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&backbuffer)))||!backbuffer){rc=25;break;}
            if(FAILED(dev->CreateRenderTargetView(backbuffer,nullptr,&rtv))||!rtv){backbuffer->Release();rc=26;break;}
            ctx->OMSetRenderTargets(1,&rtv,nullptr);
            if(!contextHooks.primary_bound()){rtv->Release();backbuffer->Release();rc=27;break;}
            D3D11_VIEWPORT vp{300.0f,160.0f,520.0f,180.0f,0.0f,1.0f};ctx->RSSetViewports(1,&vp);
            if(!query_viewport(ctx,200.0f,106.666664f,346.666656f,120.0f)){rtv->Release();backbuffer->Release();rc=28;break;}
            ctx->ClearState();
            rtv->Release();backbuffer->Release();

            const UINT requestW=(i&1u)?1920u:0u;
            const UINT requestH=(i&1u)?1080u:0u;
            hr=sc->ResizeBuffers(2,requestW,requestH,DXGI_FORMAT_UNKNOWN,0);
            if(FAILED(hr)){rc=29;break;}
            if(!refresh.ok||refresh.callbacks!=uint64_t(i+1)){rc=30;break;}
            if(!physical_buffer_is(sc,1280,720)){rc=31;break;}
            DXGI_SWAP_CHAIN_DESC ld{};if(FAILED(sc->GetDesc(&ld))||ld.BufferDesc.Width!=1920||ld.BufferDesc.Height!=1080){rc=32;break;}
            DXGI_SWAP_CHAIN_DESC pd{};if(FAILED(swapHooks.physical_desc(&pd))||pd.BufferDesc.Width!=1280||pd.BufferDesc.Height!=720){rc=33;break;}
        }
        if(rc)break;

        SwapchainHookStats ss=swapHooks.stats();HookStats cs=contextHooks.stats();
        if(ss.resizeCalls!=kResizes||ss.resizeRemapped!=kResizes||ss.resizeSucceeded!=kResizes||
           ss.getDescVirtualized<kResizes+1u||ss.getDesc1Virtualized<1u||cs.primaryRefreshes!=kResizes){rc=34;break;}

        swapHooks.uninstall();
        if(swapHooks.installed()){rc=35;break;}
        if(*reinterpret_cast<void***>(sc)!=baseBefore||*reinterpret_cast<void***>(sc1)!=sc1Before){rc=36;break;}
        DXGI_SWAP_CHAIN_DESC unhooked{};if(FAILED(sc->GetDesc(&unhooked))||unhooked.BufferDesc.Width!=1280||unhooked.BufferDesc.Height!=720){rc=37;break;}
        contextHooks.uninstall();
        if(contextHooks.installed()){rc=38;break;}

        std::cout<<"RC41_SWAPCHAIN_WARP=PASS feature_level=0x"<<std::hex<<static_cast<unsigned>(fl)<<std::dec
                 <<" resizes="<<kResizes<<" getdesc_logical=1920x1080 physical=1280x720 getdesc1=PASS getbuffer=1280x720"
                 <<" resize_refresh="<<refresh.callbacks<<" classifier_no_hold=PASS context_refresh="<<cs.primaryRefreshes
                 <<" base_sc1_same="<<(sc==static_cast<IDXGISwapChain*>(sc1)?1:0)
                 <<" base_shadowed="<<(baseDuring!=baseBefore?1:0)<<" sc1_shadowed="<<(sc1During!=sc1Before?1:0)
                 <<" restore=PASS\n";
    }while(false);

    contextHooks.uninstall();swapHooks.uninstall();
    if(ctx)ctx->ClearState();safe_release(sc1);safe_release(ctx);safe_release(dev);safe_release(sc);
    DestroyWindow(hwnd);UnregisterClassW(wc.lpszClassName,inst);
    if(rc)std::cerr<<"RC41_SWAPCHAIN_WARP=FAIL rc="<<rc<<" hr=0x"<<std::hex<<static_cast<unsigned>(hr)<<std::dec<<"\n";
    return rc;
}
