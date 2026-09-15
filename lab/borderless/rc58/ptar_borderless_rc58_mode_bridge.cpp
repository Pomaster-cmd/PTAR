#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wchar.h>
#include <stdint.h>
#include <cstring>
#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"

// RC58 MODE BRIDGE
// ----------------
// P1U46 exclusively owns the 1920x1080 USR presenter and its safe Present-boundary
// GAME DIRECT <-> USR PRESENTER switch. RC58 never mutates presenter geometry,
// presenter style, or presenter DXGI buffers.
//
// Windowed   = P1U46 GAME DIRECT + exact render-size game client (1280x720 here).
// Borderless = P1U46 USR PRESENTER at native output (1920x1080 here).
// Switching  = P1U46 F10 contract, always invoked on the game UI thread.
//
// The sidecar only owns the game HWND while Windowed is requested. Registry mode
// changes are marshalled to the game UI thread through a private registered message;
// no foreign-thread WndProc invocation is permitted.

namespace {
HMODULE g_self=nullptr,g_runtime=nullptr;
HWND g_game=nullptr;
WNDPROC g_next=nullptr;
UINT g_renderW=0,g_renderH=0;
UINT g_controlMessage=0;
volatile LONG g_started=0,g_installed=0,g_stop=0,g_internal=0,g_windowed=0,g_usrActive=1,g_keyLatch=0;
volatile LONG64 g_toWindowed=0,g_toBorderless=0,g_gameClamps=0,g_prefRequests=0,g_toggleForwards=0,g_restoreFailures=0;
RECT g_savedOuter{};
LONG_PTR g_savedStyle=0,g_savedExStyle=0;
volatile LONG g_haveSaved=0;

enum : WPARAM { RC58_CTL_TOGGLE_TO_WINDOWED=1, RC58_CTL_TOGGLE_TO_BORDERLESS=2, RC58_CTL_RESTORE_WINDOW=3 };

static void logline(const char* s) noexcept {
    if(!g_self||!s)return;wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(g_self,path,MAX_PATH))return;wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ptar_borderless_rc58.log");HANDLE h=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME st{};GetLocalTime(&st);char line[1024]{};int n=wsprintfA(line,"[%02u:%02u:%02u.%03u] %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,s);DWORD wr=0;WriteFile(h,line,(DWORD)n,&wr,nullptr);CloseHandle(h);
}
static void logfmt(const char* tag,LONG_PTR a,LONG_PTR b=0,LONG_PTR c=0,LONG_PTR d=0) noexcept {char x[768]{};wsprintfA(x,"%s %I64d %I64d %I64d %I64d",tag,(long long)a,(long long)b,(long long)c,(long long)d);logline(x);}
static LONG_PTR get_proc(HWND h) noexcept {return IsWindowUnicode(h)?GetWindowLongPtrW(h,GWLP_WNDPROC):GetWindowLongPtrA(h,GWLP_WNDPROC);}
static LONG_PTR set_proc(HWND h,WNDPROC p) noexcept {return IsWindowUnicode(h)?SetWindowLongPtrW(h,GWLP_WNDPROC,(LONG_PTR)p):SetWindowLongPtrA(h,GWLP_WNDPROC,(LONG_PTR)p);}
static LRESULT call_next(HWND h,UINT m,WPARAM w,LPARAM l) noexcept {if(!g_next)return IsWindowUnicode(h)?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);return IsWindowUnicode(h)?CallWindowProcW(g_next,h,m,w,l):CallWindowProcA(g_next,h,m,w,l);}
static int read_pref() noexcept {HKEY k=nullptr;const wchar_t* p=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";if(RegOpenKeyExW(HKEY_CURRENT_USER,p,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return -1;DWORD t=0,v=0,s=sizeof(v);LONG r=RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&v,&s);RegCloseKey(k);return (r==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(v))?(int)v:-1;}
static bool runtime_ok(HMODULE runtime,BYTE*& base) noexcept {
    base=nullptr;if(!runtime)return false;base=(BYTE*)runtime;auto* dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return false;auto* nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->OptionalHeader.SizeOfImage<0x02C7E010u)return false;
    FARPROC exported=GetProcAddress(runtime,"D3D11CreateDeviceAndSwapChain");return exported==(FARPROC)(base+0x000021C0u);
}
static bool capture_window_target() noexcept {
    if(!IsWindow(g_game)||!g_renderW||!g_renderH)return false;RECT current{};if(!GetWindowRect(g_game,&current))return false;
    const LONG_PTR s=GetWindowLongPtrW(g_game,GWL_STYLE),e=GetWindowLongPtrW(g_game,GWL_EXSTYLE);if((s&WS_POPUP)!=0)return false;
    RECT wanted{0,0,(LONG)g_renderW,(LONG)g_renderH};if(!AdjustWindowRectEx(&wanted,(DWORD)(ULONG_PTR)s,GetMenu(g_game)!=nullptr,(DWORD)(ULONG_PTR)e))return false;
    const LONG ow=wanted.right-wanted.left,oh=wanted.bottom-wanted.top;if(ow<=0||oh<=0)return false;
    g_savedOuter={current.left,current.top,current.left+ow,current.top+oh};g_savedStyle=s;g_savedExStyle=e;InterlockedExchange(&g_haveSaved,1);
    logfmt("RC58_SAVED_WINDOW_TARGET",g_savedOuter.left,g_savedOuter.top,g_savedOuter.right,g_savedOuter.bottom);logfmt("RC58_SAVED_CLIENT_TARGET",g_renderW,g_renderH,0,0);return true;
}
static bool current_window_matches_target() noexcept {
    if(!IsWindow(g_game)||!InterlockedCompareExchange(&g_haveSaved,0,0))return false;RECT r{},c{};if(!GetWindowRect(g_game,&r)||!GetClientRect(g_game,&c))return false;
    return GetWindowLongPtrW(g_game,GWL_STYLE)==g_savedStyle&&GetWindowLongPtrW(g_game,GWL_EXSTYLE)==g_savedExStyle&&
           r.left==g_savedOuter.left&&r.top==g_savedOuter.top&&r.right==g_savedOuter.right&&r.bottom==g_savedOuter.bottom&&
           (UINT)(c.right-c.left)==g_renderW&&(UINT)(c.bottom-c.top)==g_renderH;
}
static void restore_window_ui() noexcept {
    if(!IsWindow(g_game)||!InterlockedCompareExchange(&g_haveSaved,0,0))return;if(current_window_matches_target())return;
    if(GetWindowLongPtrW(g_game,GWL_STYLE)!=g_savedStyle)SetWindowLongPtrW(g_game,GWL_STYLE,g_savedStyle);
    if(GetWindowLongPtrW(g_game,GWL_EXSTYLE)!=g_savedExStyle)SetWindowLongPtrW(g_game,GWL_EXSTYLE,g_savedExStyle);
    const int ow=g_savedOuter.right-g_savedOuter.left,oh=g_savedOuter.bottom-g_savedOuter.top;
    if(!SetWindowPos(g_game,nullptr,g_savedOuter.left,g_savedOuter.top,ow,oh,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW)){InterlockedIncrement64(&g_restoreFailures);logfmt("FAIL RC58 SetWindowPos restore",GetLastError(),ow,oh,0);return;}
    RECT c{};if(!GetClientRect(g_game,&c)||(UINT)(c.right-c.left)!=g_renderW||(UINT)(c.bottom-c.top)!=g_renderH){InterlockedIncrement64(&g_restoreFailures);logfmt("FAIL RC58 restored client",c.right-c.left,c.bottom-c.top,g_renderW,g_renderH);return;}
    InterlockedIncrement64(&g_gameClamps);
}
static void forward_f10_ui() noexcept {
    if(!IsWindow(g_game)||!g_next)return;const LPARAM down=(LPARAM)(1u|(0x44u<<16));const LPARAM up=(LPARAM)(1u|(0x44u<<16)|(1u<<30)|(1u<<31));
    call_next(g_game,WM_KEYDOWN,VK_F10,down);call_next(g_game,WM_KEYUP,VK_F10,up);InterlockedIncrement64(&g_toggleForwards);
}
static bool send_control(WPARAM command) noexcept {
    if(!IsWindow(g_game)||!g_controlMessage)return false;DWORD_PTR result=0;return SendMessageTimeoutW(g_game,g_controlMessage,command,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result)!=0;
}
static void request_windowed(const char* why) noexcept {
    if(InterlockedExchange(&g_windowed,1)==1&&!InterlockedCompareExchange(&g_usrActive,0,0)){send_control(RC58_CTL_RESTORE_WINDOW);return;}
    if(InterlockedExchange(&g_usrActive,0)==1){logline(why);if(send_control(RC58_CTL_TOGGLE_TO_WINDOWED))InterlockedIncrement64(&g_toWindowed);else {InterlockedIncrement64(&g_restoreFailures);logline("FAIL RC58 windowed control dispatch");}}
    send_control(RC58_CTL_RESTORE_WINDOW);
}
static void request_borderless(const char* why) noexcept {
    if(InterlockedExchange(&g_windowed,0)==0&&InterlockedCompareExchange(&g_usrActive,0,0))return;
    if(InterlockedExchange(&g_usrActive,1)==0){logline(why);if(send_control(RC58_CTL_TOGGLE_TO_BORDERLESS))InterlockedIncrement64(&g_toBorderless);else {InterlockedIncrement64(&g_restoreFailures);logline("FAIL RC58 borderless control dispatch");}}
}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(g_controlMessage&&m==g_controlMessage){
        InterlockedExchange(&g_internal,1);
        if(w==RC58_CTL_TOGGLE_TO_WINDOWED||w==RC58_CTL_TOGGLE_TO_BORDERLESS)forward_f10_ui();
        if(w==RC58_CTL_RESTORE_WINDOW)restore_window_ui();
        InterlockedExchange(&g_internal,0);return 1;
    }
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;
    if(!internal&&w==VK_F10&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)){
        if(InterlockedExchange(&g_keyLatch,1)==0){
            if(InterlockedCompareExchange(&g_usrActive,0,0)){InterlockedExchange(&g_windowed,1);InterlockedExchange(&g_usrActive,0);InterlockedIncrement64(&g_toWindowed);logline("RC58_F10_REQUEST=WINDOWED_GAME_DIRECT");}
            else {InterlockedExchange(&g_windowed,0);InterlockedExchange(&g_usrActive,1);InterlockedIncrement64(&g_toBorderless);logline("RC58_F10_REQUEST=BORDERLESS_USR_PRESENTER");}
        }
        return call_next(h,m,w,l);
    }
    if(!internal&&w==VK_F10&&(m==WM_KEYUP||m==WM_SYSKEYUP)){InterlockedExchange(&g_keyLatch,0);LRESULT r=call_next(h,m,w,l);if(InterlockedCompareExchange(&g_windowed,0,0)&&g_controlMessage)PostMessageW(g_game,g_controlMessage,RC58_CTL_RESTORE_WINDOW,0);return r;}
    if(!internal&&InterlockedCompareExchange(&g_windowed,0,0)&&InterlockedCompareExchange(&g_haveSaved,0,0)){
        if(m==WM_WINDOWPOSCHANGING&&l){WINDOWPOS* p=(WINDOWPOS*)l;p->x=g_savedOuter.left;p->y=g_savedOuter.top;p->cx=g_savedOuter.right-g_savedOuter.left;p->cy=g_savedOuter.bottom-g_savedOuter.top;p->flags&=~(SWP_NOMOVE|SWP_NOSIZE);InterlockedIncrement64(&g_gameClamps);}
        else if(m==WM_STYLECHANGING&&l&&(w==GWL_STYLE||w==GWL_EXSTYLE)){STYLESTRUCT* ss=(STYLESTRUCT*)l;const LONG_PTR wanted=(w==GWL_STYLE)?g_savedStyle:g_savedExStyle;ss->styleNew=static_cast<DWORD>(static_cast<ULONG_PTR>(wanted));InterlockedIncrement64(&g_gameClamps);}
    }
    return call_next(h,m,w,l);
}

