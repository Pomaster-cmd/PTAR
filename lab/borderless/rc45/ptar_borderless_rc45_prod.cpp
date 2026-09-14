#include "../rc38/ptar_borderless_rc38.cpp"
#include "../rc43/ptar_borderless_mode_policy.h"
#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"
#include <stddef.h>

static volatile LONG g_rc45Starting=0;
static volatile LONG g_rc45Installed=0;
static volatile LONG g_rc45Borderless=0;
static volatile LONG64 g_rc45ToWindowed=0;
static volatile LONG64 g_rc45ToBorderless=0;
static volatile LONG64 g_rc45PresenterFollows=0;
static HMODULE g_rc45Runtime=nullptr;
static RECT g_rc45Monitor{};
static LONG_PTR g_rc45SavedWindowStyle=0;
static LONG_PTR g_rc45SavedWindowExStyle=0;
static RECT g_rc45SavedWindowRect{};
static volatile LONG g_rc45HaveSavedWindow=0;
static UINT g_rc45SyncMessage=0;
static volatile LONG g_rc45PreferenceWorkerStarted=0;
static int g_rc45LastWindowStylePreference=-2;

struct PTARBorderlessModeState {
    UINT size;
    UINT installed;
    UINT borderlessActive;
    UINT presenterVisible;
    ULONG_PTR gameStyle;
    ULONG_PTR presenterStyle;
    unsigned long long transitionsToWindowed;
    unsigned long long transitionsToBorderless;
    LONG targetLeft;
    LONG targetTop;
    LONG targetRight;
    LONG targetBottom;
    LONG windowStylePreference;
    unsigned long long presenterFollows;
};

static void rc45_starting_clear() noexcept { InterlockedExchange(&g_rc45Starting,0); }

static int rc45_read_windowstyle_preference() noexcept {
    HKEY key=nullptr;
    const wchar_t* path=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
    if(RegOpenKeyExW(HKEY_CURRENT_USER,path,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return -1;
    DWORD type=0,value=0,size=sizeof(value);
    const LONG rc=RegQueryValueExW(key,L"WindowStyle",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size);
    RegCloseKey(key);
    if(rc!=ERROR_SUCCESS || type!=REG_DWORD || size!=sizeof(value)) return -1;
    return static_cast<int>(value);
}

static void rc45_log_style(const char* tag,LONG_PTR oldStyle,LONG_PTR newStyle) noexcept {
    logfmt(tag,oldStyle,newStyle,InterlockedCompareExchange(&g_rc45Borderless,0,0),InterlockedCompareExchange(&g_rc45Installed,0,0));
}

static bool rc45_window_client_screen_rect(RECT& out) noexcept {
    if(!IsWindow(g_game) || IsIconic(g_game)) return false;
    RECT c{};
    if(!GetClientRect(g_game,&c)) return false;
    POINT a{c.left,c.top},b{c.right,c.bottom};
    if(!ClientToScreen(g_game,&a) || !ClientToScreen(g_game,&b)) return false;
    if(b.x<=a.x || b.y<=a.y) return false;
    out={a.x,a.y,b.x,b.y};
    return true;
}

static void rc45_capture_windowed_state() noexcept {
    if(!IsWindow(g_game)) return;
    const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);
    if(!ptar_rc43::is_windowed_request(style)) return;
    RECT r{};
    if(!GetWindowRect(g_game,&r)) return;
    g_rc45SavedWindowStyle=style;
    g_rc45SavedWindowExStyle=GetWindowLongPtrW(g_game,GWL_EXSTYLE);
    g_rc45SavedWindowRect=r;
    InterlockedExchange(&g_rc45HaveSavedWindow,1);
    logfmt("RC45_SAVED_WINDOW_RECT",r.left,r.top,r.right,r.bottom);
}

static void rc45_set_presenter_window_style() noexcept {
    if(!IsWindow(g_presenter)) return;
    LONG_PTR ps=GetWindowLongPtrW(g_presenter,GWL_STYLE);
    LONG_PTR pes=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);
    const LONG_PTR pds=(ps & (WS_VISIBLE|WS_DISABLED|WS_CLIPSIBLINGS|WS_CLIPCHILDREN)) | WS_POPUP;
    const LONG_PTR pde=(pes | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~(LONG_PTR)(WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_APPWINDOW);
    if(ps!=pds) SetWindowLongPtrW(g_presenter,GWL_STYLE,pds);
    if(pes!=pde) SetWindowLongPtrW(g_presenter,GWL_EXSTYLE,pde);
}

static void rc45_follow_windowed_presenter(bool raiseZ) noexcept {
    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)) return;
    RECT target{};
    if(!rc45_window_client_screen_rect(target)){
        if(IsWindow(g_presenter)) ShowWindow(g_presenter,SW_HIDE);
        return;
    }
    g_monitor=target;
    InterlockedExchange(&g_internal,1);
    rc45_set_presenter_window_style();
    const UINT flags=SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW|(raiseZ?0u:SWP_NOZORDER);
    const HWND insertAfter=raiseZ?HWND_TOP:nullptr;
    SetWindowPos(g_presenter,insertAfter,target.left,target.top,target.right-target.left,target.bottom-target.top,flags);
    InterlockedExchange(&g_internal,0);
    InterlockedIncrement64(&g_rc45PresenterFollows);
}

