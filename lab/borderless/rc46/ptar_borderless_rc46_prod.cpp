#define PTAR_BorderlessAttachStable PTAR_BorderlessAttachStable_RC45_INTERNAL
#define PTAR_BorderlessQueryMode PTAR_BorderlessQueryMode_RC45_INTERNAL
#define PTAR_BorderlessAutoStart PTAR_BorderlessAutoStart_RC45_INTERNAL
#include "../rc45/ptar_borderless_rc45_prod.cpp"
#undef PTAR_BorderlessAttachStable
#undef PTAR_BorderlessQueryMode
#undef PTAR_BorderlessAutoStart

// RC46 keeps the RC45 dual-mode presenter architecture but fixes a field-observed
// P1U46 interaction: while WindowStyle=0, the engine can stretch the game HWND to
// a monitor-covering outer rect (-frame,-caption,monitor+frame) without changing
// WindowStyle. RC45 then saved that coerced rect as if it were user window state.
// RC46 preserves the last real windowed rect, rejects monitor-covering coercion
// outside interactive user sizing, and independently clamps the presenter to the
// current game client rect so runtime SetWindowPos calls cannot pull it to (0,0).

static volatile LONG g_rc46InSizeMove=0;
static volatile LONG64 g_rc46GameCoercionClamps=0;
static volatile LONG64 g_rc46PresenterClamps=0;
static volatile LONG64 g_rc46RejectedWindowSaves=0;
static WNDPROC g_rc46GameNext=nullptr;
static WNDPROC g_rc46PresenterNext=nullptr;

struct PTARRC46ModeState {
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
    unsigned long long gameCoercionClamps;
    unsigned long long presenterClamps;
    unsigned long long rejectedWindowSaves;
    LONG savedLeft;
    LONG savedTop;
    LONG savedRight;
    LONG savedBottom;
};

static bool rc46_client_screen_rect(HWND h,RECT& out) noexcept {
    if(!IsWindow(h)||IsIconic(h)) return false;
    RECT c{};if(!GetClientRect(h,&c)) return false;
    POINT a{c.left,c.top},b{c.right,c.bottom};
    if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b)) return false;
    if(b.x<=a.x||b.y<=a.y) return false;
    out={a.x,a.y,b.x,b.y};return true;
}

static bool rc46_rect_covers_monitor(const RECT& r) noexcept {
    constexpr LONG kTol=64;
    return r.left<=g_rc45Monitor.left+kTol && r.top<=g_rc45Monitor.top+kTol &&
           r.right>=g_rc45Monitor.right-kTol && r.bottom>=g_rc45Monitor.bottom-kTol;
}

static bool rc46_game_is_monitor_covering() noexcept {
    RECT c{};if(!rc46_client_screen_rect(g_game,c)) return false;
    constexpr LONG kTol=4;
    return c.left<=g_rc45Monitor.left+kTol && c.top<=g_rc45Monitor.top+kTol &&
           c.right>=g_rc45Monitor.right-kTol && c.bottom>=g_rc45Monitor.bottom-kTol;
}

static bool rc46_proposed_outer_covers_monitor(const WINDOWPOS* p) noexcept {
    if(!p||!IsWindow(g_game)) return false;
    RECT cur{};if(!GetWindowRect(g_game,&cur)) return false;
    RECT q=cur;
    if(!(p->flags&SWP_NOMOVE)){q.left=p->x;q.top=p->y;}
    if(!(p->flags&SWP_NOSIZE)){q.right=q.left+p->cx;q.bottom=q.top+p->cy;}
    else {const LONG w=cur.right-cur.left,h=cur.bottom-cur.top;q.right=q.left+w;q.bottom=q.top+h;}
    return rc46_rect_covers_monitor(q);
}

static void rc46_log_rect(const char* tag,const RECT& r) noexcept {logfmt(tag,r.left,r.top,r.right,r.bottom);}

