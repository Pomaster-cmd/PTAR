#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>
#include "ptar_rc41_context_hooks.h"
#include "ptar_rc41_swapchain_hooks.h"

using namespace ptar_rc41;

namespace {
HMODULE g_self=nullptr;
ContextHooks g_contextHooks;
SwapchainHooks g_swapchainHooks;
volatile LONG g_starting=0;
volatile LONG g_active=0;
UINT g_logicalW=0,g_logicalH=0,g_physicalW=0,g_physicalH=0;

struct RC41State {
    UINT size;
    UINT active;
    UINT logicalW,logicalH;
    UINT physicalW,physicalH;
    UINT contextInstalled;
    UINT swapchainInstalled;
    uint64_t viewportMapped;
    uint64_t scissorMapped;
    uint64_t getDescVirtualized;
    uint64_t resizeRemapped;
    uint64_t primaryRefreshes;
};

static void log_line(const char* text) noexcept {
    if(!g_self||!text)return;
    wchar_t path[MAX_PATH]{};
    if(!GetModuleFileNameW(g_self,path,MAX_PATH))return;
    wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ptar_rc41.log");
    HANDLE h=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME st{};GetLocalTime(&st);
    char line[768]{};
    int n=wsprintfA(line,"[%02u:%02u:%02u.%03u] %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,text);
    DWORD wr=0;WriteFile(h,line,(DWORD)n,&wr,nullptr);CloseHandle(h);
}
static void log_dims(const char* tag,UINT a,UINT b,UINT c,UINT d) noexcept {
    char line[512]{};wsprintfA(line,"%s %u %u %u %u",tag,a,b,c,d);log_line(line);
}

static bool runtime_layout_ok(HMODULE runtime,BYTE*& base) noexcept {
    if(!runtime)return false;base=reinterpret_cast<BYTE*>(runtime);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return false;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    if(nt->OptionalHeader.SizeOfImage<0x02C7E400u)return false;
    if(GetProcAddress(runtime,"D3D11CreateDeviceAndSwapChain")!=reinterpret_cast<FARPROC>(base+0x000021C0u))return false;
    IDXGISwapChain* sc=*reinterpret_cast<IDXGISwapChain**>(base+0x02C7E010u);
    if(!sc)return false;
    void** vt=*reinterpret_cast<void***>(sc);if(!vt)return false;
    // Prove P1U46's final render-swapchain layer is already installed. RC41 must chain on top of it.
    if(vt[8]!=reinterpret_cast<void*>(base+0x0000C6C0u)||vt[9]!=reinterpret_cast<void*>(base+0x0000CA10u)||
       vt[12]!=reinterpret_cast<void*>(base+0x0000CDB0u)||vt[13]!=reinterpret_cast<void*>(base+0x0000CE80u))return false;
    return true;
}

static bool current_backbuffer(IDXGISwapChain* sc,ID3D11Texture2D** out,UINT& w,UINT& h) noexcept {
    if(!sc||!out)return false;*out=nullptr;w=0;h=0;
    ID3D11Texture2D* tex=nullptr;
    if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&tex)))||!tex)return false;
    D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);w=d.Width;h=d.Height;*out=tex;return w&&h;
}

static void refresh_primary(void*,IDXGISwapChain* sc) noexcept {
    ID3D11Texture2D* tex=nullptr;UINT w=0,h=0;
    if(!current_backbuffer(sc,&tex,w,h)){log_line("FAIL RC41 resize refresh cannot reacquire physical backbuffer");return;}
    if(w!=g_physicalW||h!=g_physicalH) log_dims("FAIL RC41 resize changed physical raster",w,h,g_physicalW,g_physicalH);
    if(!g_contextHooks.update_primary_resource(tex))log_line("FAIL RC41 resize refresh classifier update");
    tex->Release();
}

static void fail_cleanup() noexcept {
    g_swapchainHooks.uninstall();
    g_contextHooks.uninstall();
    g_logicalW=g_logicalH=g_physicalW=g_physicalH=0;
    InterlockedExchange(&g_active,0);
    InterlockedExchange(&g_starting,0);
}