static void rc45_restore_saved_window() noexcept {
    if(!IsWindow(g_game)) return;
    if(!InterlockedCompareExchange(&g_rc45HaveSavedWindow,0,0)) return;
    const RECT r=g_rc45SavedWindowRect;
    InterlockedExchange(&g_internal,1);
    SetWindowLongPtrW(g_game,GWL_STYLE,g_rc45SavedWindowStyle);
    SetWindowLongPtrW(g_game,GWL_EXSTYLE,g_rc45SavedWindowExStyle);
    SetWindowPos(g_game,HWND_TOP,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    InterlockedExchange(&g_internal,0);
}

static void rc45_enter_windowed(bool restoreSaved,const char* reason) noexcept {
    const bool wasBorderless=InterlockedExchange(&g_rc45Borderless,0)!=0;
    InterlockedExchange(&g_active,1);
    InterlockedExchange(&g_logicalCapture,0);
    if(GetCapture()==g_presenter) ReleaseCapture();
    ClipCursor(nullptr);
    if(restoreSaved) rc45_restore_saved_window();
    rc45_follow_windowed_presenter(true);
    if(wasBorderless) InterlockedIncrement64(&g_rc45ToWindowed);
    logline(reason);
}

static void rc45_enter_borderless(const char* reason) noexcept {
    const bool wasBorderless=InterlockedCompareExchange(&g_rc45Borderless,0,0)!=0;
    if(!wasBorderless) rc45_capture_windowed_state();
    InterlockedExchange(&g_rc45Borderless,1);
    InterlockedExchange(&g_active,1);
    g_monitor=g_rc45Monitor;
    enforce_geometry();
    if(!wasBorderless) InterlockedIncrement64(&g_rc45ToBorderless);
    logline(reason);
}

static void rc45_apply_preference_on_game_thread(int preference) noexcept {
    if(preference==1){
        if(!InterlockedCompareExchange(&g_rc45Borderless,0,0))
            rc45_enter_borderless("RC45_PREF_MODE=BORDERLESS");
        else {g_monitor=g_rc45Monitor;enforce_geometry();}
    } else if(preference>=0){
        if(InterlockedCompareExchange(&g_rc45Borderless,0,0))
            rc45_enter_windowed(true,"RC45_PREF_MODE=WINDOWED");
        else rc45_follow_windowed_presenter(false);
    }
}

static LRESULT CALLBACK RC45GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;

    if(g_rc45SyncMessage && m==g_rc45SyncMessage){
        rc45_apply_preference_on_game_thread(static_cast<int>(w));
        return 0;
    }

    if(!internal && m==WM_STYLECHANGING && l && w==GWL_STYLE){
        STYLESTRUCT* ss=reinterpret_cast<STYLESTRUCT*>(l);
        rc45_log_style("RC45_STYLE_CHANGING",ss->styleOld,ss->styleNew);
        const ptar_rc43::RequestedWindowMode requested=ptar_rc43::classify_style(ss->styleNew);
        if(requested==ptar_rc43::RequestedWindowMode::Windowed){
            const bool wasBorderless=InterlockedExchange(&g_rc45Borderless,0)!=0;
            InterlockedExchange(&g_active,1);
            if(wasBorderless) InterlockedIncrement64(&g_rc45ToWindowed);
            const LRESULT r=call_next(g_gameNext,h,m,w,l);
            return r;
        }
        if(requested==ptar_rc43::RequestedWindowMode::Borderless){
            const bool wasBorderless=InterlockedCompareExchange(&g_rc45Borderless,0,0)!=0;
            if(!wasBorderless) rc45_capture_windowed_state();
            InterlockedExchange(&g_rc45Borderless,1);
            InterlockedExchange(&g_active,1);
            if(!wasBorderless) InterlockedIncrement64(&g_rc45ToBorderless);
            g_monitor=g_rc45Monitor;
            return GameProc(h,m,w,l);
        }
    }

    if(!internal && m==WM_STYLECHANGED && w==GWL_STYLE){
        const LONG_PTR now=GetWindowLongPtrW(h,GWL_STYLE);
        rc45_log_style("RC45_STYLE_CHANGED",now,now);
        const ptar_rc43::RequestedWindowMode requested=ptar_rc43::classify_style(now);
        if(requested==ptar_rc43::RequestedWindowMode::Windowed){
            const bool wasBorderless=InterlockedExchange(&g_rc45Borderless,0)!=0;
            InterlockedExchange(&g_active,1);
            const LRESULT r=call_next(g_gameNext,h,m,w,l);
            rc45_capture_windowed_state();
            rc45_follow_windowed_presenter(true);
            if(wasBorderless) InterlockedIncrement64(&g_rc45ToWindowed);
            return r;
        }
        if(requested==ptar_rc43::RequestedWindowMode::Borderless){
            const bool wasBorderless=InterlockedCompareExchange(&g_rc45Borderless,0,0)!=0;
            if(!wasBorderless) rc45_capture_windowed_state();
            InterlockedExchange(&g_rc45Borderless,1);
            InterlockedExchange(&g_active,1);
            g_monitor=g_rc45Monitor;
            const LRESULT r=GameProc(h,m,w,l);
            enforce_geometry();
            if(!wasBorderless) InterlockedIncrement64(&g_rc45ToBorderless);
            return r;
        }
    }

    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)) return GameProc(h,m,w,l);

    const LRESULT r=call_next(g_gameNext,h,m,w,l);
    if(!internal){
        const bool raiseZ=(m==WM_ACTIVATE && LOWORD(w)!=WA_INACTIVE) || m==WM_SETFOCUS || m==WM_SHOWWINDOW;
        if(m==WM_WINDOWPOSCHANGED || m==WM_SIZE || m==WM_MOVE || m==WM_EXITSIZEMOVE || m==WM_SHOWWINDOW || m==WM_ACTIVATE || m==WM_SETFOCUS){
            if(m==WM_SIZE && w==SIZE_MINIMIZED){
                if(IsWindow(g_presenter)) ShowWindow(g_presenter,SW_HIDE);
            } else {
                rc45_follow_windowed_presenter(raiseZ);
                if(m==WM_WINDOWPOSCHANGED || m==WM_EXITSIZEMOVE) rc45_capture_windowed_state();
            }
        }
    }
    return r;
}

