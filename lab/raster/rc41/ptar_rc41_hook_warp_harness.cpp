#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "ptar_rc41_context_hooks.h"

using namespace ptar_rc41;

template<class T> static void safe_release(T*& p){ if(p){p->Release();p=nullptr;} }
static bool nearf(float a,float b,float eps=2.0e-4f){return std::fabs(a-b)<=eps;}

static HRESULT make_target(ID3D11Device* dev,UINT w,UINT h,ID3D11Texture2D** tex,ID3D11RenderTargetView** rtv){
    *tex=nullptr;*rtv=nullptr;
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    HRESULT hr=dev->CreateTexture2D(&d,nullptr,tex);if(FAILED(hr))return hr;
    hr=dev->CreateRenderTargetView(*tex,nullptr,rtv);if(FAILED(hr)){safe_release(*tex);return hr;}return S_OK;
}

static bool query_vp(ID3D11DeviceContext* ctx,float x,float y,float w,float h){
    UINT n=1;D3D11_VIEWPORT v{};ctx->RSGetViewports(&n,&v);
    return n==1&&nearf(v.TopLeftX,x)&&nearf(v.TopLeftY,y)&&nearf(v.Width,w)&&nearf(v.Height,h);
}
static bool query_sc(ID3D11DeviceContext* ctx,LONG l,LONG t,LONG r,LONG b){
    UINT n=1;D3D11_RECT q{};ctx->RSGetScissorRects(&n,&q);
    return n==1&&q.left==l&&q.top==t&&q.right==r&&q.bottom==b;
}

int main(){
    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx);
    if(FAILED(hr)||!dev||!ctx)return 10;
    ID3D11Texture2D *primaryTex=nullptr,*sameTex=nullptr,*offTex=nullptr;
    ID3D11RenderTargetView *primaryRtv=nullptr,*primaryAlias=nullptr,*sameRtv=nullptr,*offRtv=nullptr;
    int rc=0;
    do{
        if(FAILED(make_target(dev,1280,720,&primaryTex,&primaryRtv))){rc=11;break;}
        if(FAILED(dev->CreateRenderTargetView(primaryTex,nullptr,&primaryAlias))){rc=12;break;}
        if(FAILED(make_target(dev,1280,720,&sameTex,&sameRtv))){rc=13;break;}
        if(FAILED(make_target(dev,640,360,&offTex,&offRtv))){rc=14;break;}

        ContextHooks hooks;
        if(!hooks.configure(Contract{{1920,1080},{1280,720}},primaryTex)){rc=15;break;}
        void** before=*reinterpret_cast<void***>(ctx);
        if(!hooks.install(ctx)||!hooks.installed()){rc=16;break;}
        void** during=*reinterpret_cast<void***>(ctx);
        if(during==before){rc=17;break;}

        ID3D11RenderTargetView* bind=primaryAlias;
        ctx->OMSetRenderTargets(1,&bind,nullptr);
        if(!hooks.primary_bound()){rc=18;break;}
        D3D11_VIEWPORT logical{300.0f,160.0f,520.0f,180.0f,0.0f,1.0f};
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,200.0f,106.666664f,346.666656f,120.0f)){rc=19;break;}
        D3D11_RECT sc{301,161,821,341};ctx->RSSetScissorRects(1,&sc);
        if(!query_sc(ctx,200,107,548,228)){rc=20;break;}

        ctx->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
        if(!hooks.primary_bound()){rc=21;break;}

        bind=sameRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        if(hooks.primary_bound()){rc=22;break;}
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,300.0f,160.0f,520.0f,180.0f)){rc=23;break;}
        ctx->RSSetScissorRects(1,&sc);
        if(!query_sc(ctx,301,161,821,341)){rc=24;break;}

        bind=offRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        if(hooks.primary_bound()){rc=25;break;}

        bind=primaryRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        ctx->ClearState();
        if(hooks.primary_bound()){rc=26;break;}
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,300.0f,160.0f,520.0f,180.0f)){rc=27;break;}

        constexpr uint32_t kStress=200000;
        for(uint32_t i=0;i<kStress;++i){
            bind=(i&1u)?primaryAlias:sameRtv;
            ctx->OMSetRenderTargets(1,&bind,nullptr);
            const bool primary=(i&1u)!=0;
            if(hooks.primary_bound()!=primary){rc=28;break;}
            D3D11_VIEWPORT v{float(i%1500u),float(i%800u),320.0f,180.0f,0.0f,1.0f};
            ctx->RSSetViewports(1,&v);
            D3D11_RECT sr{LONG(i%1200u),LONG(i%600u),LONG(i%1200u+300u),LONG(i%600u+150u)};
            ctx->RSSetScissorRects(1,&sr);
        }
        if(rc)break;

        HookStats s=hooks.stats();
        const uint64_t expectedMapped=uint64_t(kStress/2u)+1u;
        if(s.omCalls!=uint64_t(kStress)+4u || s.omUavCalls!=1u ||
           s.viewportCalls!=uint64_t(kStress)+3u || s.scissorCalls!=uint64_t(kStress)+2u ||
           s.viewportMapped!=expectedMapped || s.scissorMapped!=expectedMapped || s.clearStateCalls!=1u){rc=29;break;}

        hooks.uninstall();
        if(hooks.installed()){rc=30;break;}
        void** after=*reinterpret_cast<void***>(ctx);
        if(after!=before){rc=31;break;}
        bind=primaryRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,300.0f,160.0f,520.0f,180.0f)){rc=32;break;}

        std::cout<<"RC41_HOOK_WARP=PASS feature_level=0x"<<std::hex<<static_cast<unsigned>(fl)<<std::dec
                 <<" stress="<<kStress<<" shadow_vtable=PASS restore=PASS primary_alias=PASS same_size_passthrough=PASS"
                 <<" om="<<s.omCalls<<" om_uav="<<s.omUavCalls<<" vp="<<s.viewportCalls<<" vp_mapped="<<s.viewportMapped
                 <<" sc="<<s.scissorCalls<<" sc_mapped="<<s.scissorMapped<<" clear="<<s.clearStateCalls<<"\n";
    }while(false);

    ctx->ClearState();
    safe_release(offRtv);safe_release(offTex);safe_release(sameRtv);safe_release(sameTex);
    safe_release(primaryAlias);safe_release(primaryRtv);safe_release(primaryTex);safe_release(ctx);safe_release(dev);
    if(rc)std::cerr<<"RC41_HOOK_WARP=FAIL rc="<<rc<<"\n";
    return rc;
}