static int attach_core(IDXGISwapChain* sc,ID3D11DeviceContext* ctx,UINT logicalW,UINT logicalH,HMODULE runtimeModule) noexcept {
    if(!sc||!ctx||!logicalW||!logicalH)return -30;
    ID3D11Texture2D* backbuffer=nullptr;UINT physicalW=0,physicalH=0;
    if(!current_backbuffer(sc,&backbuffer,physicalW,physicalH)){log_line("FAIL RC41 physical backbuffer unavailable; fail-open");return -31;}
    const bool exact=(uint64_t(physicalW)*3u==uint64_t(logicalW)*2u && uint64_t(physicalH)*3u==uint64_t(logicalH)*2u);
    if(!exact){backbuffer->Release();log_dims("FAIL RC41 non-x1.5 geometry",physicalW,physicalH,logicalW,logicalH);return -32;}
    const Contract contract{{logicalW,logicalH},{physicalW,physicalH}};
    if(!g_contextHooks.configure(contract,backbuffer)){backbuffer->Release();log_line("FAIL RC41 context configure; fail-open");return -33;}
    backbuffer->Release();
    if(!g_contextHooks.install(ctx)){log_line("FAIL RC41 context hook install; fail-open");return -34;}

    HMODULE game=GetModuleHandleW(nullptr);
    if(!runtimeModule)runtimeModule=GetModuleHandleW(L"d3d11.dll");
    if(!game||!runtimeModule||!g_self||!g_swapchainHooks.configure(contract,game,runtimeModule,g_self,refresh_primary,nullptr)){
        log_line("FAIL RC41 swapchain configure; fail-open");g_contextHooks.uninstall();return -35;
    }
    g_logicalW=logicalW;g_logicalH=logicalH;g_physicalW=physicalW;g_physicalH=physicalH;
    if(!g_swapchainHooks.install(sc)){log_line("FAIL RC41 swapchain hook install; fail-open");g_contextHooks.uninstall();g_logicalW=g_logicalH=g_physicalW=g_physicalH=0;return -36;}
    InterlockedExchange(&g_active,1);InterlockedExchange(&g_starting,0);
    log_dims("ACTIVE_RC41_LOGICAL_NATIVE_SUBRASTER logical/physical",logicalW,logicalH,physicalW,physicalH);
    return 0;
}
}

extern "C" __declspec(dllexport) int WINAPI PTAR_RC41_Attach(IDXGISwapChain* sc,ID3D11DeviceContext* ctx,UINT logicalW,UINT logicalH){
    if(InterlockedCompareExchange(&g_active,0,0))return 1;
    if(InterlockedCompareExchange(&g_starting,1,0))return 2;
    const int rc=attach_core(sc,ctx,logicalW,logicalH,GetModuleHandleW(L"d3d11.dll"));
    if(rc<0)fail_cleanup();
    return rc;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_RC41_AutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_active,0,0))return 1;
    if(InterlockedCompareExchange(&g_starting,1,0))return 2;
    BYTE* base=nullptr;
    if(!runtime_layout_ok(runtime,base)){log_line("FAIL RC41 runtime/P1U46 post-hook layout guard; fail-open");fail_cleanup();return -20;}
    auto* sc=*reinterpret_cast<IDXGISwapChain**>(base+0x02C7E010u);
    auto* ctx=*reinterpret_cast<ID3D11DeviceContext**>(base+0x02C7DFA8u);
    const UINT outputW=*reinterpret_cast<UINT*>(base+0x0004B040u);
    const UINT outputH=*reinterpret_cast<UINT*>(base+0x0004B044u);
    if(!sc||!ctx||!outputW||!outputH){log_line("FAIL RC41 runtime state unavailable; fail-open");fail_cleanup();return -21;}
    const int rc=attach_core(sc,ctx,outputW,outputH,runtime);
    if(rc<0)fail_cleanup();
    return rc;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_RC41_Query(RC41State* out){
    if(!out||out->size<sizeof(RC41State))return -1;
    RC41State s{};s.size=sizeof(s);s.active=InterlockedCompareExchange(&g_active,0,0)?1u:0u;
    s.logicalW=g_logicalW;s.logicalH=g_logicalH;s.physicalW=g_physicalW;s.physicalH=g_physicalH;
    if(s.active){
        HookStats c=g_contextHooks.stats();SwapchainHookStats q=g_swapchainHooks.stats();
        s.contextInstalled=g_contextHooks.installed()?1u:0u;s.swapchainInstalled=g_swapchainHooks.installed()?1u:0u;
        s.viewportMapped=c.viewportMapped;s.scissorMapped=c.scissorMapped;s.getDescVirtualized=q.getDescVirtualized;s.resizeRemapped=q.resizeRemapped;s.primaryRefreshes=c.primaryRefreshes;
    }
    *out=s;return 0;
}

extern "C" __declspec(dllexport) void WINAPI PTAR_RC41_Detach(){ fail_cleanup(); }

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){g_self=h;DisableThreadLibraryCalls(h);}
    return TRUE;
}
