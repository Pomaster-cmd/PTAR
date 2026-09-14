#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace {
using FnCreateTexture2D=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
constexpr size_t kSlots=43;
constexpr size_t kCreateTexture2D=5;
void* g_shadow[kSlots]{};
void** g_originalVtable=nullptr;
FnCreateTexture2D g_originalCreateTexture2D=nullptr;
volatile LONG64 g_calls=0;
thread_local unsigned g_depth=0;

static bool write_text(const char* path,const char* text) noexcept {
    HANDLE h=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    DWORD wr=0;const DWORD n=(DWORD)lstrlenA(text);const BOOL ok=WriteFile(h,text,n,&wr,nullptr);CloseHandle(h);return ok&&wr==n;
}
static void stage(const char* s) noexcept { write_text("DEVICE_HOOK_STAGE.txt",s); }

static HRESULT STDMETHODCALLTYPE hook_create_texture2d(ID3D11Device* self,const D3D11_TEXTURE2D_DESC* desc,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture2D** out){
    const unsigned depth=++g_depth;
    char buf[128]{};std::snprintf(buf,sizeof(buf),"HOOK_ENTER depth=%u calls=%lld\r\n",depth,(long long)InterlockedIncrement64(&g_calls));stage(buf);
    FnCreateTexture2D fn=g_originalCreateTexture2D;
    if(!fn){--g_depth;return E_FAIL;}
    const HRESULT hr=fn(self,desc,init,out);
    std::snprintf(buf,sizeof(buf),"HOOK_RETURN depth=%u hr=0x%08x\r\n",depth,(unsigned)hr);stage(buf);
    --g_depth;
    return hr;
}

static D3D11_TEXTURE2D_DESC make_desc(UINT w,UINT h) noexcept {
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;return d;
}
}

int main(){
    DeleteFileA("DEVICE_HOOK_STAGE.txt");DeleteFileA("DEVICE_HOOK_RESULT.txt");
    stage("00 ENTER\r\n");
    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx);
    if(FAILED(hr)||!dev||!ctx)return 10;
    stage("01 DEVICE_CREATED\r\n");

    void*** pvt=reinterpret_cast<void***>(dev);g_originalVtable=*pvt;
    if(!g_originalVtable)return 11;
    std::memcpy(g_shadow,g_originalVtable,sizeof(g_shadow));
    g_originalCreateTexture2D=reinterpret_cast<FnCreateTexture2D>(g_originalVtable[kCreateTexture2D]);
    if(!g_originalCreateTexture2D)return 12;
    g_shadow[kCreateTexture2D]=reinterpret_cast<void*>(&hook_create_texture2d);
    void* prior=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(dev),g_shadow,g_originalVtable);
    if(prior!=g_originalVtable)return 13;
    stage("02 VTABLE_SWAPPED\r\n");

    D3D11_TEXTURE2D_DESC d=make_desc(1920,1080);ID3D11Texture2D* tex=nullptr;
    stage("03 BEFORE_CREATE\r\n");
    hr=dev->CreateTexture2D(&d,nullptr,&tex);
    if(FAILED(hr)||!tex){
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(dev),g_originalVtable,g_shadow);
        if(tex)tex->Release();ctx->Release();dev->Release();return 14;
    }
    stage("04 AFTER_CREATE\r\n");
    D3D11_TEXTURE2D_DESC got{};tex->GetDesc(&got);
    if(got.Width!=1920||got.Height!=1080)return 15;

    void* restored=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(dev),g_originalVtable,g_shadow);
    if(restored!=g_shadow)return 16;
    stage("05 VTABLE_RESTORED\r\n");
    const LONG64 calls=InterlockedCompareExchange64(&g_calls,0,0);
    if(calls!=1)return 17;

    char result[384]{};
    std::snprintf(result,sizeof(result),"RC41_DEVICE_HOOK_PROBE=PASS feature_level=0x%x calls=%lld recursion=NONE create=1920x1080 restore=PASS\r\n",(unsigned)fl,(long long)calls);
    if(!write_text("DEVICE_HOOK_RESULT.txt",result))return 18;
    std::fputs(result,stdout);std::fflush(stdout);
    tex->Release();ctx->Release();dev->Release();
    return 0;
}