static DWORD WINAPI RC45PreferenceWorker(LPVOID){
    InterlockedExchange(&g_rc45PreferenceWorkerStarted,1);
    int last=g_rc45LastWindowStylePreference;
    while(InterlockedCompareExchange(&g_rc45Installed,0,0) && IsWindow(g_game)){
        const int current=rc45_read_windowstyle_preference();
        if(current>=0 && current!=last){
            last=current;
            g_rc45LastWindowStylePreference=current;
            if(g_rc45SyncMessage) PostMessageW(g_game,g_rc45SyncMessage,static_cast<WPARAM>(current),0);
        }
        Sleep(200);
    }
    InterlockedExchange(&g_rc45PreferenceWorkerStarted,0);
    return 0;
}

static DWORD WINAPI RC45StableWorker(LPVOID){
    if(!IsWindow(g_game)||!IsWindow(g_presenter)){rc45_starting_clear();return 20;}
    LONG_PTR cur=get_wndproc(g_game);
    if(!cur){logline("FAIL RC45 stable game proc unavailable; fail-open");rc45_starting_clear();return 21;}
    g_gameNext=reinterpret_cast<WNDPROC>(cur);
    if(!set_wndproc(g_game,RC45GameProc)){logline("FAIL RC45 game subclass install");rc45_starting_clear();return 22;}
    g_presenterNext=reinterpret_cast<WNDPROC>(get_wndproc(g_presenter));
    if(!g_presenterNext||!set_wndproc(g_presenter,PresenterProc)){
        set_wndproc(g_game,g_gameNext);logline("FAIL RC45 presenter subclass install");rc45_starting_clear();return 23;
    }
    patch_iat(GetModuleHandleW(nullptr));
    InterlockedExchange(&g_rc45Installed,1);
    InterlockedExchange(&g_active,1);

    const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);
    const int preference=rc45_read_windowstyle_preference();
    g_rc45LastWindowStylePreference=preference;
    logfmt("RC45_INITIAL_WINDOWSTYLE_PREF",preference,style,0,0);
    if(ptar_rc43::is_windowed_request(style)) rc45_capture_windowed_state();

    if(preference==1){
        rc45_enter_borderless("RC45_INITIAL_MODE=BORDERLESS_FROM_WINDOWSTYLE");
    } else if(preference>=0){
        rc45_enter_windowed(false,"RC45_INITIAL_MODE=WINDOWED_FROM_WINDOWSTYLE");
    } else if(ptar_rc43::is_borderless_request(style)){
        rc45_enter_borderless("RC45_INITIAL_MODE=BORDERLESS_FROM_STYLE");
    } else {
        rc45_enter_windowed(false,"RC45_INITIAL_MODE=WINDOWED_FROM_STYLE");
    }

    rc45_starting_clear();
    HANDLE pref=CreateThread(nullptr,0,RC45PreferenceWorker,nullptr,0,nullptr);
    if(pref) CloseHandle(pref); else logline("WARN RC45 preference worker unavailable; style messages remain authoritative");

    UINT gw=0,gh=0,pw=0,ph=0;client_size(g_game,gw,gh);client_size(g_presenter,pw,ph);
    logfmt("ACTIVE_RC45_WINDOWED_PRESENTER game/presenter",(LONG_PTR)((gw<<16)^gh),(LONG_PTR)((pw<<16)^ph),g_iatHooks,InterlockedCompareExchange(&g_rc45Borderless,0,0));
    if(g_rc45Runtime) RC41B_StartBootstrap(g_self,g_rc45Runtime);
    return 0;
}

