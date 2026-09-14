#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cmath>
#include "../rc41/ptar_rc41_context_hooks.h"

using namespace ptar_rc41;

template<class T> static void rel(T*& p){if(p){p->Release();p=nullptr;}}

static bool nearf(float a,float b){return std::fabs(a-b)<0.75f;}

struct Rig {
    ID3D11Device* dev=nullptr;
    ID3D11DeviceContext* ctx=nullptr;
    ID3D11Texture2D* tex=nullptr;
    ID3D11RenderTargetView* rtv=nullptr;
    FILE* log=nullptr;
    Contract contract{{1920,1080},{1280,720}};

    bool init(){
        D3D_FEATURE_LEVEL fl{};
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx)))return false;
        D3D11_TEXTURE2D_DESC d{};d.Width=1280;d.Height=720;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(FAILED(dev->CreateTexture2D(&d,nullptr,&tex)))return false;
        if(FAILED(dev->CreateRenderTargetView(tex,nullptr,&rtv)))return false;
        return true;
    }
    void reset(){ctx->OMSetRenderTargets(0,nullptr,nullptr);ctx->ClearState();}
    void set_raster(float w,float h,LONG rw,LONG rh){
        D3D11_VIEWPORT v{0,0,w,h,0,1};ctx->RSSetViewports(1,&v);
        D3D11_RECT r{0,0,rw,rh};ctx->RSSetScissorRects(1,&r);
    }
    bool read(float& w,float& h,LONG& rw,LONG& rh){
        D3D11_VIEWPORT v{};UINT n=1;ctx->RSGetViewports(&n,&v);if(n!=1)return false;
        D3D11_RECT r{};n=1;ctx->RSGetScissorRects(&n,&r);if(n!=1)return false;
        w=v.Width;h=v.Height;rw=r.right;rh=r.bottom;return true;
    }
    void done(){rel(rtv);rel(tex);rel(ctx);rel(dev);}
};

static bool expect(Rig& r,const char* tag,float wantW,float wantH,LONG wantRW,LONG wantRH){
    float w=0,h=0;LONG rw=0,rh=0;const bool got=r.read(w,h,rw,rh);
    const bool ok=got&&nearf(w,wantW)&&nearf(h,wantH)&&rw==wantRW&&rh==wantRH;
    std::fprintf(r.log,"%s=%s actual_vp=%.3fx%.3f actual_sc=%ldx%ld want=%.0fx%.0f/%ldx%ld\n",tag,ok?"PASS":"FAIL",w,h,rw,rh,wantW,wantH,wantRW,wantRH);std::fflush(r.log);
    return ok;
}

static bool case_startup_unbound_physical(Rig& r){
    r.reset();r.set_raster(1280,720,1280,720);
    ContextHooks h;if(!h.configure(r.contract,r.tex)||!h.install(r.ctx))return false;
    const bool beganUnbound=!h.primary_bound();
    r.ctx->OMSetRenderTargets(1,&r.rtv,nullptr);
    const bool ok=beganUnbound&&expect(r,"STARTUP_UNBOUND_PHYSICAL",1280,720,1280,720);
    HookStats s=h.stats();std::fprintf(r.log,"STARTUP_STATS bound=%u transitions=%llu reapply_vp=%llu reapply_sc=%llu\n",h.primary_bound()?1:0,(unsigned long long)s.primaryBindTransitions,(unsigned long long)s.viewportStateReapplies,(unsigned long long)s.scissorStateReapplies);
    h.uninstall();return ok;
}

static bool case_live_unbound_physical(Rig& r){
    r.reset();ContextHooks h;if(!h.configure(r.contract,r.tex)||!h.install(r.ctx))return false;
    r.set_raster(1280,720,1280,720);
    r.ctx->OMSetRenderTargets(1,&r.rtv,nullptr);
    const bool ok=expect(r,"LIVE_UNBOUND_PHYSICAL",1280,720,1280,720);
    HookStats s=h.stats();std::fprintf(r.log,"LIVE_PHYSICAL_STATS unbound_vp=%llu unbound_sc=%llu mapped_vp=%llu mapped_sc=%llu\n",(unsigned long long)s.viewportCallsUnbound,(unsigned long long)s.scissorCallsUnbound,(unsigned long long)s.viewportMapped,(unsigned long long)s.scissorMapped);
    h.uninstall();return ok;
}

static bool case_live_unbound_logical(Rig& r){
    r.reset();ContextHooks h;if(!h.configure(r.contract,r.tex)||!h.install(r.ctx))return false;
    r.set_raster(1920,1080,1920,1080);
    r.ctx->OMSetRenderTargets(1,&r.rtv,nullptr);
    const bool ok=expect(r,"LIVE_UNBOUND_LOGICAL",1280,720,1280,720);
    h.uninstall();return ok;
}

static bool case_startup_bound_physical(Rig& r){
    r.reset();r.ctx->OMSetRenderTargets(1,&r.rtv,nullptr);r.set_raster(1280,720,1280,720);
    ContextHooks h;if(!h.configure(r.contract,r.tex)||!h.install(r.ctx))return false;
    const bool ok=h.primary_bound()&&expect(r,"STARTUP_BOUND_PHYSICAL",1280,720,1280,720);
    h.uninstall();return ok;
}

static bool case_startup_bound_logical(Rig& r){
    r.reset();r.ctx->OMSetRenderTargets(1,&r.rtv,nullptr);r.set_raster(1920,1080,1920,1080);
    ContextHooks h;if(!h.configure(r.contract,r.tex)||!h.install(r.ctx))return false;
    const bool ok=h.primary_bound()&&expect(r,"STARTUP_BOUND_LOGICAL",1280,720,1280,720);
    h.uninstall();return ok;
}

static bool case_rebind_no_double_map(Rig& r){
    r.reset();ContextHooks h;if(!h.configure(r.contract,r.tex)||!h.install(r.ctx))return false;
    r.ctx->OMSetRenderTargets(1,&r.rtv,nullptr);r.set_raster(1920,1080,1920,1080);
    if(!expect(r,"REBIND_FIRST_LOGICAL",1280,720,1280,720)){h.uninstall();return false;}
    r.ctx->OMSetRenderTargets(0,nullptr,nullptr);
    r.set_raster(1280,720,1280,720);
    r.ctx->OMSetRenderTargets(1,&r.rtv,nullptr);
    const bool ok=expect(r,"REBIND_AFTER_PHYSICAL",1280,720,1280,720);
    h.uninstall();return ok;
}

int main(){
    Rig r;_wfopen_s(&r.log,L"RC52_RASTER_STATE_HOST.txt",L"wb");if(!r.log)return 90;
    if(!r.init()){std::fprintf(r.log,"INIT=FAIL\n");std::fclose(r.log);return 91;}
    std::fprintf(r.log,"RC52_RASTER_STATE_BEGIN logical=1920x1080 physical=1280x720 double_map_signature=853.333x480\n");
    int fail=0;
    if(!case_startup_unbound_physical(r))++fail;
    if(!case_live_unbound_physical(r))++fail;
    if(!case_live_unbound_logical(r))++fail;
    if(!case_startup_bound_physical(r))++fail;
    if(!case_startup_bound_logical(r))++fail;
    if(!case_rebind_no_double_map(r))++fail;
    std::fprintf(r.log,"RC52_RASTER_STATE_HOST=%s failures=%d\n",fail?"FAIL":"PASS",fail);std::fflush(r.log);
    r.done();std::fclose(r.log);return fail?40+fail:0;
}
