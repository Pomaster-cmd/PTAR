from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
src = ROOT / 'lab' / 'borderless' / 'rc48' / 'ptar_borderless_rc48_prod.cpp'
out = Path(sys.argv[1]) if len(sys.argv) > 1 else (Path(__file__).with_name('ptar_borderless_rc49_generated.cpp'))
s = src.read_text(encoding='utf-8')

needle = "static volatile LONG g_rc48PresenterWrapped=0;\nstatic WNDPROC g_rc48PresenterNext=nullptr;\n"
insert = needle + "static volatile LONG64 g_rc49TopmostClears=0;\nstatic volatile LONG64 g_rc49TopmostFailures=0;\n\n"
if needle not in s:
    raise SystemExit('RC49 patch guard: globals anchor missing')
s = s.replace(needle, insert, 1)

needle = "static bool rc48_make_child(bool countRepair) noexcept {\n"
helper = r'''static bool rc49_clear_windowed_topmost() noexcept {
    if(!IsWindow(g_game)||!IsWindow(g_presenter)||!rc48_windowed_mode()) return false;
    LONG_PTR ex=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);
    if(!(ex&WS_EX_TOPMOST)) return true;
    // SetWindowLongPtr changes the style bits, but Win32's topmost Z-order state is
    // committed through SetWindowPos. Field RC48 showed the bit surviving hundreds
    // of style-only repairs, so RC49 makes the Z-order transition explicit.
    const LONG_PTR desired=(ex|WS_EX_NOACTIVATE) &
        ~(LONG_PTR)(WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_APPWINDOW|WS_EX_TOOLWINDOW);
    if(ex!=desired) SetWindowLongPtrW(g_presenter,GWL_EXSTYLE,desired);
    SetLastError(ERROR_SUCCESS);
    const BOOL ok=SetWindowPos(g_presenter,HWND_NOTOPMOST,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOSENDCHANGING);
    ex=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);
    if(!ok || (ex&WS_EX_TOPMOST)) {
        InterlockedIncrement64(&g_rc49TopmostFailures);
        logfmt("FAIL RC49 WINDOWED DETOPMOST",ok,ex,GetLastError(),0);
        return false;
    }
    InterlockedIncrement64(&g_rc49TopmostClears);
    logfmt("RC49_WINDOWED_DETOPMOST_COMMITTED",ex,GetParent(g_presenter)==g_game,0,0);
    return true;
}

static bool rc49_windowed_state_ok() noexcept {
    if(!rc48_windowed_geometry_ok() || GetParent(g_presenter)!=g_game) return false;
    const LONG_PTR s=GetWindowLongPtrW(g_presenter,GWL_STYLE);
    const LONG_PTR ex=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);
    if(!(s&WS_CHILD) || (s&WS_POPUP)) return false;
    if(ex&(WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_APPWINDOW|WS_EX_TOOLWINDOW)) return false;
    return true;
}

''' + needle
if needle not in s:
    raise SystemExit('RC49 patch guard: make_child anchor missing')
s = s.replace(needle, helper, 1)

needle = "    if(ex!=desiredEx){SetWindowLongPtrW(g_presenter,GWL_EXSTYLE,desiredEx);changed=true;}\n\n    bool geometryWasBad=!rc48_windowed_geometry_ok();\n"
replacement = "    if(ex!=desiredEx){SetWindowLongPtrW(g_presenter,GWL_EXSTYLE,desiredEx);changed=true;}\n    if(!rc49_clear_windowed_topmost()) return false;\n\n    bool geometryWasBad=!rc48_windowed_geometry_ok();\n"
if needle not in s:
    raise SystemExit('RC49 patch guard: exstyle block missing')
s = s.replace(needle, replacement, 1)

needle = "        const LONG_PTR s=GetWindowLongPtrW(g_presenter,GWL_STYLE);\n        if(mode==0){\n            const bool geometryBad=!rc48_windowed_geometry_ok();\n            if(!(s&WS_CHILD) || (s&WS_POPUP) || GetParent(g_presenter)!=g_game || geometryBad)\n                rc48_make_child(geometryBad);\n"
replacement = "        const LONG_PTR s=GetWindowLongPtrW(g_presenter,GWL_STYLE);\n        if(mode==0){\n            const bool geometryBad=!rc48_windowed_geometry_ok();\n            if(!rc49_windowed_state_ok())\n                rc48_make_child(geometryBad);\n"
if needle not in s:
    raise SystemExit('RC49 patch guard: invariant block missing')
s = s.replace(needle, replacement, 1)

needle = "BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID reserved){\n"
query = r'''struct PTARRC49State {
    UINT size;
    UINT installed;
    UINT windowed;
    UINT stable;
    UINT presenterIsChild;
    UINT presenterTopmost;
    ULONG_PTR presenterStyle;
    ULONG_PTR presenterExStyle;
    unsigned long long childAttach;
    unsigned long long repairs;
    unsigned long long topmostClears;
    unsigned long long topmostFailures;
};

extern "C" __declspec(dllexport) int WINAPI PTAR_RC49_Query(PTARRC49State* out){
    if(!out||out->size<sizeof(PTARRC49State)) return -1;
    PTARRC49State q{};q.size=sizeof(q);
    q.installed=InterlockedCompareExchange(&g_rc45Installed,0,0)?1u:0u;
    q.windowed=rc48_windowed_mode()?1u:0u;
    q.stable=(q.installed&&q.windowed&&rc49_windowed_state_ok())?1u:0u;
    q.presenterIsChild=(IsWindow(g_presenter)&&GetParent(g_presenter)==g_game)?1u:0u;
    q.presenterStyle=IsWindow(g_presenter)?(ULONG_PTR)GetWindowLongPtrW(g_presenter,GWL_STYLE):0;
    q.presenterExStyle=IsWindow(g_presenter)?(ULONG_PTR)GetWindowLongPtrW(g_presenter,GWL_EXSTYLE):0;
    q.presenterTopmost=(q.presenterExStyle&WS_EX_TOPMOST)?1u:0u;
    q.childAttach=(unsigned long long)InterlockedCompareExchange64(&g_rc48ChildAttach,0,0);
    q.repairs=(unsigned long long)InterlockedCompareExchange64(&g_rc48Repairs,0,0);
    q.topmostClears=(unsigned long long)InterlockedCompareExchange64(&g_rc49TopmostClears,0,0);
    q.topmostFailures=(unsigned long long)InterlockedCompareExchange64(&g_rc49TopmostFailures,0,0);
    *out=q;return 0;
}

''' + needle
if needle not in s:
    raise SystemExit('RC49 patch guard: DllMain anchor missing')
s = s.replace(needle, query, 1)

s = s.replace('RC48_CHILD_PRESENTER_MODULE_LOADED', 'RC49_DETOPMOST_STABILITY_MODULE_LOADED', 1)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8', newline='\n')
print(f'RC49_SOURCE_GENERATED={out}')
print(f'RC49_SOURCE_BYTES={out.stat().st_size}')
