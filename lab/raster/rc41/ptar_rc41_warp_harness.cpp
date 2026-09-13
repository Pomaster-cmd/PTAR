#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "ptar_rc41_contract.h"

using namespace ptar_rc41;

template<class T> static void safe_release(T*& p){ if(p){ p->Release(); p=nullptr; } }

static bool nearf(float a,float b,float eps=1.0e-4f){ return std::fabs(a-b)<=eps; }

class TargetTracker {
    IUnknown* primaryIdentity_=nullptr;
    bool primaryBound_=false;
public:
    ~TargetTracker(){ safe_release(primaryIdentity_); }
    bool set_primary(ID3D11Resource* r){
        safe_release(primaryIdentity_);
        primaryBound_=false;
        if(!r) return false;
        return SUCCEEDED(r->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&primaryIdentity_))) && primaryIdentity_;
    }
    bool is_primary(ID3D11RenderTargetView* rtv) const {
        if(!rtv || !primaryIdentity_) return false;
        ID3D11Resource* res=nullptr;
        rtv->GetResource(&res);
        if(!res) return false;
        IUnknown* id=nullptr;
        const HRESULT hr=res->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&id));
        res->Release();
        if(FAILED(hr)||!id) return false;
        const bool same=(id==primaryIdentity_);
        id->Release();
        return same;
    }
    void observe_om(UINT count,ID3D11RenderTargetView* const* rtvs){
        primaryBound_=(count>0 && rtvs && rtvs[0] && is_primary(rtvs[0]));
    }
    bool primary_bound() const { return primaryBound_; }
};

static HRESULT make_tex(ID3D11Device* dev,UINT w,UINT h,ID3D11Texture2D** outTex,ID3D11RenderTargetView** outRtv){
    if(!dev||!outTex||!outRtv) return E_INVALIDARG;
    *outTex=nullptr; *outRtv=nullptr;
    D3D11_TEXTURE2D_DESC d{};
    d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count=1;
    d.Usage=D3D11_USAGE_DEFAULT;
    d.BindFlags=D3D11_BIND_RENDER_TARGET;
    HRESULT hr=dev->CreateTexture2D(&d,nullptr,outTex);
    if(FAILED(hr)) return hr;
    hr=dev->CreateRenderTargetView(*outTex,nullptr,outRtv);
    if(FAILED(hr)){ safe_release(*outTex); return hr; }
    return S_OK;
}

static D3D11_VIEWPORT map_vp(const Contract& c,const D3D11_VIEWPORT& v,bool primary){
    const ViewportF in{v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth};
    const ViewportF o=map_viewport(c,in,primary);
    D3D11_VIEWPORT r{};
    r.TopLeftX=o.x; r.TopLeftY=o.y; r.Width=o.w; r.Height=o.h; r.MinDepth=o.minDepth; r.MaxDepth=o.maxDepth;
    return r;
}
static D3D11_RECT map_rect(const Contract& c,const D3D11_RECT& r,bool primary){
    const RectI in{r.left,r.top,r.right,r.bottom};
    const RectI o=map_scissor(c,in,primary);
    D3D11_RECT q{o.l,o.t,o.r,o.b};
    return q;
}

