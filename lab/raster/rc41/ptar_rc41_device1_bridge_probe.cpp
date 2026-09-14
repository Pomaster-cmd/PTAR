#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace {
constexpr size_t kSlots=43;
constexpr size_t kCreateTexture2D=5;
void* g_shadow[kSlots]{};
void** g_baseVtable=nullptr;
ID3D11Device1* g_device1=nullptr;
volatile LONG64 g_calls=0;
thread_local unsigned g_depth=0;

static bool write_text(const char* path,const char* text) noexcept {
    HANDLE h=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;DWORD wr=0;const DWORD n=(DWORD)lstrlenA(text);const BOOL ok=WriteFile(h,text,n,&wr,nullptr);CloseHandle(h);return ok&&wr==n;
}
static void stage(const char* text) noexcept { write_text("DEVICE1_BRIDGE_STAGE.txt",text); }

static HRESULT STDMETHODCALLTYPE hook_create_texture2d(ID3D11Device*,const D3D11_TEXTURE2D_DESC* desc,const D3D11_SUBRESOURCE_DATA* init,ID3D11Texture2D** out){
    const unsigned depth=++g_depth;const LONG64 calls=InterlockedIncrement64(&g_calls);
    char b[160]{};std::snprintf(b,sizeof(b),"HOOK_ENTER depth=%u calls=%lld\r\n",depth,(long long)calls);stage(b);
    if(!g_device1||depth!=1){--g_depth;return E_FAIL;}
    const HRESULT hr=g_device1->CreateTexture2D(desc,init,out);
    std::snprintf(b,sizeof(b),"HOOK_RETURN depth=%u hr=0x%08x\r\n",depth,(unsigned)hr);stage(b);
    --g_depth;return hr;
}

static D3D11_TEXTURE2D_DESC make_desc(UINT w,UINT h) noexcept {
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;return d;
}
}

int main(){
    DeleteFileA("DEVICE1_BRIDGE_STAGE.txt");DeleteFileA("DEVICE1_BRIDGE_RESULT.txt");stage("00 ENTER\r\n");
    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL fl{};
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&fl,&ctx);
    if(FAILED(hr)||!dev||!ctx)return 10;stage("01 DEVICE\r\n");
    if(FAILED(dev->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(&g_device1)))||!g_device1)return 11;
    void** baseVt=*reinterpret_cast<void***>(dev);void** d1Vt=*reinterpret_cast<void***>(g_device1);
    const bool pointerDistinct=reinterpret_cast<void*>(dev)!=reinterpret_cast<void*>(g_device1);
    const bool vtableDistinct=baseVt!=d1Vt;
    char relation[256]{};std::snprintf(relation,sizeof(relation),"02 QI pointer_distinct=%u vtable_distinct=%u base_slot=%p d1_slot=%p\r\n",pointerDistinct?1u:0u,vtableDistinct?1u:0u,baseVt[kCreateTexture2D],d1Vt[kCreateTexture2D]);stage(relation);
    if(!pointerDistinct||!vtableDistinct){g_device1->Release();ctx->Release();dev->Release();return 12;}

    g_baseVtable=baseVt;std::memcpy(g_shadow,baseVt,sizeof(g_shadow));g_shadow[kCreateTexture2D]=reinterpret_cast<void*>(&hook_create_texture2d);
    if(InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(dev),g_shadow,g_baseVtable)!=g_baseVtable)return 13;
    stage("03 BASE_SHADOWED\r\n");
    if(*reinterpret_cast<void***>(g_device1)!=d1Vt)return 14;

    D3D11_TEXTURE2D_DESC d=make_desc(1920,1080);ID3D11Texture2D* tex=nullptr;stage("04 BEFORE_BASE_CREATE\r\n");
    hr=dev->CreateTexture2D(&d,nullptr,&tex);
    if(FAILED(hr)||!tex)return 15;stage("05 AFTER_BASE_CREATE\r\n");
    D3D11_TEXTURE2D_DESC got{};tex->GetDesc(&got);if(got.Width!=1920||got.Height!=1080)return 16;
    const LONG64 calls=InterlockedCompareExchange64(&g_calls,0,0);if(calls!=1)return 17;

    if(InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(dev),g_baseVtable,g_shadow)!=g_shadow)return 18;
    stage("06 RESTORED\r\n");
    char result[512]{};
    std::snprintf(result,sizeof(result),"RC41_DEVICE1_BRIDGE=PASS feature_level=0x%x pointer_distinct=1 vtable_distinct=1 base_calls=%lld create=1920x1080 recursion=NONE restore=PASS\r\n",(unsigned)fl,(long long)calls);
    if(!write_text("DEVICE1_BRIDGE_RESULT.txt",result))return 19;
    std::fputs(result,stdout);std::fflush(stdout);
    tex->Release();g_device1->Release();g_device1=nullptr;ctx->Release();dev->Release();return 0;
}