static bool rc46_saved_window_valid() noexcept {
    if(!InterlockedCompareExchange(&g_rc45HaveSavedWindow,0,0)) return false;
    return g_rc45SavedWindowRect.right>g_rc45SavedWindowRect.left &&
           g_rc45SavedWindowRect.bottom>g_rc45SavedWindowRect.top &&
           !rc46_rect_covers_monitor(g_rc45SavedWindowRect);
}

static void rc46_capture_real_windowed_state() noexcept {
    if(!IsWindow(g_game)||IsIconic(g_game)) return;
    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)) return;
    const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);
    if(!ptar_rc43::is_windowed_request(style)) return;
    RECT r{};if(!GetWindowRect(g_game,&r)) return;
    if(rc46_rect_covers_monitor(r)||rc46_game_is_monitor_covering()){
        InterlockedIncrement64(&g_rc46RejectedWindowSaves);
        rc46_log_rect("RC46_REJECTED_MONITOR_COVERING_WINDOW_SAVE",r);
        return;
    }
    g_rc45SavedWindowStyle=style;
    g_rc45SavedWindowExStyle=GetWindowLongPtrW(g_game,GWL_EXSTYLE);
    g_rc45SavedWindowRect=r;
    InterlockedExchange(&g_rc45HaveSavedWindow,1);
    rc46_log_rect("RC46_SAVED_REAL_WINDOW_RECT",r);
}

static bool rc46_presenter_target(RECT& target) noexcept {
    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)){target=g_rc45Monitor;return true;}
    return rc46_client_screen_rect(g_game,target);
}

static void rc46_force_presenter_to_target(bool raiseZ) noexcept {
    if(!IsWindow(g_presenter)) return;
    RECT target{};if(!rc46_presenter_target(target)){ShowWindow(g_presenter,SW_HIDE);return;}
    g_monitor=target;
    InterlockedExchange(&g_internal,1);
    rc45_set_presenter_window_style();
    const HWND after=raiseZ?HWND_TOP:nullptr;
    const UINT flags=SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW|(raiseZ?0u:SWP_NOZORDER);
    SetWindowPos(g_presenter,after,target.left,target.top,target.right-target.left,target.bottom-target.top,flags);
    InterlockedExchange(&g_internal,0);
    InterlockedIncrement64(&g_rc45PresenterFollows);
}

static void rc46_restore_real_window_if_hijacked() noexcept {
    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)||InterlockedCompareExchange(&g_rc46InSizeMove,0,0)) return;
    if(g_rc45LastWindowStylePreference==1||!rc46_saved_window_valid()||!rc46_game_is_monitor_covering()) return;
    const RECT r=g_rc45SavedWindowRect;
    InterlockedExchange(&g_internal,1);
    SetWindowLongPtrW(g_game,GWL_STYLE,g_rc45SavedWindowStyle);
    SetWindowLongPtrW(g_game,GWL_EXSTYLE,g_rc45SavedWindowExStyle);
    SetWindowPos(g_game,HWND_TOP,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    InterlockedExchange(&g_internal,0);
    InterlockedIncrement64(&g_rc46GameCoercionClamps);
    rc46_log_rect("RC46_RESTORED_WINDOW_AFTER_MONITOR_COERCION",r);
    rc46_force_presenter_to_target(true);
}

static LRESULT CALLBACK RC46PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;
    LRESULT r=PresenterProc(h,m,w,l);
    if(!internal && !InterlockedCompareExchange(&g_rc45Borderless,0,0) && m==WM_WINDOWPOSCHANGING && l){
        RECT target{};
        if(rc46_client_screen_rect(g_game,target)){
            WINDOWPOS* p=reinterpret_cast<WINDOWPOS*>(l);
            const bool changed=p->x!=target.left||p->y!=target.top||p->cx!=(target.right-target.left)||p->cy!=(target.bottom-target.top)||((p->flags&(SWP_NOMOVE|SWP_NOSIZE))!=0);
            p->x=target.left;p->y=target.top;p->cx=target.right-target.left;p->cy=target.bottom-target.top;
            p->flags&=~(SWP_NOMOVE|SWP_NOSIZE);
            if(changed){InterlockedIncrement64(&g_rc46PresenterClamps);rc46_log_rect("RC46_PRESENTER_WINDOWPOS_CLAMP",target);}
        }
    }
    return r;
}

