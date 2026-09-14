#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cmath>
#include <cstdio>
#include "ptar_rc41_context_hooks.h"
#include "ptar_rc41_resource_bridge.h"
#include "ptar_rc41_resource_tag.h"

using namespace ptar_rc41;

template<class T> static void safe_release(T*& p){ if(p){p->Release();p=nullptr;} }
static bool dims(ID3D11Texture2D* t,UINT w,UINT h){ if(!t)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);return d.Width==w&&d.Height==h; }
static bool vp_is(ID3D11DeviceContext* ctx,float x,float y,float w,float h){
    UINT n=1;D3D11_VIEWPORT v{};ctx->RSGetViewports(&n,&v);auto eq=[](float a,float b){return std::fabs(a-b)<3.0e-4f;};
    return n==1&&eq(v.TopLeftX,x)&&eq(v.TopLeftY,y)&&eq(v.Width,w)&&eq(v.Height,h);
}
static D3D11_TEXTURE2D_DESC rt_desc(UINT w,UINT h,UINT bind= D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE){
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=bind;return d;
}
static bool write_result(const char* text){
    HANDLE h=CreateFileA("RC41_RESOURCE_WARP_RESULT.txt",GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;DWORD wr=0;const DWORD n=(DWORD)lstrlenA(text);const BOOL ok=WriteFile(h,text,n,&wr,nullptr);CloseHandle(h);return ok&&wr==n;
}

int main(){
    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx);
    if(FAILED(hr)||!dev||!ctx)return 10;
    const Contract contract{{1920,1080},{1280,720}};
    ID3D11Texture2D* primary=nullptr;D3D11_TEXTURE2D_DESC pd=rt_desc(1280,720);
    if(FAILED(dev->CreateTexture2D(&pd,nullptr,&primary))||!primary)return 11;

    HMODULE game=GetModuleHandleW(nullptr),runtime=GetModuleHandleW(L"d3d11.dll"),sidecar=GetModuleHandleW(L"kernel32.dll");
    ResourceBridge bridge;ContextHooks hooks;
    if(!game||!runtime||!sidecar||!bridge.configure(contract,game,runtime,sidecar)||!hooks.configure(contract,primary))return 12;
    if(!bridge.install(dev)||!hooks.install(ctx))return 13;

    ResourcePolicy policy(contract);D3D11_TEXTURE2D_DESC probe=rt_desc(1920,1080);
    D3D11_SUBRESOURCE_DATA fakeInit{};
    if(policy.map_texture2d(probe,&fakeInit,CallerDomain::Game).remapped)return 14;
    if(policy.map_texture2d(probe,nullptr,CallerDomain::Runtime).remapped)return 15;
    D3D11_TEXTURE2D_DESC srvOnly=probe;srvOnly.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(policy.map_texture2d(srvOnly,nullptr,CallerDomain::Game).remapped)return 16;
    D3D11_TEXTURE2D_DESC shared=probe;shared.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
    if(policy.map_texture2d(shared,nullptr,CallerDomain::Game).remapped)return 17;

    ID3D11Texture2D* familyRT=nullptr;ID3D11RenderTargetView* familyRTV=nullptr;
    if(FAILED(dev->CreateTexture2D(&probe,nullptr,&familyRT))||!dims(familyRT,1280,720)||!resource_has_family_tag(familyRT))return 18;
    if(FAILED(dev->CreateRenderTargetView(familyRT,nullptr,&familyRTV))||!familyRTV||!get_family_tag(familyRTV))return 19;
    ctx->OMSetRenderTargets(1,&familyRTV,nullptr);
    D3D11_VIEWPORT v{300,150,600,300,0,1};ctx->RSSetViewports(1,&v);
    if(!vp_is(ctx,200,100,400,200))return 20;

    D3D11_TEXTURE2D_DESC dd{};dd.Width=1920;dd.Height=1080;dd.MipLevels=1;dd.ArraySize=1;dd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dd.SampleDesc.Count=1;dd.Usage=D3D11_USAGE_DEFAULT;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* familyDepth=nullptr;ID3D11DepthStencilView* familyDSV=nullptr;
    if(FAILED(dev->CreateTexture2D(&dd,nullptr,&familyDepth))||!dims(familyDepth,1280,720)||!resource_has_family_tag(familyDepth))return 21;
    if(FAILED(dev->CreateDepthStencilView(familyDepth,nullptr,&familyDSV))||!familyDSV||!get_family_tag(familyDSV))return 22;
    ctx->ClearState();ctx->OMSetRenderTargets(0,nullptr,familyDSV);ctx->RSSetViewports(1,&v);
    if(!vp_is(ctx,200,100,400,200))return 23;

    ID3D11Texture2D* samePhysical=nullptr;ID3D11RenderTargetView* sameRTV=nullptr;D3D11_TEXTURE2D_DESC same=rt_desc(1280,720);
    if(FAILED(dev->CreateTexture2D(&same,nullptr,&samePhysical))||!dims(samePhysical,1280,720)||resource_has_family_tag(samePhysical))return 24;
    if(FAILED(dev->CreateRenderTargetView(samePhysical,nullptr,&sameRTV))||!sameRTV||get_family_tag(sameRTV))return 25;
    ctx->ClearState();ctx->OMSetRenderTargets(1,&sameRTV,nullptr);ctx->RSSetViewports(1,&v);
    if(!vp_is(ctx,300,150,600,300))return 26;

    ResourceBridgeStats rs=bridge.stats();HookStats hs=hooks.stats();
    if(rs.textureRemapped!=2||rs.textureFallbacks!=0||rs.rtvTagged!=1||rs.dsvTagged!=1||hs.viewportMapped<2)return 27;

    ctx->ClearState();hooks.uninstall();bridge.uninstall();
    ID3D11Texture2D* restored=nullptr;
    if(FAILED(dev->CreateTexture2D(&probe,nullptr,&restored))||!dims(restored,1920,1080)||resource_has_family_tag(restored))return 28;

    char result[640]{};
    wsprintfA(result,"RC41_RESOURCE_WARP=PASS feature_level=0x%x exact_logical_rt=1920x1080->1280x720 depth=PASS dsv_only_mapping=PASS same_size_nonfamily=PASS remapped=%llu rtv_tagged=%llu dsv_tagged=%llu vp_mapped=%llu restore=PASS\r\n",
              static_cast<unsigned>(fl),static_cast<unsigned long long>(rs.textureRemapped),static_cast<unsigned long long>(rs.rtvTagged),
              static_cast<unsigned long long>(rs.dsvTagged),static_cast<unsigned long long>(hs.viewportMapped));
    if(!write_result(result))return 29;
    std::fputs(result,stdout);std::fflush(stdout);

    safe_release(restored);safe_release(sameRTV);safe_release(samePhysical);safe_release(familyDSV);safe_release(familyDepth);safe_release(familyRTV);safe_release(familyRT);safe_release(primary);safe_release(ctx);safe_release(dev);
    return 0;
}