static DWORD WINAPI Worker(LPVOID) noexcept {
    for(unsigned i=0;i<200&&!InterlockedCompareExchange(&g_stop,0,0);++i){if(IsWindow(g_game)&&get_proc(g_game))break;Sleep(10);}if(!IsWindow(g_game)){logline("FAIL RC58 game window unavailable");return 20;}
    if(!capture_window_target()){logline("FAIL RC58 capture exact window target");return 21;}
    g_controlMessage=RegisterWindowMessageW(L"PTAR_RC58_MODE_BRIDGE_CONTROL_20260915");if(!g_controlMessage){logline("FAIL RC58 control message registration");return 22;}
    g_next=(WNDPROC)get_proc(g_game);if(!g_next||!set_proc(g_game,GameProc)){logline("FAIL RC58 game subclass");return 23;}InterlockedExchange(&g_installed,1);logline("RC58_MODE_BRIDGE_INSTALLED");
    int last=-2;unsigned restoreTick=0;
    while(!InterlockedCompareExchange(&g_stop,0,0)&&IsWindow(g_game)){
        int p=read_pref();if(p>=0&&p!=last){last=p;InterlockedIncrement64(&g_prefRequests);if(p==0)request_windowed("RC58_PREF_REQUEST=WINDOWED_GAME_DIRECT");else if(p==1)request_borderless("RC58_PREF_REQUEST=BORDERLESS_USR_PRESENTER");}
        if(InterlockedCompareExchange(&g_windowed,0,0)&&(++restoreTick%2u)==0u&&!current_window_matches_target())send_control(RC58_CTL_RESTORE_WINDOW);
        Sleep(50);
    }
    return 0;
}
}