static LRESULT CALLBACK RC46GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;
    if(!internal && !InterlockedCompareExchange(&g_rc45Borderless,0,0)){
        if(m==WM_ENTERSIZEMOVE){InterlockedExchange(&g_rc46InSizeMove,1);logline("RC46_USER_SIZEMOVE_ENTER");}
        if(m==WM_WINDOWPOSCHANGING && l && !InterlockedCompareExchange(&g_rc46InSizeMove,0,0) && g_rc45LastWindowStylePreference!=1 && rc46_saved_window_valid()){
            LRESULT rr=call_next(g_rc46GameNext,h,m,w,l);
            WINDOWPOS* p=reinterpret_cast<WINDOWPOS*>(l);
            if(rc46_proposed_outer_covers_monitor(p)){
                const RECT s=g_rc45SavedWindowRect;
                p->x=s.left;p->y=s.top;p->cx=s.right-s.left;p->cy=s.bottom-s.top;p->flags&=~(SWP_NOMOVE|SWP_NOSIZE);
                InterlockedIncrement64(&g_rc46GameCoercionClamps);
                rc46_log_rect("RC46_CLAMPED_MONITOR_WINDOWPOS",s);
            }
            return rr;
        }
    }
    LRESULT r=RC45GameProc(h,m,w,l);
    if(!internal && !InterlockedCompareExchange(&g_rc45Borderless,0,0)){
        if(m==WM_WINDOWPOSCHANGED||m==WM_SIZE||m==WM_MOVE||m==WM_SHOWWINDOW||m==WM_ACTIVATE||m==WM_SETFOCUS){
            rc46_restore_real_window_if_hijacked();
            rc46_force_presenter_to_target((m==WM_ACTIVATE&&LOWORD(w)!=WA_INACTIVE)||m==WM_SETFOCUS||m==WM_SHOWWINDOW);
        }
        if(m==WM_EXITSIZEMOVE){
            InterlockedExchange(&g_rc46InSizeMove,0);
            rc46_capture_real_windowed_state();
            rc46_force_presenter_to_target(true);
            logline("RC46_USER_SIZEMOVE_EXIT");
        }
    }
    return r;
}