int main(){
    ID3D11Device* dev=nullptr;
    ID3D11DeviceContext* ctx=nullptr;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx);
    if(FAILED(hr)||!dev||!ctx){ std::cerr<<"FAIL create WARP hr=0x"<<std::hex<<static_cast<unsigned long>(hr)<<"\n"; return 10; }

    ID3D11Texture2D *primaryTex=nullptr,*sameSizeOtherTex=nullptr,*offscreenTex=nullptr;
    ID3D11RenderTargetView *primaryRtv1=nullptr,*primaryRtv2=nullptr,*sameSizeOtherRtv=nullptr,*offscreenRtv=nullptr;
    int rc=0;
    do {
        if(FAILED(make_tex(dev,1280,720,&primaryTex,&primaryRtv1))){rc=11;break;}
        if(FAILED(dev->CreateRenderTargetView(primaryTex,nullptr,&primaryRtv2))){rc=12;break;}
        if(FAILED(make_tex(dev,1280,720,&sameSizeOtherTex,&sameSizeOtherRtv))){rc=13;break;}
        if(FAILED(make_tex(dev,640,360,&offscreenTex,&offscreenRtv))){rc=14;break;}

        TargetTracker tracker;
        if(!tracker.set_primary(primaryTex)){rc=15;break;}
        if(!tracker.is_primary(primaryRtv1)||!tracker.is_primary(primaryRtv2)){rc=16;break;}
        if(tracker.is_primary(sameSizeOtherRtv)||tracker.is_primary(offscreenRtv)){rc=17;break;}

        const Contract c{{1920,1080},{1280,720}};
        ID3D11RenderTargetView* bind=primaryRtv2;
        ctx->OMSetRenderTargets(1,&bind,nullptr);
        tracker.observe_om(1,&bind);
        if(!tracker.primary_bound()){rc=18;break;}

        D3D11_VIEWPORT logicalVp{300.0f,160.0f,520.0f,180.0f,0.0f,1.0f};
        D3D11_VIEWPORT physicalVp=map_vp(c,logicalVp,tracker.primary_bound());
        ctx->RSSetViewports(1,&physicalVp);
        UINT vpCount=1; D3D11_VIEWPORT gotVp{};
        ctx->RSGetViewports(&vpCount,&gotVp);
        if(vpCount!=1 || !nearf(gotVp.TopLeftX,200.0f) || !nearf(gotVp.TopLeftY,106.666664f,2e-4f) ||
           !nearf(gotVp.Width,346.666656f,2e-4f) || !nearf(gotVp.Height,120.0f) ||
           gotVp.MinDepth!=0.0f || gotVp.MaxDepth!=1.0f){rc=19;break;}

        D3D11_RECT logicalSc{301,161,821,341};
        D3D11_RECT physicalSc=map_rect(c,logicalSc,tracker.primary_bound());
        ctx->RSSetScissorRects(1,&physicalSc);
        UINT scCount=1; D3D11_RECT gotSc{};
        ctx->RSGetScissorRects(&scCount,&gotSc);
        if(scCount!=1 || gotSc.left!=200 || gotSc.top!=107 || gotSc.right!=548 || gotSc.bottom!=228){rc=20;break;}

        bind=sameSizeOtherRtv;
        ctx->OMSetRenderTargets(1,&bind,nullptr);
        tracker.observe_om(1,&bind);
        if(tracker.primary_bound()){rc=21;break;}
        D3D11_VIEWPORT passVp=map_vp(c,logicalVp,tracker.primary_bound());
        if(!nearf(passVp.TopLeftX,logicalVp.TopLeftX)||!nearf(passVp.TopLeftY,logicalVp.TopLeftY)||
           !nearf(passVp.Width,logicalVp.Width)||!nearf(passVp.Height,logicalVp.Height)){rc=22;break;}

        bind=offscreenRtv;
        ctx->OMSetRenderTargets(1,&bind,nullptr);
        tracker.observe_om(1,&bind);
        if(tracker.primary_bound()){rc=23;break;}

        ctx->OMSetRenderTargets(0,nullptr,nullptr);
        tracker.observe_om(0,nullptr);
        if(tracker.primary_bound()){rc=24;break;}

        std::cout<<"RC41_WARP_TARGET_CLASSIFIER=PASS feature_level=0x"<<std::hex<<static_cast<unsigned>(fl)<<std::dec
                 <<" primary_alias=PASS same_size_nonprimary=PASS offscreen_passthrough=PASS"
                 <<" viewport=200,106.6667,346.6667,120 scissor=200,107,548,228\n";
    } while(false);

    ctx->ClearState();
    safe_release(offscreenRtv); safe_release(offscreenTex);
    safe_release(sameSizeOtherRtv); safe_release(sameSizeOtherTex);
    safe_release(primaryRtv2); safe_release(primaryRtv1); safe_release(primaryTex);
    safe_release(ctx); safe_release(dev);
    if(rc){ std::cerr<<"RC41_WARP_TARGET_CLASSIFIER=FAIL rc="<<rc<<"\n"; }
    return rc;
}
