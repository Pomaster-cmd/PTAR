#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "ptar_rc41_resource_tag.h"

using AttachFn=int (WINAPI*)(IDXGISwapChain*,ID3D11DeviceContext*,UINT,UINT);
using DetachFn=void (WINAPI*)();
struct RC41State {
    UINT size,active,logicalW,logicalH,physicalW,physicalH,resourceBridgeInstalled,contextInstalled,swapchainInstalled;
    uint64_t viewportMapped,scissorMapped,getDescVirtualized,resizeRemapped,primaryRefreshes,textureRemapped,textureFallbacks,rtvTagged,dsvTagged;
};
using QueryFn=int (WINAPI*)(RC41State*);

template<class T> static void safe_release(T*& p){if(p){p->Release();p=nullptr;}}
static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static bool physical_is(IDXGISwapChain* sc,UINT w,UINT h){
    ID3D11Texture2D* t=nullptr;if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))||!t)return false;
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);t->Release();return d.Width==w&&d.Height==h;
}
static bool texture_is(ID3D11Texture2D* t,UINT w,UINT h){if(!t)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);return d.Width==w&&d.Height==h;}
static bool vp_is(ID3D11DeviceContext* ctx,float x,float y,float w,float h){
    UINT n=1;D3D11_VIEWPORT v{};ctx->RSGetViewports(&n,&v);auto almost_equal=[](float a,float b){return std::fabs(a-b)<3.0e-4f;};
    return n==1&&almost_equal(v.TopLeftX,x)&&almost_equal(v.TopLeftY,y)&&almost_equal(v.Width,w)&&almost_equal(v.Height,h);
}
static bool write_result(const char* text){
    HANDLE h=CreateFileA("RC41_PROD_HOST_RESULT.txt",GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;DWORD wr=0;const DWORD n=(DWORD)lstrlenA(text);const BOOL ok=WriteFile(h,text,n,&wr,nullptr);CloseHandle(h);return ok&&wr==n;
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
        RC41State st{};st.size=sizeof(st);if(query(&st)!=0||!st.active||!st.resourceBridgeInstalled||!st.contextInstalled||!st.swapchainInstalled||st.logicalW!=1920||st.logicalH!=1080||st.physicalW!=1280||st.physicalH!=720){rc=16;break;}
        DXGI_SWAP_CHAIN_DESC logical{};if(FAILED(sc->GetDesc(&logical))||logical.BufferDesc.Width!=1920||logical.BufferDesc.Height!=1080){rc=17;break;}
        if(!physical_is(sc,1280,720)){rc=18;break;}

        D3D11_TEXTURE2D_DESC ld{};ld.Width=1920;ld.Height=1080;ld.MipLevels=1;ld.ArraySize=1;ld.Format=DXGI_FORMAT_R8G8B8A8_UNORM;ld.SampleDesc.Count=1;ld.Usage=D3D11_USAGE_DEFAULT;ld.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* logicalFamily=nullptr;ID3D11RenderTargetView* logicalFamilyRTV=nullptr;
        if(FAILED(dev->CreateTexture2D(&ld,nullptr,&logicalFamily))||!texture_is(logicalFamily,1280,720)||!ptar_rc41::resource_has_family_tag(logicalFamily)){rc=19;break;}
        if(FAILED(dev->CreateRenderTargetView(logicalFamily,nullptr,&logicalFamilyRTV))||!logicalFamilyRTV||!ptar_rc41::get_family_tag(logicalFamilyRTV)){safe_release(logicalFamily);rc=20;break;}
        ctx->OMSetRenderTargets(1,&logicalFamilyRTV,nullptr);D3D11_VIEWPORT familyVp{300,160,520,180,0,1};ctx->RSSetViewports(1,&familyVp);
        if(!vp_is(ctx,200.0f,106.666664f,346.666656f,120.0f)){safe_release(logicalFamilyRTV);safe_release(logicalFamily);rc=21;break;}
        ctx->ClearState();safe_release(logicalFamilyRTV);safe_release(logicalFamily);

        constexpr uint32_t kLoops=128;
        for(uint32_t i=0;i<kLoops;++i){
            ID3D11Texture2D* t=nullptr;ID3D11RenderTargetView* rtv=nullptr;
            if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t)))||!t){rc=22;break;}
            if(FAILED(dev->CreateRenderTargetView(t,nullptr,&rtv))||!rtv){t->Release();rc=23;break;}
            ctx->OMSetRenderTargets(1,&rtv,nullptr);
            D3D11_VIEWPORT v{300,160,520,180,0,1};ctx->RSSetViewports(1,&v);
            if(!vp_is(ctx,200.0f,106.666664f,346.666656f,120.0f)){rtv->Release();t->Release();rc=24;break;}
            ctx->ClearState();rtv->Release();t->Release();
            const UINT rw=(i&1u)?1920u:0u,rh=(i&1u)?1080u:0u;
            hr=sc->ResizeBuffers(2,rw,rh,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr)){rc=25;break;}
            if(!physical_is(sc,1280,720)){rc=26;break;}
            DXGI_SWAP_CHAIN_DESC q{};if(FAILED(sc->GetDesc(&q))||q.BufferDesc.Width!=1920||q.BufferDesc.Height!=1080){rc=27;break;}
        }
        if(rc)break;
        finalStats={};finalStats.size=sizeof(finalStats);if(query(&finalStats)!=0||finalStats.viewportMapped<129||finalStats.getDescVirtualized<129||finalStats.resizeRemapped!=128||finalStats.primaryRefreshes!=128||finalStats.textureRemapped<1||finalStats.textureFallbacks!=0||finalStats.rtvTagged<1){rc=28;break;}
        detach();
        st={};st.size=sizeof(st);if(query(&st)!=0||st.active){rc=29;break;}
        DXGI_SWAP_CHAIN_DESC physical{};if(FAILED(sc->GetDesc(&physical))||physical.BufferDesc.Width!=1280||physical.BufferDesc.Height!=720){rc=30;break;}
        ID3D11Texture2D* restored=nullptr;if(FAILED(dev->CreateTexture2D(&ld,nullptr,&restored))||!texture_is(restored,1920,1080)||ptar_rc41::resource_has_family_tag(restored)){safe_release(restored);rc=31;break;}safe_release(restored);
        char result[768]{};
        std::snprintf(result,sizeof(result),"RC41_PROD_HOST=PASS feature_level=0x%x logical=1920x1080 physical=1280x720 resource_family=PASS resizes=%u vp_mapped=%llu getdesc_virtualized=%llu resize_remapped=%llu refresh=%llu texture_remapped=%llu rtv_tagged=%llu unload_restore=PASS\r\n",
                  static_cast<unsigned>(fl),static_cast<unsigned>(kLoops),static_cast<unsigned long long>(finalStats.viewportMapped),
                  static_cast<unsigned long long>(finalStats.getDescVirtualized),static_cast<unsigned long long>(finalStats.resizeRemapped),
                  static_cast<unsigned long long>(finalStats.primaryRefreshes),static_cast<unsigned long long>(finalStats.textureRemapped),
                  static_cast<unsigned long long>(finalStats.rtvTagged));
        if(!write_result(result)){rc=32;break;}
        std::fputs(result,stdout);std::fflush(stdout);
    }while(false);
    if(detach)detach();FreeLibrary(dll);ctx->ClearState();safe_release(ctx);safe_release(dev);safe_release(sc);DestroyWindow(hwnd);UnregisterClassW(wc.lpszClassName,inst);
    if(rc){std::fprintf(stderr,"RC41_PROD_HOST=FAIL rc=%d hr=0x%x\n",rc,static_cast<unsigned>(hr));std::fflush(stderr);}
    return rc;
}