static DWORD WINAPI RC46InstallWorker(LPVOID){
    if(!IsWindow(g_game)||!IsWindow(g_presenter)){InterlockedExchange(&g_rc45Starting,0);return 20;}
    LONG_PTR cur=get_wndproc(g_game);if(!cur){logline("FAIL RC46 stable game proc unavailable; fail-open");InterlockedExchange(&g_rc45Starting,0);return 21;}
    g_rc46GameNext=reinterpret_cast<WNDPROC>(cur);g_gameNext=g_rc46GameNext;
    if(!set_wndproc(g_game,RC46GameProc)){logline("FAIL RC46 game subclass install");InterlockedExchange(&g_rc45Starting,0);return 22;}
    g_rc46PresenterNext=reinterpret_cast<WNDPROC>(get_wndproc(g_presenter));g_presenterNext=g_rc46PresenterNext;
    if(!g_presenterNext||!set_wndproc(g_presenter,RC46PresenterProc)){set_wndproc(g_game,g_rc46GameNext);logline("FAIL RC46 presenter subclass install");InterlockedExchange(&g_rc45Starting,0);return 23;}
    patch_iat(GetModuleHandleW(nullptr));InterlockedExchange(&g_rc45Installed,1);InterlockedExchange(&g_active,1);
    const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);const int preference=rc45_read_windowstyle_preference();g_rc45LastWindowStylePreference=preference;
    logfmt("RC46_INITIAL_WINDOWSTYLE_PREF",preference,style,0,0);if(ptar_rc43::is_windowed_request(style))rc46_capture_real_windowed_state();
    if(preference==1)rc45_enter_borderless("RC46_INITIAL_MODE=BORDERLESS_FROM_WINDOWSTYLE");
    else if(preference>=0){rc45_enter_windowed(false,"RC46_INITIAL_MODE=WINDOWED_FROM_WINDOWSTYLE");rc46_restore_real_window_if_hijacked();rc46_force_presenter_to_target(true);}
    else if(ptar_rc43::is_borderless_request(style))rc45_enter_borderless("RC46_INITIAL_MODE=BORDERLESS_FROM_STYLE");
    else {rc45_enter_windowed(false,"RC46_INITIAL_MODE=WINDOWED_FROM_STYLE");rc46_force_presenter_to_target(true);}
    InterlockedExchange(&g_rc45Starting,0);
    HANDLE pref=CreateThread(nullptr,0,RC45PreferenceWorker,nullptr,0,nullptr);if(pref)CloseHandle(pref);else logline("WARN RC46 preference worker unavailable; style messages remain authoritative");
    UINT gw=0,gh=0,pw=0,ph=0;client_size(g_game,gw,gh);client_size(g_presenter,pw,ph);
    logfmt("ACTIVE_RC46_WINDOWED_GEOMETRY_GUARD game/presenter",(LONG_PTR)((gw<<16)^gh),(LONG_PTR)((pw<<16)^ph),g_iatHooks,InterlockedCompareExchange(&g_rc45Borderless,0,0));
    if(g_rc45Runtime)RC41B_StartBootstrap(g_self,g_rc45Runtime);return 0;
}