struct PTARRC58BridgeState {UINT size,installed,windowed,usrActive,haveSaved;unsigned long long toWindowed,toBorderless,gameClamps,prefRequests,toggleForwards,restoreFailures;LONG savedLeft,savedTop,savedRight,savedBottom;UINT renderW,renderH;};
extern "C" __declspec(dllexport) int WINAPI PTAR_RC58_QueryBridge(PTARRC58BridgeState* s){if(!s||s->size<sizeof(PTARRC58BridgeState))return -1;s->installed=(UINT)InterlockedCompareExchange(&g_installed,0,0);s->windowed=(UINT)InterlockedCompareExchange(&g_windowed,0,0);s->usrActive=(UINT)InterlockedCompareExchange(&g_usrActive,0,0);s->haveSaved=(UINT)InterlockedCompareExchange(&g_haveSaved,0,0);s->toWindowed=(unsigned long long)InterlockedCompareExchange64(&g_toWindowed,0,0);s->toBorderless=(unsigned long long)InterlockedCompareExchange64(&g_toBorderless,0,0);s->gameClamps=(unsigned long long)InterlockedCompareExchange64(&g_gameClamps,0,0);s->prefRequests=(unsigned long long)InterlockedCompareExchange64(&g_prefRequests,0,0);s->toggleForwards=(unsigned long long)InterlockedCompareExchange64(&g_toggleForwards,0,0);s->restoreFailures=(unsigned long long)InterlockedCompareExchange64(&g_restoreFailures,0,0);s->savedLeft=g_savedOuter.left;s->savedTop=g_savedOuter.top;s->savedRight=g_savedOuter.right;s->savedBottom=g_savedOuter.bottom;s->renderW=g_renderW;s->renderH=g_renderH;return 0;}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND,HWND,UINT,UINT,UINT,UINT){return InterlockedCompareExchange(&g_installed,0,0)?1:0;}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_started,1,0)!=0)return 1;BYTE* base=nullptr;if(!runtime_ok(runtime,base)){logline("FAIL RC58 exact P1U46 guard");InterlockedExchange(&g_started,0);return -20;}g_runtime=runtime;g_game=*(HWND*)(base+0x02C3FAC0u);g_renderW=*(UINT*)(base+0x02C3FB78u);g_renderH=*(UINT*)(base+0x02C3FB7Cu);if(!g_game||!IsWindow(g_game)||!g_renderW||!g_renderH){logfmt("FAIL RC58 game/runtime geometry",(LONG_PTR)g_game,g_renderW,g_renderH,0);InterlockedExchange(&g_started,0);return -21;}
    logline("RC58_MODE_BRIDGE_AUTOSTART=PASS");logline("RC58_PRESENTER_MUTATION=NONE");logline("RC58_DXGI_RESIZE=NONE");logline("RC58_FOREIGN_THREAD_WNDPROC_CALLS=NONE");logline("RC58_WINDOWED=P1U46_GAME_DIRECT_PLUS_EXACT_GAME_CLIENT");logline("RC58_BORDERLESS=P1U46_USR_PRESENTER_NATIVE");
    if(g_self)RC41B_StartBootstrap(g_self,runtime);HANDLE th=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(!th){logline("FAIL RC58 worker create");return -22;}CloseHandle(th);return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){g_self=h;DisableThreadLibraryCalls(h);logline("RC58_MODE_BRIDGE_CARRIER_LOADED");}else if(reason==DLL_PROCESS_DETACH){InterlockedExchange(&g_stop,1);if(IsWindow(g_game)&&g_next&&get_proc(g_game)==(LONG_PTR)GameProc)set_proc(g_game,g_next);}return TRUE;}