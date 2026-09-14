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

        // RC42 regression: the engine may program logical raster state BEFORE it binds
        // the remapped primary-family target. Binding that target must reconcile the
        // already-live state immediately; otherwise a 1920x1080 viewport is clipped by
        // the physical 1280x720 target and the 3D scene appears zoomed/cropped.
        D3D11_VIEWPORT preVp{150.0f,90.0f,900.0f,600.0f,0.0f,1.0f};
        D3D11_RECT preSc{150,90,1050,690};
        ctx->RSSetViewports(1,&preVp);ctx->RSSetScissorRects(1,&preSc);
        if(!query_vp(ctx,150.0f,90.0f,900.0f,600.0f)||!query_sc(ctx,150,90,1050,690)){rc=18;break;}

        ID3D11RenderTargetView* bind=primaryAlias;
        ctx->OMSetRenderTargets(1,&bind,nullptr);
        if(!hooks.primary_bound()){rc=19;break;}
        if(!query_vp(ctx,100.0f,60.0f,600.0f,400.0f)||!query_sc(ctx,100,60,700,460)){rc=20;break;}

        D3D11_VIEWPORT logical{300.0f,160.0f,520.0f,180.0f,0.0f,1.0f};
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,200.0f,106.666664f,346.666656f,120.0f)){rc=21;break;}
        D3D11_RECT sc{301,161,821,341};ctx->RSSetScissorRects(1,&sc);
        if(!query_sc(ctx,200,107,548,228)){rc=22;break;}

        ctx->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,0,D3D11_KEEP_UNORDERED_ACCESS_VIEWS,nullptr,nullptr);
        if(!hooks.primary_bound()){rc=23;break;}

        bind=sameRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        if(hooks.primary_bound()){rc=24;break;}
        // Leaving primary-family rendering must restore the engine's logical state.
        if(!query_vp(ctx,300.0f,160.0f,520.0f,180.0f)||!query_sc(ctx,301,161,821,341)){rc=25;break;}
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,300.0f,160.0f,520.0f,180.0f)){rc=26;break;}
        ctx->RSSetScissorRects(1,&sc);
        if(!query_sc(ctx,301,161,821,341)){rc=27;break;}

        bind=offRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        if(hooks.primary_bound()){rc=28;break;}

        bind=primaryRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        ctx->ClearState();
        if(hooks.primary_bound()){rc=29;break;}
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,300.0f,160.0f,520.0f,180.0f)){rc=30;break;}

        constexpr uint32_t kStress=200000;
        for(uint32_t i=0;i<kStress;++i){
            bind=(i&1u)?primaryAlias:sameRtv;
            ctx->OMSetRenderTargets(1,&bind,nullptr);
            const bool primary=(i&1u)!=0;
            if(hooks.primary_bound()!=primary){rc=31;break;}
            D3D11_VIEWPORT v{float(i%1500u),float(i%800u),320.0f,180.0f,0.0f,1.0f};
            ctx->RSSetViewports(1,&v);
            D3D11_RECT sr{LONG(i%1200u),LONG(i%600u),LONG(i%1200u+300u),LONG(i%600u+150u)};
            ctx->RSSetScissorRects(1,&sr);
        }
        if(rc)break;

        HookStats s=hooks.stats();
        const uint64_t expectedMapped=uint64_t(kStress/2u)+1u;
        if(s.omCalls!=uint64_t(kStress)+4u || s.omUavCalls!=1u ||
           s.viewportCalls!=uint64_t(kStress)+4u || s.scissorCalls!=uint64_t(kStress)+3u ||
           s.viewportMapped!=expectedMapped || s.scissorMapped!=expectedMapped || s.clearStateCalls!=1u){rc=32;break;}
        if(s.primaryBindTransitions<kStress || s.viewportStateReapplies<kStress || s.scissorStateReapplies<kStress ||
           s.viewportCallsUnbound==0 || s.scissorCallsUnbound==0){rc=33;break;}

        hooks.uninstall();
        if(hooks.installed()){rc=34;break;}
        void** after=*reinterpret_cast<void***>(ctx);
        if(after!=before){rc=35;break;}
        bind=primaryRtv;ctx->OMSetRenderTargets(1,&bind,nullptr);
        ctx->RSSetViewports(1,&logical);
        if(!query_vp(ctx,300.0f,160.0f,520.0f,180.0f)){rc=36;break;}

        std::cout<<"RC42_HOOK_WARP=PASS feature_level=0x"<<std::hex<<static_cast<unsigned>(fl)<<std::dec
                 <<" stress="<<kStress<<" prebind_reconcile=PASS unbind_restore=PASS shadow_vtable=PASS restore=PASS primary_alias=PASS same_size_passthrough=PASS"
                 <<" om="<<s.omCalls<<" om_uav="<<s.omUavCalls<<" vp="<<s.viewportCalls<<" vp_mapped="<<s.viewportMapped
                 <<" sc="<<s.scissorCalls<<" sc_mapped="<<s.scissorMapped<<" transitions="<<s.primaryBindTransitions
                 <<" vp_reapply="<<s.viewportStateReapplies<<" sc_reapply="<<s.scissorStateReapplies<<" clear="<<s.clearStateCalls<<"\n";
    }while(false);

    ctx->ClearState();
    safe_release(offRtv);safe_release(offTex);safe_release(sameRtv);safe_release(sameTex);
    safe_release(primaryAlias);safe_release(primaryRtv);safe_release(primaryTex);safe_release(ctx);safe_release(dev);
    if(rc)std::cerr<<"RC42_HOOK_WARP=FAIL rc="<<rc<<"\n";
    return rc;
}