static int rc46_attach(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    if(InterlockedCompareExchange(&g_rc45Installed,0,0))return 1;if(InterlockedCompareExchange(&g_rc45Starting,1,0))return 2;
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)||!renderW||!renderH||!outputW||!outputH||renderW>outputW||renderH>outputH){logline("FAIL RC46 invalid attach args");InterlockedExchange(&g_rc45Starting,0);return -10;}
    HMONITOR hm=MonitorFromWindow(presenter,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!hm||!GetMonitorInfoW(hm,&mi)){logline("FAIL RC46 monitor resolve");InterlockedExchange(&g_rc45Starting,0);return -11;}
    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);if(mw!=outputW||mh!=outputH){logfmt("FAIL RC46 output/monitor mismatch",mw,mh,outputW,outputH);InterlockedExchange(&g_rc45Starting,0);return -12;}
    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=mi.rcMonitor;g_rc45Monitor=mi.rcMonitor;
    g_initialGameProc=reinterpret_cast<WNDPROC>(get_wndproc(game));g_presenterStyle0=GetWindowLongPtrW(presenter,GWL_STYLE);g_presenterExStyle0=GetWindowLongPtrW(presenter,GWL_EXSTYLE);
    if(!g_initialGameProc){logline("FAIL RC46 initial game proc");InterlockedExchange(&g_rc45Starting,0);return -13;}
    g_rc45SyncMessage=RegisterWindowMessageW(L"PTAR_RC46_WINDOWSTYLE_SYNC_20260914");if(!g_rc45SyncMessage){logline("FAIL RC46 sync message registration");InterlockedExchange(&g_rc45Starting,0);return -14;}
    HANDLE th=CreateThread(nullptr,0,RC46InstallWorker,nullptr,0,nullptr);if(!th){logline("FAIL RC46 worker create");InterlockedExchange(&g_rc45Starting,0);return -15;}CloseHandle(th);
    logfmt("ATTACH_RC46_WINDOWED_GEOMETRY_GUARD_PENDING",renderW,renderH,outputW,outputH);return 0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){return rc46_attach(game,presenter,renderW,renderH,outputW,outputH);}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessQueryMode(PTARRC46ModeState* s){
    if(!s||s->size<offsetof(PTARRC46ModeState,targetLeft))return -1;const UINT callerSize=s->size;ZeroMemory(reinterpret_cast<BYTE*>(s)+sizeof(UINT),callerSize-sizeof(UINT));
    s->installed=(UINT)InterlockedCompareExchange(&g_rc45Installed,0,0);s->borderlessActive=(UINT)InterlockedCompareExchange(&g_rc45Borderless,0,0);s->presenterVisible=(IsWindow(g_presenter)&&IsWindowVisible(g_presenter))?1u:0u;
    if(IsWindow(g_game))s->gameStyle=(ULONG_PTR)GetWindowLongPtrW(g_game,GWL_STYLE);if(IsWindow(g_presenter))s->presenterStyle=(ULONG_PTR)GetWindowLongPtrW(g_presenter,GWL_STYLE);
    s->transitionsToWindowed=(unsigned long long)InterlockedCompareExchange64(&g_rc45ToWindowed,0,0);s->transitionsToBorderless=(unsigned long long)InterlockedCompareExchange64(&g_rc45ToBorderless,0,0);
    if(callerSize>=sizeof(PTARRC46ModeState)){s->targetLeft=g_monitor.left;s->targetTop=g_monitor.top;s->targetRight=g_monitor.right;s->targetBottom=g_monitor.bottom;s->windowStylePreference=g_rc45LastWindowStylePreference;s->presenterFollows=(unsigned long long)InterlockedCompareExchange64(&g_rc45PresenterFollows,0,0);s->gameCoercionClamps=(unsigned long long)InterlockedCompareExchange64(&g_rc46GameCoercionClamps,0,0);s->presenterClamps=(unsigned long long)InterlockedCompareExchange64(&g_rc46PresenterClamps,0,0);s->rejectedWindowSaves=(unsigned long long)InterlockedCompareExchange64(&g_rc46RejectedWindowSaves,0,0);s->savedLeft=g_rc45SavedWindowRect.left;s->savedTop=g_rc45SavedWindowRect.top;s->savedRight=g_rc45SavedWindowRect.right;s->savedBottom=g_rc45SavedWindowRect.bottom;}
    return 0;
}

static bool rc46_runtime_layout_ok(HMODULE runtime,BYTE*& base,IMAGE_NT_HEADERS64*& nt){return rc45_runtime_layout_ok(runtime,base,nt);}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_rc45Installed,0,0))return 1;BYTE* base=nullptr;IMAGE_NT_HEADERS64* nt=nullptr;if(!rc46_runtime_layout_ok(runtime,base,nt)){logline("FAIL RC46 runtime layout guard; fail-open");return -20;}
    HWND game=*(HWND*)(base+0x02C3FAC0u);HWND presenter=*(HWND*)(base+0x02C7DFE0u);UINT renderW=*(UINT*)(base+0x02C3FB78u),renderH=*(UINT*)(base+0x02C3FB7Cu);UINT outputW=*(UINT*)(base+0x0004B040u),outputH=*(UINT*)(base+0x0004B044u);
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)){logline("FAIL RC46 windows unavailable; fail-open");return -21;}if(!renderW||!renderH||!outputW||!outputH){logfmt("FAIL RC46 runtime dimensions",renderW,renderH,outputW,outputH);return -22;}
    const LONG_PTR proc=get_wndproc(game);if(proc!=(LONG_PTR)(base+0x0000DC50u)){logfmt("FAIL RC46 expected P1U46 WndProc not active",proc,(LONG_PTR)(base+0x0000DC50u));return -23;}g_rc45Runtime=runtime;
    logfmt("AUTO_START_RC46_WINDOWED_GEOMETRY_GUARD geometry",renderW,renderH,outputW,outputH);return rc46_attach(game,presenter,renderW,renderH,outputW,outputH);
}
