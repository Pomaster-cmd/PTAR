from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]


def once(t,o,n,label):
    c=t.count(o)
    if c!=1:
        raise RuntimeError(f'{label}: expected 1 match got {c}')
    return t.replace(o,n,1)


def write(p,t):
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(t,encoding='utf-8',newline='\n')


def main():
    out=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'lab/borderless/rc58/generated'
    out.mkdir(parents=True,exist_ok=True)
    subprocess.check_call([sys.executable,str(ROOT/'lab/borderless/rc56/build_rc56_presenter_only.py'),str(out)])

    p45=out/'ptar_borderless_rc45_presenter_only.cpp'
    t=p45.read_text(encoding='utf-8-sig')
    anchor='static int g_rc45LastWindowStylePreference=-2;\n'
    globals_=r'''static int g_rc45LastWindowStylePreference=-2;

// RC58 owns only a virtual display mode. The game HWND/DXGI state stays Windowed.
// Mode changes are requested on the game thread and committed on the present
// thread after P1U46 has completed a conservative startup stability barrier.
static volatile LONG g_rc58Stable=0;
static volatile LONG g_rc58PendingMode=-1; // -1 none, 0 Windowed, 1 presenter-only Borderless
static volatile LONG g_rc58HotkeyDown=0;
static volatile LONG64 g_rc58ToggleRequests=0;
static volatile LONG64 g_rc58ToggleApplies=0;
static volatile LONG64 g_rc58EarlyRejects=0;
static volatile LONG64 g_rc58NativeBorderlessBlocks=0;
static volatile LONG64 g_rc58RegistryNormalizations=0;
static DWORD g_rc58FirstSuccessfulPresentTick=0;
'''
    t=once(t,anchor,globals_,'globals')

    proc_anchor='static LRESULT CALLBACK RC45GameProc(HWND h,UINT m,WPARAM w,LPARAM l){\n'
    helpers=r'''static bool rc58_set_windowstyle_zero() noexcept {
    HKEY key=nullptr;DWORD disp=0;
    const wchar_t* path=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
    if(RegCreateKeyExW(HKEY_CURRENT_USER,path,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,&disp)!=ERROR_SUCCESS) return false;
    const DWORD zero=0;const LONG r=RegSetValueExW(key,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&zero),sizeof(zero));RegCloseKey(key);
    if(r==ERROR_SUCCESS){InterlockedIncrement64(&g_rc58RegistryNormalizations);g_rc45LastWindowStylePreference=0;return true;}
    return false;
}

static void rc58_request_mode(int mode,const char* reason) noexcept {
    if(mode!=0 && mode!=1) return;
    InterlockedIncrement64(&g_rc58ToggleRequests);
    if(!InterlockedCompareExchange(&g_rc58Stable,0,0)){
        InterlockedIncrement64(&g_rc58EarlyRejects);
        logline("RC58_MODE_REQUEST_REJECTED_STARTUP_NOT_STABLE");
        return;
    }
    InterlockedExchange(&g_rc58PendingMode,mode);
    logline(reason);
}

'''
    t=once(t,proc_anchor,helpers+proc_anchor,'helpers')

    top='''static LRESULT CALLBACK RC45GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;
'''
    top_new=r'''static LRESULT CALLBACK RC45GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;

    // Canonical RC58 toggle: Ctrl+F10. P1U46 owns plain F10, so the chords do not
    // collide. The request is consumed here but applied only at a Present boundary.
    if(!internal && (m==WM_KEYDOWN || m==WM_SYSKEYDOWN) && w==VK_F10 && (GetAsyncKeyState(VK_CONTROL)&0x8000)){
        if(InterlockedExchange(&g_rc58HotkeyDown,1)==0){
            const int target=InterlockedCompareExchange(&g_rc45Borderless,0,0)?0:1;
            rc58_request_mode(target,target?"RC58_HOTKEY_REQUEST=BORDERLESS":"RC58_HOTKEY_REQUEST=WINDOWED");
        }
        return 0;
    }
    if(!internal && (m==WM_KEYUP || m==WM_SYSKEYUP) && w==VK_F10 && InterlockedCompareExchange(&g_rc58HotkeyDown,0,0)){
        InterlockedExchange(&g_rc58HotkeyDown,0);return 0;
    }

    // Never allow the game's native Borderless path to mutate the HWND. If the
    // settings UI requests it after startup, translate it to RC58 virtual mode.
    if(!internal && m==WM_STYLECHANGING && l && w==GWL_STYLE){
        STYLESTRUCT* ss=reinterpret_cast<STYLESTRUCT*>(l);
        const auto requested=ptar_rc43::classify_style(ss->styleNew);
        if(requested==ptar_rc43::RequestedWindowMode::Borderless){
            ss->styleNew=ss->styleOld;
            InterlockedIncrement64(&g_rc58NativeBorderlessBlocks);
            rc58_request_mode(1,"RC58_NATIVE_BORDERLESS_VIRTUALIZED");
            rc58_set_windowstyle_zero();
        } else if(requested==ptar_rc43::RequestedWindowMode::Windowed){
            rc58_request_mode(0,"RC58_NATIVE_WINDOWED_REQUEST");
        }
        return call_next(g_gameNext,h,m,w,l);
    }
    if(!internal && m==WM_STYLECHANGED && w==GWL_STYLE){
        return call_next(g_gameNext,h,m,w,l);
    }
'''
    t=once(t,top,top_new,'game proc top')

    worker_old=r'''static DWORD WINAPI RC45PreferenceWorker(LPVOID){
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
}'''
    worker_new=r'''static DWORD WINAPI RC45PreferenceWorker(LPVOID){
    InterlockedExchange(&g_rc45PreferenceWorkerStarted,1);
    int last=0;g_rc45LastWindowStylePreference=0;
    while(InterlockedCompareExchange(&g_rc45Installed,0,0) && IsWindow(g_game)){
        const int current=rc45_read_windowstyle_preference();
        if(current==1){
            // The game may persist Borderless before/without a style message.
            // Translate the preference and immediately restore the safe native
            // Windowed baseline so the next process can never boot native Borderless.
            rc58_request_mode(1,"RC58_REGISTRY_BORDERLESS_VIRTUALIZED");
            rc58_set_windowstyle_zero();
            last=0;
        } else if(current==0 && last!=0){
            rc58_request_mode(0,"RC58_REGISTRY_WINDOWED_REQUEST");
            last=0;g_rc45LastWindowStylePreference=0;
        }
        Sleep(100);
    }
    InterlockedExchange(&g_rc45PreferenceWorkerStarted,0);
    return 0;
}'''
    t=once(t,worker_old,worker_new,'preference worker')
    write(p45,t)

    p46=out/'ptar_borderless_rc46_presenter_only.cpp'
    t=p46.read_text(encoding='utf-8-sig')
    old=r'''    const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);const int preference=rc45_read_windowstyle_preference();g_rc45LastWindowStylePreference=preference;
    logfmt("RC56_PRESENTER_ONLY_BORDERLESS_INITIAL_WINDOWSTYLE_PREF",preference,style,0,0);if(ptar_rc43::is_windowed_request(style))rc46_capture_real_windowed_state();
    if(preference==1)rc45_enter_borderless("RC56_PRESENTER_ONLY_BORDERLESS_INITIAL_MODE=BORDERLESS_FROM_WINDOWSTYLE");
    else if(preference>=0){rc45_enter_windowed(false,"RC56_PRESENTER_ONLY_BORDERLESS_INITIAL_MODE=WINDOWED_FROM_WINDOWSTYLE");rc46_restore_real_window_if_hijacked();rc46_force_presenter_to_target(true);}
    else if(ptar_rc43::is_borderless_request(style))rc45_enter_borderless("RC56_PRESENTER_ONLY_BORDERLESS_INITIAL_MODE=BORDERLESS_FROM_STYLE");
    else {rc45_enter_windowed(false,"RC56_PRESENTER_ONLY_BORDERLESS_INITIAL_MODE=WINDOWED_FROM_STYLE");rc46_force_presenter_to_target(true);}'''
    # build_rc56 replaces RC46_WINDOWED_GEOMETRY_GUARD strings globally; verify exact generated text first.
    if old not in t:
        old=old.replace('RC56_PRESENTER_ONLY_BORDERLESS_INITIAL_WINDOWSTYLE_PREF','RC46_INITIAL_WINDOWSTYLE_PREF').replace('RC56_PRESENTER_ONLY_BORDERLESS_INITIAL_MODE','RC46_INITIAL_MODE')
    new=r'''    const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);const int observedPreference=rc45_read_windowstyle_preference();
    // RC58 process invariant: native engine mode is Windowed regardless of the
    // persisted value. The installer establishes 0 before process creation and
    // this is a defense-in-depth normalization only.
    g_rc45LastWindowStylePreference=0;
    logfmt("RC58_INITIAL_WINDOWSTYLE_OBSERVED",observedPreference,style,0,0);
    if(observedPreference==1) rc58_set_windowstyle_zero();
    if(ptar_rc43::is_windowed_request(style))rc46_capture_real_windowed_state();
    rc45_enter_windowed(false,"RC58_INITIAL_MODE=WINDOWED_NATIVE_BASELINE");
    rc46_restore_real_window_if_hijacked();rc46_force_presenter_to_target(true);'''
    t=once(t,old,new,'RC46 initial mode')
    t=t.replace('RC56_PRESENTER_ONLY_BORDERLESS','RC58_VIRTUAL_PRESENTER_MODE')
    write(p46,t)

    p51=out/'ptar_borderless_rc56_prod.cpp'
    t=p51.read_text(encoding='utf-8-sig')
    state_anchor='static UINT g_rc51LastHwndW=0,g_rc51LastHwndH=0,g_rc51LastSwapW=0,g_rc51LastSwapH=0,g_rc51LastCachedW=0,g_rc51LastCachedH=0;\n'
    state_new=state_anchor+r'''
static volatile LONG64 g_rc58StablePresentCount=0;
static volatile LONG64 g_rc58ModeApplyFailures=0;

static bool rc58_startup_stable_after_present() noexcept {
    const LONG64 n=InterlockedIncrement64(&g_rc58StablePresentCount);
    DWORD first=g_rc58FirstSuccessfulPresentTick;
    const DWORD now=GetTickCount();
    if(!first){
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&g_rc58FirstSuccessfulPresentTick),(LONG)now,0);
        first=g_rc58FirstSuccessfulPresentTick;
    }
    if(!InterlockedCompareExchange(&g_rc58Stable,0,0) && n>=180 && (DWORD)(now-first)>=5000){
        InterlockedExchange(&g_rc58Stable,1);
        logfmt("RC58_STARTUP_STABLE presents/ms",(LONG_PTR)n,(LONG_PTR)(DWORD)(now-first),0,0);
    }
    return InterlockedCompareExchange(&g_rc58Stable,0,0)!=0;
}
'''
    t=once(t,state_anchor,state_new,'rc51 state')

    old_present=r'''static HRESULT STDMETHODCALLTYPE rc51_present(IDXGISwapChain* sc,UINT syncInterval,UINT flags){
    PresentFn original=g_rc51OrigPresent;
    if(!original)return E_FAIL;
    const HRESULT hr=original(sc,syncInterval,flags);
    InterlockedIncrement64(&g_rc51PresentCalls);
    if(SUCCEEDED(hr))rc51_resync_if_needed(sc);
    return hr;
}'''
    new_present=r'''static HRESULT STDMETHODCALLTYPE rc51_present(IDXGISwapChain* sc,UINT syncInterval,UINT flags){
    PresentFn original=g_rc51OrigPresent;
    if(!original)return E_FAIL;
    const HRESULT hr=original(sc,syncInterval,flags);
    InterlockedIncrement64(&g_rc51PresentCalls);
    if(SUCCEEDED(hr)){
        const bool stable=rc58_startup_stable_after_present();
        if(stable){
            const LONG pending=InterlockedExchange(&g_rc58PendingMode,-1);
            if(pending==1 && !InterlockedCompareExchange(&g_rc45Borderless,0,0)){
                rc45_enter_borderless("RC58_APPLY=BORDERLESS_PRESENT_BOUNDARY");
                InterlockedIncrement64(&g_rc58ToggleApplies);
                if(!rc51_resync_if_needed(sc))InterlockedIncrement64(&g_rc58ModeApplyFailures);
                return hr;
            }
            if(pending==0 && InterlockedCompareExchange(&g_rc45Borderless,0,0)){
                rc45_enter_windowed(false,"RC58_APPLY=WINDOWED_PRESENT_BOUNDARY");
                rc46_restore_real_window_if_hijacked();
                rc46_force_presenter_to_target(true);
                InterlockedIncrement64(&g_rc58ToggleApplies);
                if(!rc51_resync_if_needed(sc))InterlockedIncrement64(&g_rc58ModeApplyFailures);
                return hr;
            }
        }
        rc51_resync_if_needed(sc);
    }
    return hr;
}'''
    t=once(t,old_present,new_present,'present boundary')

    query_anchor='extern "C" __declspec(dllexport) int WINAPI PTAR_RC51_QueryPresenterSync(PTARRC51PresenterSyncState* s){'
    query=r'''struct PTARRC58VirtualModeState {
    UINT size,stable,borderlessActive;
    LONG pendingMode;
    unsigned long long stablePresents,toggleRequests,toggleApplies,earlyRejects,nativeBorderlessBlocks,registryNormalizations,applyFailures;
};
extern "C" __declspec(dllexport) int WINAPI PTAR_RC58_QueryVirtualMode(PTARRC58VirtualModeState* s){
    if(!s||s->size<sizeof(PTARRC58VirtualModeState))return -1;
    s->stable=(UINT)InterlockedCompareExchange(&g_rc58Stable,0,0);
    s->borderlessActive=(UINT)InterlockedCompareExchange(&g_rc45Borderless,0,0);
    s->pendingMode=InterlockedCompareExchange(&g_rc58PendingMode,0,0);
    s->stablePresents=(unsigned long long)InterlockedCompareExchange64(&g_rc58StablePresentCount,0,0);
    s->toggleRequests=(unsigned long long)InterlockedCompareExchange64(&g_rc58ToggleRequests,0,0);
    s->toggleApplies=(unsigned long long)InterlockedCompareExchange64(&g_rc58ToggleApplies,0,0);
    s->earlyRejects=(unsigned long long)InterlockedCompareExchange64(&g_rc58EarlyRejects,0,0);
    s->nativeBorderlessBlocks=(unsigned long long)InterlockedCompareExchange64(&g_rc58NativeBorderlessBlocks,0,0);
    s->registryNormalizations=(unsigned long long)InterlockedCompareExchange64(&g_rc58RegistryNormalizations,0,0);
    s->applyFailures=(unsigned long long)InterlockedCompareExchange64(&g_rc58ModeApplyFailures,0,0);
    return 0;
}

'''
    t=once(t,query_anchor,query+query_anchor,'query export')
    t=t.replace('RC51_PRESENTER_SWAPCHAIN_SYNC_MODULE_LOADED','RC58_VIRTUAL_PRESENTER_MODE_MODULE_LOADED')
    write(out/'ptar_borderless_rc58_prod.cpp',t)

    # Keep the inherited filename only as an internal build dependency marker;
    # production workflows compile ptar_borderless_rc58_prod.cpp explicitly.
    (out/'RC58_GENERATION.txt').write_text(
        'RC58_GENERATION=PASS\nBASE=RC56_PRESENTER_ONLY+RC51+RC55\nNATIVE_GAME_MODE=WINDOWED_ONLY\nVIRTUAL_TOGGLE=CTRL+F10\nMODE_APPLY=PRESENTER_PRESENT_BOUNDARY\nSTARTUP_BARRIER=180_PRESENTS_AND_5000MS\nGAME_HWND_MUTATION=BLOCKED\nREGISTRY_BORDERLESS=VIRTUALIZED_TO_WINDOWSTYLE_0\n',encoding='ascii',newline='\n')
    print('RC58_GENERATION=PASS')

if __name__=='__main__': main()
