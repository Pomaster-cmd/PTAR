#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstring>
#include <cstdint>
#include <stddef.h>

// RC51 keeps RC46's field-proven window geometry/input policy byte-for-byte at
// source level. The only new behaviour is a presenter-swapchain backing-size
// reconciliation executed on the P1U46 presenter Present thread after a frame
// has been submitted. No SetParent/owner/WS_CHILD policy is introduced.
#define DllMain DllMain_RC46_INTERNAL
#include "../../borderless/rc46/ptar_borderless_rc46_prod.cpp"
#undef DllMain

namespace {

constexpr uintptr_t kPresenterSwapchain=0x02C7DFC8u;
constexpr uintptr_t kPresenterCachedW=0x02C7E000u;
constexpr uintptr_t kPresenterCachedH=0x02C7E004u;
constexpr uintptr_t kPresenterCachedFormat=0x02C7E008u;
constexpr uintptr_t kPresenterReady=0x02C7E00Cu;
constexpr uintptr_t kReleasePresenterResources=0x00024DF0u;
constexpr uintptr_t kRebuildPresenterBackbuffer=0x0000C200u;
constexpr size_t kSwapVtableSlots=18;
constexpr size_t kPresentSlot=8;
constexpr size_t kGetDescSlot=12;
constexpr size_t kResizeBuffersSlot=13;

using PresentFn=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using GetDescFn=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*,DXGI_SWAP_CHAIN_DESC*);
using ResizeBuffersFn=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using CleanupFn=void(*)();
using RebuildFn=HRESULT(*)(ID3D11Device*);

static volatile LONG g_rc51Stop=0;
static volatile LONG g_rc51HookInstalled=0;
static volatile LONG g_rc51Resyncing=0;
static volatile LONG64 g_rc51PresentCalls=0;
static volatile LONG64 g_rc51MismatchChecks=0;
static volatile LONG64 g_rc51Resyncs=0;
static volatile LONG64 g_rc51Failures=0;
static volatile LONG64 g_rc51TransitionSkips=0;
static volatile LONG64 g_rc51ZeroClientSkips=0;
static volatile LONG64 g_rc51SwapReplacements=0;
static IDXGISwapChain* g_rc51Swap=nullptr;
static void** g_rc51OriginalVtable=nullptr;
static void* g_rc51ShadowVtable[kSwapVtableSlots]{};
static PresentFn g_rc51OrigPresent=nullptr;
static GetDescFn g_rc51OrigGetDesc=nullptr;
static ResizeBuffersFn g_rc51OrigResize=nullptr;
static UINT g_rc51LastHwndW=0,g_rc51LastHwndH=0,g_rc51LastSwapW=0,g_rc51LastSwapH=0,g_rc51LastCachedW=0,g_rc51LastCachedH=0;

static bool rc51_client_size(UINT& w,UINT& h) noexcept {
    w=h=0;
    if(!IsWindow(g_presenter) || IsIconic(g_presenter)) return false;
    RECT c{};if(!GetClientRect(g_presenter,&c))return false;
    const LONG cw=c.right-c.left,ch=c.bottom-c.top;
    if(cw<=0||ch<=0)return false;
    w=(UINT)cw;h=(UINT)ch;return true;
}

static bool rc51_runtime_ok(BYTE*& base) noexcept {
    base=nullptr;
    IMAGE_NT_HEADERS64* nt=nullptr;
    if(!g_rc45Runtime || !rc45_runtime_layout_ok(g_rc45Runtime,base,nt)) return false;
    // Additional sentinels specifically protect the internal resource lifecycle
    // functions used by RC51. These offsets are exact-P1U46 only.
    static const BYTE cleanupAnchor[]={0x48,0x83,0xEC,0x28};
    static const BYTE rebuildAnchor[]={0x48,0x89,0x5C,0x24};
    if(std::memcmp(base+kReleasePresenterResources,cleanupAnchor,sizeof(cleanupAnchor))!=0) return false;
    if(std::memcmp(base+kRebuildPresenterBackbuffer,rebuildAnchor,sizeof(rebuildAnchor))!=0) return false;
    return true;
}

