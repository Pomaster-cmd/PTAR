#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wchar.h>
#include <stdint.h>
#include <cstring>
#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"

// RC58 MODE BRIDGE
// ----------------
// P1U46 owns its native 1920x1080 presenter and the output-path switch.
// RC58 never subclasses/resizes/restyles that presenter and never calls DXGI
// ResizeBuffers. The only geometry policy left in the sidecar is a game-HWND
// Windowed guard, active exclusively while P1U46 is in GAME DIRECT mode.
//
// Windowed   = P1U46 GAME DIRECT + saved game window geometry.
// Borderless = P1U46 USR PRESENTER, untouched native presenter.
// Switching  = P1U46 F10 safe Present-boundary toggle.

namespace {
HMODULE g_self=nullptr,g_runtime=nullptr;
HWND g_game=nullptr;
WNDPROC g_next=nullptr;
volatile LONG g_started=0,g_installed=0,g_stop=0,g_internal=0,g_windowed=0,g_usrActive=1,g_keyLatch=0;
volatile LONG64 g_toWindowed=0,g_toBorderless=0,g_gameClamps=0,g_prefRequests=0,g_toggleForwards=0;
RECT g_savedOuter{};
LONG_PTR g_savedStyle=0,g_savedExStyle=0;
volatile LONG g_haveSaved=0;

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
static bool capture_window() noexcept {
    if(!IsWindow(g_game))return false;RECT r{};if(!GetWindowRect(g_game,&r))return false;LONG_PTR s=GetWindowLongPtrW(g_game,GWL_STYLE),e=GetWindowLongPtrW(g_game,GWL_EXSTYLE);if((s&WS_POPUP)!=0)return false;g_savedOuter=r;g_savedStyle=s;g_savedExStyle=e;InterlockedExchange(&g_haveSaved,1);logfmt("RC58_SAVED_WINDOW",r.left,r.top,r.right,r.bottom);return true;
}
static void restore_window() noexcept {
    if(!IsWindow(g_game)||!InterlockedCompareExchange(&g_haveSaved,0,0))return;InterlockedExchange(&g_internal,1);
    if(GetWindowLongPtrW(g_game,GWL_STYLE)!=g_savedStyle)SetWindowLongPtrW(g_game,GWL_STYLE,g_savedStyle);
    if(GetWindowLongPtrW(g_game,GWL_EXSTYLE)!=g_savedExStyle)SetWindowLongPtrW(g_game,GWL_EXSTYLE,g_savedExStyle);
    const int w=g_savedOuter.right-g_savedOuter.left,h=g_savedOuter.bottom-g_savedOuter.top;
    SetWindowPos(g_game,nullptr,g_savedOuter.left,g_savedOuter.top,w,h,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    InterlockedExchange(&g_internal,0);InterlockedIncrement64(&g_gameClamps);
}
static void forward_f10() noexcept {
    if(!IsWindow(g_game)||!g_next)return;const LPARAM down=(LPARAM)(1u|(0x44u<<16));const LPARAM up=(LPARAM)(1u|(0x44u<<16)|(1u<<30)|(1u<<31));
    call_next(g_game,WM_KEYDOWN,VK_F10,down);call_next(g_game,WM_KEYUP,VK_F10,up);InterlockedIncrement64(&g_toggleForwards);
}
static void request_windowed(const char* why) noexcept {
    if(InterlockedExchange(&g_windowed,1)==1 && !InterlockedCompareExchange(&g_usrActive,0,0)){restore_window();return;}
    if(InterlockedExchange(&g_usrActive,0)==1){logline(why);forward_f10();InterlockedIncrement64(&g_toWindowed);}
    // P1U46 commits F10 on Present and then performs input-only recovery. Keep
    // the guard active immediately; any resulting WINDOWPOS/STYLE request is
    // clamped. A short delayed restore covers recovery paths that bypass a
    // mutable WINDOWPOS message.
    Sleep(40);restore_window();
}
static void request_borderless(const char* why) noexcept {
    if(InterlockedExchange(&g_windowed,0)==0 && InterlockedCompareExchange(&g_usrActive,0,0))return;
    if(InterlockedExchange(&g_usrActive,1)==0){logline(why);forward_f10();InterlockedIncrement64(&g_toBorderless);}
}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;
    if(!internal && w==VK_F10 && (m==WM_KEYDOWN||m==WM_SYSKEYDOWN)){
        if(InterlockedExchange(&g_keyLatch,1)==0){
            // Plain F10 remains the public switch. We track the same state before
            // forwarding so game-only geometry is protected on the OFF edge.
            if(InterlockedCompareExchange(&g_usrActive,0,0)){InterlockedExchange(&g_windowed,1);InterlockedExchange(&g_usrActive,0);InterlockedIncrement64(&g_toWindowed);logline("RC58_F10_REQUEST=WINDOWED_GAME_DIRECT");}
            else {InterlockedExchange(&g_windowed,0);InterlockedExchange(&g_usrActive,1);InterlockedIncrement64(&g_toBorderless);logline("RC58_F10_REQUEST=BORDERLESS_USR_PRESENTER");}
        }
        LRESULT r=call_next(h,m,w,l);if(InterlockedCompareExchange(&g_windowed,0,0)){Sleep(1);restore_window();}return r;
    }
    if(!internal && w==VK_F10 && (m==WM_KEYUP||m==WM_SYSKEYUP)){InterlockedExchange(&g_keyLatch,0);return call_next(h,m,w,l);}
    if(!internal && InterlockedCompareExchange(&g_windowed,0,0) && InterlockedCompareExchange(&g_haveSaved,0,0)){
        if(m==WM_WINDOWPOSCHANGING && l){WINDOWPOS* p=(WINDOWPOS*)l;p->x=g_savedOuter.left;p->y=g_savedOuter.top;p->cx=g_savedOuter.right-g_savedOuter.left;p->cy=g_savedOuter.bottom-g_savedOuter.top;p->flags&=~(SWP_NOMOVE|SWP_NOSIZE);InterlockedIncrement64(&g_gameClamps);}
        else if(m==WM_STYLECHANGING && l && (w==GWL_STYLE||w==GWL_EXSTYLE)){STYLESTRUCT* ss=(STYLESTRUCT*)l;const LONG_PTR wanted=(w==GWL_STYLE)?g_savedStyle:g_savedExStyle;ss->styleNew=static_cast<DWORD>(static_cast<ULONG_PTR>(wanted));InterlockedIncrement64(&g_gameClamps);}
    }
    return call_next(h,m,w,l);
}

static DWORD WINAPI Worker(LPVOID) noexcept {
    // Allow P1U46 to finish its current creation call before subclassing.
    for(unsigned i=0;i<200 && !InterlockedCompareExchange(&g_stop,0,0);++i){if(IsWindow(g_game)&&get_proc(g_game)){break;}Sleep(10);}if(!IsWindow(g_game)){logline("FAIL RC58 game window unavailable");return 20;}
    capture_window();g_next=(WNDPROC)get_proc(g_game);if(!g_next||!set_proc(g_game,GameProc)){logline("FAIL RC58 game subclass");return 21;}InterlockedExchange(&g_installed,1);logline("RC58_MODE_BRIDGE_INSTALLED");
    int last=-2;
    while(!InterlockedCompareExchange(&g_stop,0,0)&&IsWindow(g_game)){
        int p=read_pref();if(p>=0&&p!=last){last=p;InterlockedIncrement64(&g_prefRequests);if(p==0)request_windowed("RC58_PREF_REQUEST=WINDOWED_GAME_DIRECT");else if(p==1)request_borderless("RC58_PREF_REQUEST=BORDERLESS_USR_PRESENTER");}
        Sleep(100);
    }
    return 0;
}
}

