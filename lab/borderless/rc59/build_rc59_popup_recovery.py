from pathlib import Path

SRC=Path('lab/borderless/rc58/ptar_borderless_rc58_mode_bridge.cpp')
OUT=Path('lab/borderless/rc59/generated/ptar_borderless_rc59_mode_bridge.cpp')

s=SRC.read_text(encoding='utf-8')
s=s.replace('RC58','RC59').replace('rc58','rc59')
s=s.replace('#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"','#include "../../../raster/rc41b/ptar_rc41b_bootstrap.h"',1)

old='volatile LONG g_haveSaved=0;'
new='volatile LONG g_haveSaved=0,g_canonicalizeDone=0,g_canonicalizeOk=0;'
if old not in s: raise RuntimeError('global anchor missing')
s=s.replace(old,new,1)

old='enum : WPARAM { RC59_CTL_TOGGLE_TO_WINDOWED=1, RC59_CTL_TOGGLE_TO_BORDERLESS=2, RC59_CTL_RESTORE_WINDOW=3 };'
new='enum : WPARAM { RC59_CTL_TOGGLE_TO_WINDOWED=1, RC59_CTL_TOGGLE_TO_BORDERLESS=2, RC59_CTL_RESTORE_WINDOW=3, RC59_CTL_CANONICALIZE_STARTUP=4 };'
if old not in s: raise RuntimeError('control enum anchor missing')
s=s.replace(old,new,1)

old="const bool protect=(InterlockedCompareExchange(&g_windowed,0,0)!=0||InterlockedCompareExchange(&g_forceGeometry,0,0)!=0)&&InterlockedCompareExchange(&g_haveSaved,0,0)!=0;"
new="const bool protect=InterlockedCompareExchange(&g_haveSaved,0,0)!=0;"
if old not in s: raise RuntimeError('protect anchor missing')
s=s.replace(old,new,1)

