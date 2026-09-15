#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wchar.h>
#include <stdint.h>
#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"

// RC60 PASSIVE ROUTE COORDINATOR
// ------------------------------
// The field RC59 failure proved that startup-time HWND subclassing / canonicalization
// is unsafe while Warhammer's UI thread is busy and P1U46 is still advancing its own
// input-window policy. RC60 therefore has deliberately narrower authority:
//   * P1U46 exclusively owns the game WndProc and all presenter/DXGI state.
//   * The game exclusively owns game-HWND style, size and placement.
//   * RC60 never subclasses, never SetWindowLongPtr/SetWindowPos, never writes P1U46
//     private policy bytes, and never mutates presenter geometry or buffers.
//   * WindowStyle is only mirrored to P1U46's already-validated F10 route switch.
//   * Initial Borderless (WindowStyle=1) is completely passive: P1U46 already starts
//     on USR PRESENTER and RC60 sends no startup message at all.
//   * Initial Windowed waits for a responsive UI thread before posting F10; no synchronous
//     CallWindowProc is performed from a foreign thread.

namespace {
HMODULE g_self=nullptr,g_runtime=nullptr;
BYTE* g_base=nullptr;
HWND g_game=nullptr;
UINT g_renderW=0,g_renderH=0;
volatile LONG g_started=0,g_installed=0,g_stop=0,g_uiReady=0,g_pending=0;
volatile LONG64 g_prefChanges=0,g_togglePosts=0,g_toggleRetries=0,g_postFailures=0,g_routeMatches=0,g_uiTimeouts=0;
DWORD g_startTick=0,g_pendingTick=0;
int g_lastPref=-2;

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static constexpr SIZE_T kGameHwndRva=0x02C3FAC0u;
static constexpr SIZE_T kRenderWRva=0x02C3FB78u;
static constexpr SIZE_T kRenderHRva=0x02C3FB7Cu;
static constexpr SIZE_T kUsrActiveRva=0x02C7E074u; // exact P1U46 read-only route state: 1=USR, 0=GAME DIRECT

static void logline(const char* s) noexcept {
    if(!g_self||!s)return;wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(g_self,path,MAX_PATH))return;wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ptar_borderless_rc60.log");HANDLE h=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME st{};GetLocalTime(&st);char line[1024]{};int n=wsprintfA(line,"[%02u:%02u:%02u.%03u] %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,s);DWORD wr=0;WriteFile(h,line,(DWORD)n,&wr,nullptr);CloseHandle(h);
}
static void logfmt(const char* tag,LONG_PTR a,LONG_PTR b=0,LONG_PTR c=0,LONG_PTR d=0) noexcept {char x[768]{};wsprintfA(x,"%s %I64d %I64d %I64d %I64d",tag,(long long)a,(long long)b,(long long)c,(long long)d);logline(x);}
static int read_pref() noexcept {HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return -1;DWORD t=0,v=0,s=sizeof(v);LONG r=RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&v,&s);RegCloseKey(k);return (r==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(v)&&(v==0||v==1))?(int)v:-1;}
static bool runtime_ok(HMODULE runtime,BYTE*& base) noexcept {
    base=nullptr;if(!runtime)return false;base=(BYTE*)runtime;auto* dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return false;auto* nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->OptionalHeader.SizeOfImage<=kUsrActiveRva)return false;
    FARPROC exported=GetProcAddress(runtime,"D3D11CreateDeviceAndSwapChain");return exported==(FARPROC)(base+0x000021C0u);
}
static int route_usr() noexcept {if(!g_base)return -1;return (*(volatile BYTE*)(g_base+kUsrActiveRva))?1:0;}
static bool game_valid() noexcept {return IsWindow(g_game)!=FALSE&&g_renderW!=0&&g_renderH!=0;}
static bool ui_probe(DWORD timeoutMs=60) noexcept {
    if(!IsWindow(g_game))return false;DWORD_PTR r=0;SetLastError(ERROR_SUCCESS);LRESULT ok=SendMessageTimeoutW(g_game,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,timeoutMs,&r);if(!ok){InterlockedIncrement64(&g_uiTimeouts);return false;}return true;
}
static bool post_f10() noexcept {
    if(!IsWindow(g_game))return false;const LPARAM down=(LPARAM)(1u|(0x44u<<16));const LPARAM up=(LPARAM)(1u|(0x44u<<16)|(1u<<30)|(1u<<31));
    const BOOL a=PostMessageW(g_game,WM_KEYDOWN,VK_F10,down),b=PostMessageW(g_game,WM_KEYUP,VK_F10,up);if(!a||!b){InterlockedIncrement64(&g_postFailures);logfmt("FAIL RC60 F10 post",GetLastError(),a,b,0);return false;}
    InterlockedIncrement64(&g_togglePosts);g_pendingTick=GetTickCount();InterlockedExchange(&g_pending,1);logline("RC60_F10_POSTED_TO_P1U46_WNDPROC");return true;
}
static bool presenter_native() noexcept {
    HWND p=FindWindowW(L"Win81USRPresenterV041",nullptr);if(!p||!IsWindow(p))return false;RECT c{};if(!GetClientRect(p,&c))return false;return (UINT)(c.right-c.left)==1920u&&(UINT)(c.bottom-c.top)==1080u;
}