struct PTARRC58BridgeState {UINT size,installed,windowed,usrActive,haveSaved;unsigned long long toWindowed,toBorderless,gameClamps,prefRequests,toggleForwards;LONG savedLeft,savedTop,savedRight,savedBottom;};
extern "C" __declspec(dllexport) int WINAPI PTAR_RC58_QueryBridge(PTARRC58BridgeState* s){if(!s||s->size<sizeof(PTARRC58BridgeState))return -1;s->installed=(UINT)InterlockedCompareExchange(&g_installed,0,0);s->windowed=(UINT)InterlockedCompareExchange(&g_windowed,0,0);s->usrActive=(UINT)InterlockedCompareExchange(&g_usrActive,0,0);s->haveSaved=(UINT)InterlockedCompareExchange(&g_haveSaved,0,0);s->toWindowed=(unsigned long long)InterlockedCompareExchange64(&g_toWindowed,0,0);s->toBorderless=(unsigned long long)InterlockedCompareExchange64(&g_toBorderless,0,0);s->gameClamps=(unsigned long long)InterlockedCompareExchange64(&g_gameClamps,0,0);s->prefRequests=(unsigned long long)InterlockedCompareExchange64(&g_prefRequests,0,0);s->toggleForwards=(unsigned long long)InterlockedCompareExchange64(&g_toggleForwards,0,0);s->savedLeft=g_savedOuter.left;s->savedTop=g_savedOuter.top;s->savedRight=g_savedOuter.right;s->savedBottom=g_savedOuter.bottom;return 0;}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND,HWND,UINT,UINT,UINT,UINT){return InterlockedCompareExchange(&g_installed,0,0)?1:0;}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_started,1,0)!=0)return 1;BYTE* base=nullptr;if(!runtime_ok(runtime,base)){logline("FAIL RC58 exact P1U46 guard");InterlockedExchange(&g_started,0);return -20;}g_runtime=runtime;g_game=*(HWND*)(base+0x02C3FAC0u);if(!g_game||!IsWindow(g_game)){logline("FAIL RC58 game HWND");InterlockedExchange(&g_started,0);return -21;}
    logline("RC58_MODE_BRIDGE_AUTOSTART=PASS");logline("RC58_PRESENTER_MUTATION=NONE");logline("RC58_DXGI_RESIZE=NONE");logline("RC58_WINDOWED=P1U46_GAME_DIRECT_PLUS_GAME_GUARD");logline("RC58_BORDERLESS=P1U46_USR_PRESENTER_NATIVE");
    if(g_self)RC41B_StartBootstrap(g_self,runtime);HANDLE th=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(!th){logline("FAIL RC58 worker create");return -22;}CloseHandle(th);return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){g_self=h;DisableThreadLibraryCalls(h);logline("RC58_MODE_BRIDGE_CARRIER_LOADED");}else if(reason==DLL_PROCESS_DETACH){InterlockedExchange(&g_stop,1);if(IsWindow(g_game)&&g_next&&get_proc(g_game)==(LONG_PTR)GameProc)set_proc(g_game,g_next);}return TRUE;}