anchor='static void restore_window_ui() noexcept {'
if anchor not in s: raise RuntimeError('restore function anchor missing')
canon=r'''static bool handoff_p1u46_window_policy_ui() noexcept {
    BYTE* base=nullptr;if(!runtime_ok(g_runtime,base)||!base){logline("FAIL RC59 P1U46 policy ABI guard");return false;}
    // Exact P1U46 ABI, guarded above by the runtime identity/layout contract:
    // RVA 0x034FFA68 is the game-window policy mode consumed by the style/exstyle sanitizers.
    //   0 = canonical overlapped/windowed policy
    //   1 = startup popup policy captured from a WindowStyle=1 launch
    //   2 = P1U46 input-geometry special mode
    // The field failure is mode=1 persisting after the native USR presenter is active.
    volatile BYTE* policyMode=(volatile BYTE*)(base+0x034FFA68u);
    const BYTE before=*policyMode;
    if(before==2){logfmt("FAIL RC59 P1U46 policy mode2 unsafe",before,0,0,0);return false;}
    *policyMode=0;MemoryBarrier();const BYTE after=*policyMode;
    logfmt("RC59_P1U46_POLICY_HANDOFF",before,after,0,0);
    return after==0;
}
static void canonicalize_startup_ui() noexcept {
    InterlockedExchange(&g_canonicalizeDone,0);InterlockedExchange(&g_canonicalizeOk,0);
    if(!IsWindow(g_game)||!g_renderW||!g_renderH){logline("FAIL RC59 canonicalize invalid state");InterlockedExchange(&g_canonicalizeDone,1);return;}
    const LONG_PTR beforeStyle=GetWindowLongPtrW(g_game,GWL_STYLE),beforeEx=GetWindowLongPtrW(g_game,GWL_EXSTYLE);
    if(!handoff_p1u46_window_policy_ui()){InterlockedExchange(&g_canonicalizeDone,1);return;}
    LONG_PTR wantedStyle=(beforeStyle&~((LONG_PTR)WS_POPUP))|((LONG_PTR)WS_OVERLAPPEDWINDOW);
    wantedStyle|=(beforeStyle&((LONG_PTR)WS_VISIBLE|(LONG_PTR)WS_CLIPSIBLINGS|(LONG_PTR)WS_CLIPCHILDREN));
    RECT wr{0,0,(LONG)g_renderW,(LONG)g_renderH};
    if(!AdjustWindowRectEx(&wr,(DWORD)(ULONG_PTR)wantedStyle,GetMenu(g_game)!=nullptr,(DWORD)(ULONG_PTR)beforeEx)){
        logfmt("FAIL RC59 AdjustWindowRectEx canonicalize",GetLastError(),wantedStyle,beforeEx,0);InterlockedExchange(&g_canonicalizeDone,1);return;
    }
    const int ow=wr.right-wr.left,oh=wr.bottom-wr.top;if(ow<=0||oh<=0){logline("FAIL RC59 canonical outer geometry");InterlockedExchange(&g_canonicalizeDone,1);return;}
    HMONITOR mon=MonitorFromWindow(g_game,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);RECT mr{};
    if(mon&&GetMonitorInfoW(mon,&mi))mr=mi.rcMonitor;else{mr.left=0;mr.top=0;mr.right=(LONG)GetSystemMetrics(SM_CXSCREEN);mr.bottom=(LONG)GetSystemMetrics(SM_CYSCREEN);}
    const int x=mr.left+((mr.right-mr.left)-ow)/2,y=mr.top+((mr.bottom-mr.top)-oh)/2;
    InterlockedExchange(&g_forceGeometry,1);
    SetLastError(ERROR_SUCCESS);const LONG_PTR prev=SetWindowLongPtrW(g_game,GWL_STYLE,wantedStyle);const DWORD styleErr=GetLastError();
    const BOOL posOk=SetWindowPos(g_game,nullptr,x,y,ow,oh,SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    InterlockedExchange(&g_forceGeometry,0);
    if((prev==0&&styleErr!=ERROR_SUCCESS)||!posOk){logfmt("FAIL RC59 canonicalize mutation",styleErr,GetLastError(),ow,oh);InterlockedIncrement64(&g_restoreFailures);InterlockedExchange(&g_canonicalizeDone,1);return;}
    RECT c{},outer{};GetClientRect(g_game,&c);GetWindowRect(g_game,&outer);const LONG_PTR afterStyle=GetWindowLongPtrW(g_game,GWL_STYLE),afterEx=GetWindowLongPtrW(g_game,GWL_EXSTYLE);
    if((afterStyle&WS_POPUP)!=0||(UINT)(c.right-c.left)!=g_renderW||(UINT)(c.bottom-c.top)!=g_renderH){
        logfmt("FAIL RC59 canonicalize verify",afterStyle,c.right-c.left,c.bottom-c.top,0);InterlockedIncrement64(&g_restoreFailures);InterlockedExchange(&g_canonicalizeDone,1);return;
    }
    g_savedOuter=outer;g_savedStyle=afterStyle;g_savedExStyle=afterEx;InterlockedExchange(&g_haveSaved,1);InterlockedExchange(&g_canonicalizeOk,1);InterlockedExchange(&g_canonicalizeDone,1);
    logfmt("RC59_STARTUP_CANONICALIZED_UI_THREAD",beforeStyle,afterStyle,c.right-c.left,c.bottom-c.top);
    logfmt("RC59_SAVED_WINDOW_TARGET",outer.left,outer.top,outer.right,outer.bottom);
}
'''
s=s.replace(anchor,canon+anchor,1)

old='''if(g_controlMessage&&m==g_controlMessage){
        if(w==RC59_CTL_TOGGLE_TO_WINDOWED||w==RC59_CTL_TOGGLE_TO_BORDERLESS)forward_f10_ui();
        if(w==RC59_CTL_RESTORE_WINDOW)restore_window_ui();
        return 1;
    }'''
new='''if(g_controlMessage&&m==g_controlMessage){
        if(w==RC59_CTL_TOGGLE_TO_WINDOWED||w==RC59_CTL_TOGGLE_TO_BORDERLESS)forward_f10_ui();
        if(w==RC59_CTL_CANONICALIZE_STARTUP)canonicalize_startup_ui();
        if(w==RC59_CTL_RESTORE_WINDOW)restore_window_ui();
        return 1;
    }'''
if old not in s: raise RuntimeError('GameProc control block missing')
s=s.replace(old,new,1)