static DWORD WINAPI Worker(LPVOID) noexcept {
    g_startTick=GetTickCount();
    logline("RC60_STARTUP_POLICY=PASSIVE_NO_HWND_MUTATION");
    logline("RC60_GAME_WNDPROC_OWNER=P1U46_ONLY");
    logline("RC60_GAME_GEOMETRY_OWNER=GAME_ONLY");
    logline("RC60_P1U46_PRIVATE_POLICY_WRITES=NONE");
    logline("RC60_PRESENTER_MUTATION=NONE");
    logline("RC60_DXGI_RESIZE=NONE");
    for(unsigned i=0;i<4000&&!InterlockedCompareExchange(&g_stop,0,0);++i){
        if(!game_valid()){if(g_base){g_game=*(HWND*)(g_base+kGameHwndRva);g_renderW=*(UINT*)(g_base+kRenderWRva);g_renderH=*(UINT*)(g_base+kRenderHRva);}Sleep(5);continue;}
        break;
    }
    if(!game_valid()){logline("FAIL RC60 game window/runtime geometry unavailable");return 20;}
    int p=read_pref();int r=route_usr();
    logfmt("RC60_START_STATE pref/usr/renderW/renderH",p,r,g_renderW,g_renderH);
    if(p==1&&r==1){logline("RC60_INITIAL_BORDERLESS_ZERO_TOUCH");if(presenter_native())logline("RC60_INITIAL_NATIVE_PRESENTER_1920x1080");}
    InterlockedExchange(&g_installed,1);
    unsigned uiGood=0;
    while(!InterlockedCompareExchange(&g_stop,0,0)&&IsWindow(g_game)){
        p=read_pref();r=route_usr();
        if(p>=0&&p!=g_lastPref){g_lastPref=p;InterlockedIncrement64(&g_prefChanges);logfmt("RC60_WINDOWSTYLE_OBSERVED",p,r,0,0);}
        if(p<0||r<0){Sleep(50);continue;}
        const int desiredUsr=(p==1)?1:0;
        if(r==desiredUsr){
            InterlockedIncrement64(&g_routeMatches);InterlockedExchange(&g_pending,0);uiGood=0;Sleep(50);continue;
        }
        // Mismatch means only P1U46's safe route switch is required. Never alter HWND geometry.
        if(ui_probe(60)){if(uiGood<20)++uiGood;}else uiGood=0;
        const DWORD age=GetTickCount()-g_startTick;
        const bool startupWindowed=(age<5000u&&p==0);
        if(uiGood>=3&&!startupWindowed){
            if(!InterlockedCompareExchange(&g_pending,0,0)){
                post_f10();uiGood=0;
            } else if(GetTickCount()-g_pendingTick>3000u){
                InterlockedIncrement64(&g_toggleRetries);InterlockedExchange(&g_pending,0);logline("RC60_F10_ROUTE_CONFIRM_TIMEOUT_RETRY");
            }
        }
        Sleep(50);
    }
    return 0;
}
}

struct PTARRC60BridgeState {
    UINT size,installed,desiredPref,usrActive,uiReady,pending,renderW,renderH,presenterNative;
    unsigned long long prefChanges,togglePosts,toggleRetries,postFailures,routeMatches,uiTimeouts;
};
extern "C" __declspec(dllexport) int WINAPI PTAR_RC60_QueryBridge(PTARRC60BridgeState* s){
    if(!s||s->size<sizeof(PTARRC60BridgeState))return -1;s->installed=(UINT)InterlockedCompareExchange(&g_installed,0,0);int p=read_pref(),r=route_usr();s->desiredPref=(p<0)?0xffffffffu:(UINT)p;s->usrActive=(r<0)?0xffffffffu:(UINT)r;s->uiReady=(UINT)InterlockedCompareExchange(&g_uiReady,0,0);s->pending=(UINT)InterlockedCompareExchange(&g_pending,0,0);s->renderW=g_renderW;s->renderH=g_renderH;s->presenterNative=presenter_native()?1u:0u;
    s->prefChanges=(unsigned long long)InterlockedCompareExchange64(&g_prefChanges,0,0);s->togglePosts=(unsigned long long)InterlockedCompareExchange64(&g_togglePosts,0,0);s->toggleRetries=(unsigned long long)InterlockedCompareExchange64(&g_toggleRetries,0,0);s->postFailures=(unsigned long long)InterlockedCompareExchange64(&g_postFailures,0,0);s->routeMatches=(unsigned long long)InterlockedCompareExchange64(&g_routeMatches,0,0);s->uiTimeouts=(unsigned long long)InterlockedCompareExchange64(&g_uiTimeouts,0,0);return 0;
}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND,HWND,UINT,UINT,UINT,UINT){return InterlockedCompareExchange(&g_installed,0,0)?1:0;}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_started,1,0)!=0)return 1;BYTE* base=nullptr;if(!runtime_ok(runtime,base)){logline("FAIL RC60 exact P1U46 guard");InterlockedExchange(&g_started,0);return -20;}
    g_runtime=runtime;g_base=base;g_game=*(HWND*)(base+kGameHwndRva);g_renderW=*(UINT*)(base+kRenderWRva);g_renderH=*(UINT*)(base+kRenderHRva);
    logline("RC60_PASSIVE_ROUTE_COORDINATOR_LOADED");RC41B_StartBootstrap(g_self,runtime);
    HANDLE th=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(!th){logline("FAIL RC60 worker create");InterlockedExchange(&g_started,0);return -22;}CloseHandle(th);return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){g_self=h;DisableThreadLibraryCalls(h);logline("RC60_CARRIER_LOADED");}else if(reason==DLL_PROCESS_DETACH)InterlockedExchange(&g_stop,1);return TRUE;}
