from pathlib import Path

SRC=Path('lab/borderless/rc58/ptar_borderless_rc58_mode_bridge.cpp')
OUT=Path('lab/borderless/rc59/generated/ptar_borderless_rc59_mode_bridge.cpp')

s=SRC.read_text(encoding='utf-8')
# Version/log/export namespace first.
s=s.replace('RC58','RC59').replace('rc58','rc59')

old_protect="const bool protect=(InterlockedCompareExchange(&g_windowed,0,0)!=0||InterlockedCompareExchange(&g_forceGeometry,0,0)!=0)&&InterlockedCompareExchange(&g_haveSaved,0,0)!=0;"
new_protect="const bool protect=InterlockedCompareExchange(&g_haveSaved,0,0)!=0;"
if old_protect not in s:
    raise RuntimeError('protect anchor missing')
s=s.replace(old_protect,new_protect,1)

old_periodic="if(InterlockedCompareExchange(&g_windowed,0,0)&&(++restoreTick%2u)==0u&&!current_window_matches_target())post_control(RC59_CTL_RESTORE_WINDOW);"
new_periodic="if((++restoreTick%2u)==0u&&!current_window_matches_target())post_control(RC59_CTL_RESTORE_WINDOW);"
if old_periodic not in s:
    raise RuntimeError('periodic restore anchor missing')
s=s.replace(old_periodic,new_periodic,1)

start=s.index('static DWORD WINAPI Worker(LPVOID) noexcept {')
end=s.index('\n}\n\nstruct PTARRC59BridgeState',start)
worker=r'''static DWORD WINAPI Worker(LPVOID) noexcept {
    for(unsigned i=0;i<400&&!InterlockedCompareExchange(&g_stop,0,0);++i){if(IsWindow(g_game)&&get_proc(g_game))break;Sleep(5);}if(!IsWindow(g_game)){logline("FAIL RC59 game window unavailable");return 20;}
    const LONG_PTR observedStyle=GetWindowLongPtrW(g_game,GWL_STYLE);const LONG_PTR observedEx=GetWindowLongPtrW(g_game,GWL_EXSTYLE);RECT observed{};GetWindowRect(g_game,&observed);
    logfmt("RC59_STARTUP_OBSERVED_STYLE",observedStyle,observedEx,observed.right-observed.left,observed.bottom-observed.top);
    if((observedStyle&WS_POPUP)!=0)logline("RC59_FIELD_POPUP_START_DETECTED");

    bool captured=false;LONG_PTR stableProc=0;unsigned stableProcTicks=0;unsigned popupTicks=0;
    for(unsigned i=0;i<3000&&!InterlockedCompareExchange(&g_stop,0,0)&&IsWindow(g_game);++i){
        const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);const LONG_PTR proc=get_proc(g_game);
        if((style&WS_POPUP)!=0||!proc||!g_renderW||!g_renderH){
            stableProc=0;stableProcTicks=0;++popupTicks;
            if(i==0||i==20||i==100||i==500)logfmt("RC59_WAIT_P1U46_CANONICAL_WINDOW",style,proc,g_renderW,g_renderH);
            Sleep(5);continue;
        }
        if(proc==stableProc)++stableProcTicks;else{stableProc=proc;stableProcTicks=1;}
        // Require a short stable WndProc epoch so P1U46 has finished installing its own guard.
        if(stableProcTicks<6){Sleep(5);continue;}
        if(capture_window_target()){captured=true;break;}
        stableProcTicks=0;Sleep(5);
    }
    if(!captured){const LONG_PTR style=IsWindow(g_game)?GetWindowLongPtrW(g_game,GWL_STYLE):0;logfmt("FAIL RC59 canonical window capture timeout",style,get_proc(g_game),g_renderW,g_renderH);return 21;}
    logfmt("RC59_CANONICAL_WINDOW_CAPTURED_AFTER_WAIT",popupTicks,g_savedStyle,g_savedOuter.right-g_savedOuter.left,g_savedOuter.bottom-g_savedOuter.top);

    g_controlMessage=RegisterWindowMessageW(L"PTAR_RC59_MODE_BRIDGE_CONTROL_20260915");if(!g_controlMessage){logline("FAIL RC59 control message registration");return 22;}
    g_next=(WNDPROC)get_proc(g_game);if(!g_next||!set_proc(g_game,GameProc)){logline("FAIL RC59 game subclass");return 23;}InterlockedExchange(&g_installed,1);logline("RC59_MODE_BRIDGE_INSTALLED");
    logline("RC59_GAME_HWND_POLICY=ALWAYS_CANONICAL_WINDOWED_RENDER_SIZE");
    int last=-2;unsigned restoreTick=0;
    while(!InterlockedCompareExchange(&g_stop,0,0)&&IsWindow(g_game)){
        int p=read_pref();if(p>=0&&p!=last){last=p;InterlockedIncrement64(&g_prefRequests);if(p==0)request_windowed("RC59_PREF_REQUEST=WINDOWED_GAME_DIRECT");else if(p==1)request_borderless("RC59_PREF_REQUEST=BORDERLESS_USR_PRESENTER");}
        if((++restoreTick%2u)==0u&&!current_window_matches_target())post_control(RC59_CTL_RESTORE_WINDOW);
        Sleep(50);
    }
    return 0;
}'''
s=s[:start]+worker+s[end:]

# Keep a compatibility query export for any RC58 diagnostic host while exposing RC59 canonically.
needle='extern "C" __declspec(dllexport) int WINAPI PTAR_RC59_QueryBridge(PTARRC59BridgeState* s)'
idx=s.find(needle)
if idx<0: raise RuntimeError('query export missing after versioning')
# no compatibility alias needed in field package; exact RC59 symbol is the contract.

OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_text(s,encoding='utf-8',newline='\n')
print('RC59_POPUP_RECOVERY_SOURCE=PASS')
