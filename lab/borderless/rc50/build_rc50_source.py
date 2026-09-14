from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
src = ROOT / 'lab' / 'borderless' / 'rc46' / 'ptar_borderless_rc46_prod.cpp'
out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name('ptar_borderless_rc50_generated.cpp')
s = src.read_text(encoding='utf-8')

# RC50 intentionally returns to the RC46 top-level/unowned presenter topology.
# It changes only one invariant: whenever Windows confirms a Windowed game
# WINDOWPOS change, immediately reassert the presenter above the game. No owner,
# parent or WS_CHILD mutation is introduced.
anchor = 'static WNDPROC g_rc46PresenterNext=nullptr;\n'
if anchor not in s:
    raise SystemExit('RC50 guard: globals anchor missing')
s = s.replace(anchor, anchor + 'static volatile LONG64 g_rc50ZOrderReasserts=0;\nstatic volatile LONG g_rc50FirstRaiseLogged=0;\n', 1)

old = '            rc46_restore_real_window_if_hijacked();\n            rc46_force_presenter_to_target((m==WM_ACTIVATE&&LOWORD(w)!=WA_INACTIVE)||m==WM_SETFOCUS||m==WM_SHOWWINDOW);\n'
new = '''            rc46_restore_real_window_if_hijacked();
            const bool rc50Raise=(m==WM_WINDOWPOSCHANGED)||
                (m==WM_ACTIVATE&&LOWORD(w)!=WA_INACTIVE)||m==WM_SETFOCUS||m==WM_SHOWWINDOW;
            rc46_force_presenter_to_target(rc50Raise);
            if(m==WM_WINDOWPOSCHANGED){
                InterlockedIncrement64(&g_rc50ZOrderReasserts);
                if(InterlockedCompareExchange(&g_rc50FirstRaiseLogged,1,0)==0)
                    logline("RC50_WINDOWED_ZORDER_REASSERT_ACTIVE");
            }
'''
if old not in s:
    raise SystemExit('RC50 guard: RC46 event block missing')
s = s.replace(old, new, 1)

# Add an independent diagnostic export without changing inherited ABI.
anchor = 'static bool rc46_runtime_layout_ok(HMODULE runtime,BYTE*& base,IMAGE_NT_HEADERS64*& nt){return rc45_runtime_layout_ok(runtime,base,nt);}\n'
extra = '''extern "C" __declspec(dllexport) unsigned long long WINAPI PTAR_RC50_ZOrderReasserts(){
    return (unsigned long long)InterlockedCompareExchange64(&g_rc50ZOrderReasserts,0,0);
}

''' + anchor
if anchor not in s:
    raise SystemExit('RC50 guard: export anchor missing')
s = s.replace(anchor, extra, 1)

s = s.replace('ACTIVE_RC46_WINDOWED_GEOMETRY_GUARD game/presenter', 'ACTIVE_RC50_TOPLEVEL_ZORDER_GUARD game/presenter', 1)
s = s.replace('AUTO_START_RC46_WINDOWED_GEOMETRY_GUARD geometry', 'AUTO_START_RC50_TOPLEVEL_ZORDER_GUARD geometry', 1)

# Hard architectural contract: this candidate must never become RC47/RC48 again.
if 'SetParent(' in s or 'GWLP_HWNDPARENT' in s or 'WS_CHILD' in s:
    raise SystemExit('RC50 contract violated: parent/owner/child mutation found')

out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8', newline='\n')
print(f'RC50_SOURCE_GENERATED={out}')
print('RC50_ARCHITECTURE=TOPLEVEL_UNOWNED_EVENT_ZORDER_ONLY')