start=s.index('static DWORD WINAPI Worker(LPVOID) noexcept {')
end=s.index('\n}\n\nstruct PTARRC59BridgeState',start)
worker=r'''static DWORD WINAPI Worker(LPVOID) noexcept {
    for(unsigned i=0;i<400&&!InterlockedCompareExchange(&g_stop,0,0);++i){if(IsWindow(g_game)&&get_proc(g_game))break;Sleep(5);}if(!IsWindow(g_game)){logline("FAIL RC59 game window unavailable");return 20;}
    const LONG_PTR observedStyle=GetWindowLongPtrW(g_game,GWL_STYLE),observedEx=GetWindowLongPtrW(g_game,GWL_EXSTYLE);RECT observed{};GetWindowRect(g_game,&observed);
    logfmt("RC59_STARTUP_OBSERVED_STYLE",observedStyle,observedEx,observed.right-observed.left,observed.bottom-observed.top);
    const bool startupPopup=(observedStyle&WS_POPUP)!=0;if(startupPopup)logline("RC59_FIELD_POPUP_START_DETECTED");

    BYTE* rb=(BYTE*)g_runtime;auto* dos=(IMAGE_DOS_HEADER*)rb;auto* nt=(IMAGE_NT_HEADERS64*)(rb+dos->e_lfanew);const uintptr_t rtLo=(uintptr_t)rb,rtHi=rtLo+(uintptr_t)nt->OptionalHeader.SizeOfImage;
    LONG_PTR stableProc=0;unsigned stableTicks=0;bool p1u46Proc=false;
    for(unsigned i=0;i<3000&&!InterlockedCompareExchange(&g_stop,0,0)&&IsWindow(g_game);++i){
        const LONG_PTR proc=get_proc(g_game);const uintptr_t up=(uintptr_t)proc;const bool inRuntime=proc&&up>=rtLo&&up<rtHi;
        if(!inRuntime||!g_renderW||!g_renderH){stableProc=0;stableTicks=0;if(i==0||i==20||i==100||i==500)logfmt("RC59_WAIT_P1U46_WNDPROC",GetWindowLongPtrW(g_game,GWL_STYLE),proc,g_renderW,g_renderH);Sleep(5);continue;}
        if(proc==stableProc)++stableTicks;else{stableProc=proc;stableTicks=1;}
        if(stableTicks>=6){p1u46Proc=true;break;}Sleep(5);
    }
    if(!p1u46Proc){logfmt("FAIL RC59 P1U46 WndProc readiness",GetWindowLongPtrW(g_game,GWL_STYLE),get_proc(g_game),g_renderW,g_renderH);return 21;}
    logfmt("RC59_P1U46_WNDPROC_READY",stableProc,stableTicks,g_renderW,g_renderH);

    g_controlMessage=RegisterWindowMessageW(L"PTAR_RC59_MODE_BRIDGE_CONTROL_20260915");if(!g_controlMessage){logline("FAIL RC59 control message registration");return 22;}
    g_next=(WNDPROC)get_proc(g_game);if(!g_next||!set_proc(g_game,GameProc)){logline("FAIL RC59 game subclass");return 23;}

    if(startupPopup){
        logline("RC59_STARTUP_POPUP_CANONICALIZE_REQUEST");InterlockedExchange(&g_canonicalizeDone,0);InterlockedExchange(&g_canonicalizeOk,0);
        if(!post_control(RC59_CTL_CANONICALIZE_STARTUP)){logline("FAIL RC59 canonicalize control post");return 24;}
        for(unsigned i=0;i<2000&&!InterlockedCompareExchange(&g_canonicalizeDone,0,0);++i)Sleep(5);
        if(!InterlockedCompareExchange(&g_canonicalizeDone,0,0)||!InterlockedCompareExchange(&g_canonicalizeOk,0,0)){logline("FAIL RC59 startup popup canonicalization");return 25;}
    } else {
        if(!capture_window_target()){logline("FAIL RC59 capture exact window target");return 26;}
        logline("RC59_STARTUP_ALREADY_CANONICAL_WINDOWED");
    }
    if(!current_window_matches_target()){logline("FAIL RC59 canonical target postcheck");return 27;}
    InterlockedExchange(&g_installed,1);logline("RC59_MODE_BRIDGE_INSTALLED");logline("RC59_GAME_HWND_POLICY=ALWAYS_CANONICAL_WINDOWED_RENDER_SIZE");

    int last=-2;unsigned restoreTick=0;
    while(!InterlockedCompareExchange(&g_stop,0,0)&&IsWindow(g_game)){
        int p=read_pref();if(p>=0&&p!=last){last=p;InterlockedIncrement64(&g_prefRequests);if(p==0)request_windowed("RC59_PREF_REQUEST=WINDOWED_GAME_DIRECT");else if(p==1)request_borderless("RC59_PREF_REQUEST=BORDERLESS_USR_PRESENTER");}
        if((++restoreTick%2u)==0u&&!current_window_matches_target())post_control(RC59_CTL_RESTORE_WINDOW);
        Sleep(50);
    }
    return 0;
}'''
s=s[:start]+worker+s[end:]

needle='extern "C" __declspec(dllexport) int WINAPI PTAR_RC59_QueryBridge(PTARRC59BridgeState* s)'
if needle not in s: raise RuntimeError('query export missing after versioning')

OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_text(s,encoding='utf-8',newline='\n')
print('RC59_POPUP_RECOVERY_SOURCE=PASS')