static void rc51_snapshot(UINT hwndW,UINT hwndH,const DXGI_SWAP_CHAIN_DESC& d,BYTE* base) noexcept {
    g_rc51LastHwndW=hwndW;g_rc51LastHwndH=hwndH;g_rc51LastSwapW=d.BufferDesc.Width;g_rc51LastSwapH=d.BufferDesc.Height;
    g_rc51LastCachedW=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedW);
    g_rc51LastCachedH=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedH);
}

static bool rc51_resync_if_needed(IDXGISwapChain* sc) noexcept {
    if(!sc || sc!=g_rc51Swap || !g_rc51OrigGetDesc || !g_rc51OrigResize) return false;
    BYTE* base=nullptr;if(!rc51_runtime_ok(base)) return false;
    if(InterlockedCompareExchange(&g_internal,0,0)!=0){InterlockedIncrement64(&g_rc51TransitionSkips);return false;}
    UINT hwndW=0,hwndH=0;if(!rc51_client_size(hwndW,hwndH)){InterlockedIncrement64(&g_rc51ZeroClientSkips);return false;}
    DXGI_SWAP_CHAIN_DESC d{};if(FAILED(g_rc51OrigGetDesc(sc,&d)))return false;
    InterlockedIncrement64(&g_rc51MismatchChecks);
    rc51_snapshot(hwndW,hwndH,d,base);
    const UINT cachedW=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedW);
    const UINT cachedH=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedH);
    const bool ready=*reinterpret_cast<volatile UINT*>(base+kPresenterReady)!=0;
    if(d.BufferDesc.Width==hwndW && d.BufferDesc.Height==hwndH && cachedW==hwndW && cachedH==hwndH && ready) return true;
    if(InterlockedCompareExchange(&g_rc51Resyncing,1,0)!=0) return false;

    // Re-read after acquiring the guard. A mode transition may have completed
    // between the first test and this frame boundary.
    if(InterlockedCompareExchange(&g_internal,0,0)!=0){InterlockedIncrement64(&g_rc51TransitionSkips);InterlockedExchange(&g_rc51Resyncing,0);return false;}
    if(!rc51_client_size(hwndW,hwndH)){InterlockedIncrement64(&g_rc51ZeroClientSkips);InterlockedExchange(&g_rc51Resyncing,0);return false;}
    if(FAILED(g_rc51OrigGetDesc(sc,&d))){InterlockedIncrement64(&g_rc51Failures);InterlockedExchange(&g_rc51Resyncing,0);return false;}

    ID3D11Device* dev=nullptr;
    HRESULT devHr=sc->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&dev));
    if(FAILED(devHr)||!dev){
        InterlockedIncrement64(&g_rc51Failures);
        logfmt("FAIL RC51 presenter GetDevice",devHr,hwndW,hwndH,0);
        InterlockedExchange(&g_rc51Resyncing,0);return false;
    }
    auto cleanup=reinterpret_cast<CleanupFn>(base+kReleasePresenterResources);
    auto rebuild=reinterpret_cast<RebuildFn>(base+kRebuildPresenterBackbuffer);
    logfmt("RC51_PRESENTER_RESYNC_BEGIN hwnd/swap",(LONG_PTR)((hwndW<<16)^(hwndH&0xFFFFu)),(LONG_PTR)((d.BufferDesc.Width<<16)^(d.BufferDesc.Height&0xFFFFu)),cachedW,cachedH);

    cleanup();
    HRESULT resizeHr=g_rc51OrigResize(sc,0,hwndW,hwndH,DXGI_FORMAT_UNKNOWN,d.Flags);
    HRESULT rebuildHr=rebuild(dev); // also restores resources if ResizeBuffers failed
    dev->Release();

    DXGI_SWAP_CHAIN_DESC after{};HRESULT descHr=g_rc51OrigGetDesc(sc,&after);
    const UINT afterCachedW=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedW);
    const UINT afterCachedH=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedH);
    const bool afterReady=*reinterpret_cast<volatile UINT*>(base+kPresenterReady)!=0;
    if(SUCCEEDED(descHr))rc51_snapshot(hwndW,hwndH,after,base);
    const bool ok=SUCCEEDED(resizeHr)&&SUCCEEDED(rebuildHr)&&SUCCEEDED(descHr)&&after.BufferDesc.Width==hwndW&&after.BufferDesc.Height==hwndH&&afterCachedW==hwndW&&afterCachedH==hwndH&&afterReady;
    if(ok){
        InterlockedIncrement64(&g_rc51Resyncs);
        logfmt("RC51_PRESENTER_RESYNC_PASS hwnd/swap",(LONG_PTR)((hwndW<<16)^(hwndH&0xFFFFu)),(LONG_PTR)((after.BufferDesc.Width<<16)^(after.BufferDesc.Height&0xFFFFu)),afterCachedW,afterCachedH);
    } else {
        InterlockedIncrement64(&g_rc51Failures);
        logfmt("FAIL RC51 presenter resync HRESULT",resizeHr,rebuildHr,descHr,afterReady?1:0);
        logfmt("FAIL RC51 presenter resync dimensions",hwndW,hwndH,after.BufferDesc.Width,after.BufferDesc.Height);
    }
    InterlockedExchange(&g_rc51Resyncing,0);
    return ok;
}

