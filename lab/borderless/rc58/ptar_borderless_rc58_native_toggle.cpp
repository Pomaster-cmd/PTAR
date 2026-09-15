#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wchar.h>
#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"

// RC58 native-toggle architecture:
// - this sidecar never subclasses or resizes the game HWND;
// - this sidecar never subclasses, resizes, hides or restyles the P1U46 presenter;
// - P1U46 remains the sole owner of its native 1920x1080 presenter;
// - Windowed <-> Borderless is P1U46's own field-tested F10 safe-boundary switch:
//     GAME DIRECT  = Windowed game output
//     USR PRESENTER = native Borderless reconstruction output
// - this carrier only bootstraps the byte-identical RC55 raster sidecar.

namespace {
HMODULE g_self=nullptr;
volatile LONG g_started=0;
volatile LONG g_bootstrapDispatched=0;
HMODULE g_runtime=nullptr;

static void logline(const char* s) noexcept {
    if(!g_self||!s)return;
    wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(g_self,path,MAX_PATH))return;
    wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"ptar_borderless_rc58.log");
    HANDLE h=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME st{};GetLocalTime(&st);char line[1024]{};
    int n=wsprintfA(line,"[%02u:%02u:%02u.%03u] %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,s);
    DWORD wr=0;WriteFile(h,line,(DWORD)n,&wr,nullptr);CloseHandle(h);
}
}

struct PTARRC58NativeToggleState {
    UINT size;
    UINT started;
    UINT bootstrapDispatched;
    ULONG_PTR runtime;
};

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(!runtime){logline("FAIL RC58 native-toggle AutoStart runtime=null");return -10;}
    if(InterlockedCompareExchange(&g_started,1,0)!=0)return 1;
    g_runtime=runtime;
    logline("RC58_NATIVE_TOGGLE_AUTOSTART=PASS");
    logline("RC58_WINDOWED=P1U46_GAME_DIRECT");
    logline("RC58_BORDERLESS=P1U46_USR_PRESENTER");
    logline("RC58_MODE_SWITCH=P1U46_F10_SAFE_BOUNDARY");
    logline("RC58_GAME_HWND_MUTATION=NONE");
    logline("RC58_PRESENTER_GEOMETRY_MUTATION=NONE");
    if(g_self){RC41B_StartBootstrap(g_self,runtime);InterlockedExchange(&g_bootstrapDispatched,1);logline("RC58_RC55_BOOTSTRAP_DISPATCHED");}
    else logline("WARN RC58 self module unavailable; raster bootstrap skipped");
    return 0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND,HWND,UINT,UINT,UINT,UINT){
    // Compatibility export only. P1U46 owns all display geometry in RC58.
    return InterlockedCompareExchange(&g_started,0,0)?1:0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_RC58_QueryNativeToggle(PTARRC58NativeToggleState* s){
    if(!s||s->size<sizeof(PTARRC58NativeToggleState))return -1;
    s->started=(UINT)InterlockedCompareExchange(&g_started,0,0);
    s->bootstrapDispatched=(UINT)InterlockedCompareExchange(&g_bootstrapDispatched,0,0);
    s->runtime=(ULONG_PTR)g_runtime;
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){g_self=h;DisableThreadLibraryCalls(h);logline("RC58_NATIVE_TOGGLE_CARRIER_LOADED");}
    return TRUE;
}