static int rc45_attach(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    if(InterlockedCompareExchange(&g_rc45Installed,0,0)) return 1;
    if(InterlockedCompareExchange(&g_rc45Starting,1,0)) return 2;
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)||!renderW||!renderH||!outputW||!outputH||renderW>outputW||renderH>outputH){
        logline("FAIL RC45 invalid attach args");rc45_starting_clear();return -10;
    }
    HMONITOR hm=MonitorFromWindow(presenter,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi)){logline("FAIL RC45 monitor resolve");rc45_starting_clear();return -11;}
    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);
    if(mw!=outputW||mh!=outputH){logfmt("FAIL RC45 output/monitor mismatch",mw,mh,outputW,outputH);rc45_starting_clear();return -12;}
    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;
    g_monitor=mi.rcMonitor;g_rc45Monitor=mi.rcMonitor;
    g_initialGameProc=reinterpret_cast<WNDPROC>(get_wndproc(game));
    g_presenterStyle0=GetWindowLongPtrW(presenter,GWL_STYLE);g_presenterExStyle0=GetWindowLongPtrW(presenter,GWL_EXSTYLE);
    if(!g_initialGameProc){logline("FAIL RC45 initial game proc");rc45_starting_clear();return -13;}
    g_rc45SyncMessage=RegisterWindowMessageW(L"PTAR_RC45_WINDOWSTYLE_SYNC_20260914");
    if(!g_rc45SyncMessage){logline("FAIL RC45 sync message registration");rc45_starting_clear();return -14;}
    HANDLE th=CreateThread(nullptr,0,RC45StableWorker,nullptr,0,nullptr);
    if(!th){logline("FAIL RC45 worker create");rc45_starting_clear();return -15;}
    CloseHandle(th);logfmt("ATTACH_RC45_WINDOWED_PRESENTER_PENDING",renderW,renderH,outputW,outputH);return 0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    return rc45_attach(game,presenter,renderW,renderH,outputW,outputH);
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessQueryMode(PTARBorderlessModeState* s){
    if(!s||s->size<offsetof(PTARBorderlessModeState,targetLeft)) return -1;
    const UINT callerSize=s->size;
    ZeroMemory(reinterpret_cast<BYTE*>(s)+sizeof(UINT),callerSize-sizeof(UINT));
    s->installed=(UINT)InterlockedCompareExchange(&g_rc45Installed,0,0);
    s->borderlessActive=(UINT)InterlockedCompareExchange(&g_rc45Borderless,0,0);
    s->presenterVisible=(IsWindow(g_presenter)&&IsWindowVisible(g_presenter))?1u:0u;
    if(IsWindow(g_game)) s->gameStyle=(ULONG_PTR)GetWindowLongPtrW(g_game,GWL_STYLE);
    if(IsWindow(g_presenter)) s->presenterStyle=(ULONG_PTR)GetWindowLongPtrW(g_presenter,GWL_STYLE);
    s->transitionsToWindowed=(unsigned long long)InterlockedCompareExchange64(&g_rc45ToWindowed,0,0);
    s->transitionsToBorderless=(unsigned long long)InterlockedCompareExchange64(&g_rc45ToBorderless,0,0);
    if(callerSize>=sizeof(PTARBorderlessModeState)){
        s->targetLeft=g_monitor.left;s->targetTop=g_monitor.top;s->targetRight=g_monitor.right;s->targetBottom=g_monitor.bottom;
        s->windowStylePreference=g_rc45LastWindowStylePreference;
        s->presenterFollows=(unsigned long long)InterlockedCompareExchange64(&g_rc45PresenterFollows,0,0);
    }
    return 0;
}