static HRESULT STDMETHODCALLTYPE rc51_present(IDXGISwapChain* sc,UINT syncInterval,UINT flags){
    PresentFn original=g_rc51OrigPresent;
    if(!original)return E_FAIL;
    const HRESULT hr=original(sc,syncInterval,flags);
    InterlockedIncrement64(&g_rc51PresentCalls);
    if(SUCCEEDED(hr))rc51_resync_if_needed(sc);
    return hr;
}

static void rc51_uninstall_hook() noexcept {
    if(g_rc51Swap && g_rc51OriginalVtable && InterlockedCompareExchange(&g_rc51HookInstalled,0,0)){
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(g_rc51Swap),g_rc51OriginalVtable,g_rc51ShadowVtable);
    }
    InterlockedExchange(&g_rc51HookInstalled,0);
    g_rc51OriginalVtable=nullptr;g_rc51OrigPresent=nullptr;g_rc51OrigGetDesc=nullptr;g_rc51OrigResize=nullptr;
    if(g_rc51Swap){g_rc51Swap->Release();g_rc51Swap=nullptr;}
}

static bool rc51_install_hook(IDXGISwapChain* sc) noexcept {
    if(!sc)return false;
    if(InterlockedCompareExchange(&g_rc51HookInstalled,0,0) && sc==g_rc51Swap)return true;
    if(InterlockedCompareExchange(&g_rc51Resyncing,0,0))return false;
    rc51_uninstall_hook();
    void*** obj=reinterpret_cast<void***>(sc);void** vt=*obj;if(!vt)return false;
    std::memcpy(g_rc51ShadowVtable,vt,sizeof(g_rc51ShadowVtable));
    g_rc51OrigPresent=reinterpret_cast<PresentFn>(vt[kPresentSlot]);
    g_rc51OrigGetDesc=reinterpret_cast<GetDescFn>(vt[kGetDescSlot]);
    g_rc51OrigResize=reinterpret_cast<ResizeBuffersFn>(vt[kResizeBuffersSlot]);
    if(!g_rc51OrigPresent||!g_rc51OrigGetDesc||!g_rc51OrigResize)return false;
    g_rc51ShadowVtable[kPresentSlot]=reinterpret_cast<void*>(&rc51_present);
    sc->AddRef();g_rc51Swap=sc;g_rc51OriginalVtable=vt;
    void* prior=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(sc),g_rc51ShadowVtable,vt);
    if(prior!=vt){g_rc51Swap=nullptr;sc->Release();g_rc51OriginalVtable=nullptr;g_rc51OrigPresent=nullptr;g_rc51OrigGetDesc=nullptr;g_rc51OrigResize=nullptr;return false;}
    InterlockedExchange(&g_rc51HookInstalled,1);
    logfmt("RC51_PRESENTER_PRESENT_HOOK_INSTALLED",reinterpret_cast<LONG_PTR>(sc),reinterpret_cast<LONG_PTR>(vt),reinterpret_cast<LONG_PTR>(g_rc51ShadowVtable),0);
    return true;
}

