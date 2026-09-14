#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cmath>
#include <cstdint>
#include <iostream>

using AttachFn=int (WINAPI*)(IDXGISwapChain*,ID3D11DeviceContext*,UINT,UINT);
using DetachFn=void (WINAPI*)();
struct RC41State {UINT size,active,logicalW,logicalH,physicalW,physicalH,contextInstalled,swapchainInstalled;uint64_t viewportMapped,scissorMapped,getDescVirtualized,resizeRemapped,primaryRefreshes;};
using QueryFn=int (WINAPI*)(RC41State*);

template<class T> static void safe_release(T*& p){if(p){p->Release();p=nullptr;}}
static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static bool physical_is(IDXGISwapChain* sc,UINT w,UINT h){
    ID3D11Texture2D* t=nullptr;if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))||!t)return false;
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);t->Release();return d.Width==w&&d.Height==h;
}
static bool vp_is(ID3D11DeviceContext* ctx,float x,float y,float w,float h){
    UINT n=1;D3D11_VIEWPORT v{};ctx->RSGetViewports(&n,&v);auto almost_equal=[](float a,float b){return std::fabs(a-b)<3.0e-4f;};
    return n==1&&almost_equal(v.TopLeftX,x)&&almost_equal(v.TopLeftY,y)&&almost_equal(v.Width,w)&&almost_equal(v.Height,h);
}

int main(){
    HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=wndproc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC41_PROD_HOST";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 10;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"rc41 prod host",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,inst,nullptr);if(!hwnd)return 11;
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=1280;sd.BufferDesc.Height=720;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=hwnd;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc=nullptr;ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&sc,&dev,&fl,&ctx);
    if(FAILED(hr)||!sc||!dev||!ctx)return 12;
    HMODULE dll=LoadLibraryW(L"ptar_rc41.dll");if(!dll)return 13;
    auto attach=reinterpret_cast<AttachFn>(GetProcAddress(dll,"PTAR_RC41_Attach"));
    auto detach=reinterpret_cast<DetachFn>(GetProcAddress(dll,"PTAR_RC41_Detach"));
    auto query=reinterpret_cast<QueryFn>(GetProcAddress(dll,"PTAR_RC41_Query"));
    int rc=0;RC41State finalStats{};
    do{
        if(!attach||!detach||!query){rc=14;break;}
        if(attach(sc,ctx,1920,1080)!=0){rc=15;break;}
        RC41State st{};st.size=sizeof(st);if(query(&st)!=0||!st.active||st.logicalW!=1920||st.logicalH!=1080||st.physicalW!=1280||st.physicalH!=720){rc=16;break;}
        DXGI_SWAP_CHAIN_DESC logical{};if(FAILED(sc->GetDesc(&logical))||logical.BufferDesc.Width!=1920||logical.BufferDesc.Height!=1080){rc=17;break;}
        if(!physical_is(sc,1280,720)){rc=18;break;}
        constexpr uint32_t kLoops=128;
        for(uint32_t i=0;i<kLoops;++i){
            ID3D11Texture2D* t=nullptr;ID3D11RenderTargetView* rtv=nullptr;
            if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))||!t){rc=19;break;}
            if(FAILED(dev->CreateRenderTargetView(t,nullptr,&rtv))||!rtv){t->Release();rc=20;break;}
            ctx->OMSetRenderTargets(1,&rtv,nullptr);
            D3D11_VIEWPORT v{300,160,520,180,0,1};ctx->RSSetViewports(1,&v);
            if(!vp_is(ctx,200.0f,106.666664f,346.666656f,120.0f)){rtv->Release();t->Release();rc=21;break;}
            ctx->ClearState();rtv->Release();t->Release();
            const UINT rw=(i&1u)?1920u:0u,rh=(i&1u)?1080u:0u;
            hr=sc->ResizeBuffers(2,rw,rh,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr)){rc=22;break;}
            if(!physical_is(sc,1280,720)){rc=23;break;}
            DXGI_SWAP_CHAIN_DESC q{};if(FAILED(sc->GetDesc(&q))||q.BufferDesc.Width!=1920||q.BufferDesc.Height!=1080){rc=24;break;}
        }
        if(rc)break;
        finalStats={};finalStats.size=sizeof(finalStats);if(query(&finalStats)!=0||finalStats.viewportMapped<128||finalStats.getDescVirtualized<129||finalStats.resizeRemapped!=128||finalStats.primaryRefreshes!=128){rc=25;break;}
        detach();
        st={};st.size=sizeof(st);if(query(&st)!=0||st.active){rc=26;break;}
        DXGI_SWAP_CHAIN_DESC physical{};if(FAILED(sc->GetDesc(&physical))||physical.BufferDesc.Width!=1280||physical.BufferDesc.Height!=720){rc=27;break;}
        std::cout<<"RC41_PROD_HOST=PASS feature_level=0x"<<std::hex<<static_cast<unsigned>(fl)<<std::dec
                 <<" logical=1920x1080 physical=1280x720 resizes="<<kLoops
                 <<" vp_mapped="<<finalStats.viewportMapped<<" getdesc_virtualized="<<finalStats.getDescVirtualized
                 <<" resize_remapped="<<finalStats.resizeRemapped<<" refresh="<<finalStats.primaryRefreshes
                 <<" unload_restore=PASS\n";
    }while(false);
    if(detach)detach();FreeLibrary(dll);ctx->ClearState();safe_release(ctx);safe_release(dev);safe_release(sc);DestroyWindow(hwnd);UnregisterClassW(wc.lpszClassName,inst);
    if(rc)std::cerr<<"RC41_PROD_HOST=FAIL rc="<<rc<<" hr=0x"<<std::hex<<static_cast<unsigned>(hr)<<std::dec<<"\n";
    return rc;
}