static bool rc45_runtime_layout_ok(HMODULE runtime,BYTE*& base,IMAGE_NT_HEADERS64*& nt){
    if(!runtime)return false;base=(BYTE*)runtime;
    auto* dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return false;
    nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    if(nt->OptionalHeader.SizeOfImage<0x02C7E010u)return false;
    FARPROC exported=GetProcAddress(runtime,"D3D11CreateDeviceAndSwapChain");
    if(exported!=(FARPROC)(base+0x000021C0u))return false;
    static const BYTE gameAnchor[]={0x49,0x8B,0x49,0x30,0x48,0x89,0x0D,0xBA,0xC2,0xC3,0x02};
    if(memcmp(base+0x000037FBu,gameAnchor,sizeof(gameAnchor))!=0)return false;
    static const BYTE restoredPresenterLog[]={0xE8,0xAD,0x70,0xFF,0xFF};
    if(memcmp(base+0x0000C2FEu,restoredPresenterLog,sizeof(restoredPresenterLog))!=0)return false;
    BYTE* call=base+0x00003806u;if(call[0]!=0xE8)return false;
    INT32 disp=0;memcpy(&disp,call+1,sizeof(disp));BYTE* target=call+5+disp;
    if(target!=base+0x03500500u)return false;
    static const BYTE loaderCallsP1U46[]={0x49,0x8D,0x83,0x60,0xC4,0x00,0x00};
    if(memcmp(base+0x03500517u,loaderCallsP1U46,sizeof(loaderCallsP1U46))!=0)return false;
    auto* sh=IMAGE_FIRST_SECTION(nt);bool rc38=false;
    for(WORD i=0;i<nt->FileHeader.NumberOfSections;++i){if(memcmp(sh[i].Name,".rc38",5)==0&&sh[i].VirtualAddress==0x03500000u){rc38=true;break;}}
    return rc38;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_rc45Installed,0,0))return 1;
    BYTE* base=nullptr;IMAGE_NT_HEADERS64* nt=nullptr;
    if(!rc45_runtime_layout_ok(runtime,base,nt)){logline("FAIL RC45 runtime layout guard; fail-open");return -20;}
    HWND game=*(HWND*)(base+0x02C3FAC0u);HWND presenter=*(HWND*)(base+0x02C7DFE0u);
    UINT renderW=*(UINT*)(base+0x02C3FB78u),renderH=*(UINT*)(base+0x02C3FB7Cu);
    UINT outputW=*(UINT*)(base+0x0004B040u),outputH=*(UINT*)(base+0x0004B044u);
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)){logline("FAIL RC45 windows unavailable; fail-open");return -21;}
    if(!renderW||!renderH||!outputW||!outputH){logfmt("FAIL RC45 runtime dimensions",renderW,renderH,outputW,outputH);return -22;}
    const LONG_PTR proc=get_wndproc(game);
    if(proc!=(LONG_PTR)(base+0x0000DC50u)){logfmt("FAIL RC45 expected P1U46 WndProc not active",proc,(LONG_PTR)(base+0x0000DC50u));return -23;}
    g_rc45Runtime=runtime;
    logfmt("AUTO_START_RC45_WINDOWED_PRESENTER geometry",renderW,renderH,outputW,outputH);
    return rc45_attach(game,presenter,renderW,renderH,outputW,outputH);
}