static DWORD WINAPI rc51_worker(LPVOID){
    IDXGISwapChain* last=nullptr;
    while(!InterlockedCompareExchange(&g_rc51Stop,0,0)){
        BYTE* base=nullptr;
        if(!rc51_runtime_ok(base) || !InterlockedCompareExchange(&g_rc45Installed,0,0)){Sleep(10);continue;}
        IDXGISwapChain* current=*reinterpret_cast<IDXGISwapChain* volatile*>(base+kPresenterSwapchain);
        if(current && current!=last){
            if(last)InterlockedIncrement64(&g_rc51SwapReplacements);
            if(rc51_install_hook(current))last=current;
            else logline("WARN RC51 presenter hook install deferred");
        }
        Sleep(10);
    }
    return 0;
}

} // namespace

struct PTARRC51PresenterSyncState {
    UINT size,installed,resyncing;
    unsigned long long presentCalls,mismatchChecks,resyncs,failures,transitionSkips,zeroClientSkips,swapReplacements;
    UINT hwndW,hwndH,swapW,swapH,cachedW,cachedH;
};

extern "C" __declspec(dllexport) int WINAPI PTAR_RC51_QueryPresenterSync(PTARRC51PresenterSyncState* s){
    if(!s||s->size<offsetof(PTARRC51PresenterSyncState,hwndW))return -1;
    const UINT caller=s->size;ZeroMemory(reinterpret_cast<BYTE*>(s)+sizeof(UINT),caller-sizeof(UINT));
    s->installed=(UINT)InterlockedCompareExchange(&g_rc51HookInstalled,0,0);s->resyncing=(UINT)InterlockedCompareExchange(&g_rc51Resyncing,0,0);
    s->presentCalls=(unsigned long long)InterlockedCompareExchange64(&g_rc51PresentCalls,0,0);s->mismatchChecks=(unsigned long long)InterlockedCompareExchange64(&g_rc51MismatchChecks,0,0);s->resyncs=(unsigned long long)InterlockedCompareExchange64(&g_rc51Resyncs,0,0);s->failures=(unsigned long long)InterlockedCompareExchange64(&g_rc51Failures,0,0);s->transitionSkips=(unsigned long long)InterlockedCompareExchange64(&g_rc51TransitionSkips,0,0);s->zeroClientSkips=(unsigned long long)InterlockedCompareExchange64(&g_rc51ZeroClientSkips,0,0);s->swapReplacements=(unsigned long long)InterlockedCompareExchange64(&g_rc51SwapReplacements,0,0);
    if(caller>=sizeof(PTARRC51PresenterSyncState)){s->hwndW=g_rc51LastHwndW;s->hwndH=g_rc51LastHwndH;s->swapW=g_rc51LastSwapW;s->swapH=g_rc51LastSwapH;s->cachedW=g_rc51LastCachedW;s->cachedH=g_rc51LastCachedH;}
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID reserved){
    if(reason==DLL_PROCESS_ATTACH){
        if(!DllMain_RC46_INTERNAL(h,reason,reserved))return FALSE;
        InterlockedExchange(&g_rc51Stop,0);
        HANDLE th=CreateThread(nullptr,0,rc51_worker,nullptr,0,nullptr);
        if(th)CloseHandle(th);else logline("WARN RC51 presenter-sync worker unavailable; RC46 remains active");
        logline("RC51_PRESENTER_SWAPCHAIN_SYNC_MODULE_LOADED");
        return TRUE;
    }
    if(reason==DLL_PROCESS_DETACH){
        InterlockedExchange(&g_rc51Stop,1);
        rc51_uninstall_hook();
        return DllMain_RC46_INTERNAL(h,reason,reserved);
    }
    return DllMain_RC46_INTERNAL(h,reason,reserved);
}
