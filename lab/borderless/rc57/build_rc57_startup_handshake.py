from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'lab/borderless/rc56'))
import build_rc56_presenter_only as rc56


def once(text,old,new,label):
    c=text.count(old)
    if c!=1: raise RuntimeError(f'{label}: expected one match, got {c}')
    return text.replace(old,new,1)


def main():
    out=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'lab/borderless/rc57/generated'
    rc56.build(out)

    p45=out/'ptar_borderless_rc45_presenter_only.cpp'
    t=p45.read_text(encoding='utf-8')
    t=once(t,
        'static int g_rc45LastWindowStylePreference=-2;\n',
        'static int g_rc45LastWindowStylePreference=-2;\n'
        'static volatile LONG g_rc57InitialBorderlessDeferred=0;\n'
        'static volatile LONG g_rc57SawWindowedEdge=0;\n',
        'state')

    old='''static void rc45_apply_preference_on_game_thread(int preference) noexcept {\n    if(preference==1){\n        if(!InterlockedCompareExchange(&g_rc45Borderless,0,0))\n            rc45_enter_borderless("RC45_PREF_MODE=BORDERLESS");\n        else {g_monitor=g_rc45Monitor;rc56_presenter_borderless_only(false);}\n    } else if(preference>=0){\n        if(InterlockedCompareExchange(&g_rc45Borderless,0,0))\n            rc45_enter_windowed(true,"RC45_PREF_MODE=WINDOWED");\n        else rc45_follow_windowed_presenter(false);\n    }\n}\n'''
    new='''static void rc45_apply_preference_on_game_thread(int preference) noexcept {\n    if(preference==1){\n        if(InterlockedCompareExchange(&g_rc57InitialBorderlessDeferred,0,0)){\n            logline("RC57_BORDERLESS_REQUEST_DEFERRED_UNTIL_EXPLICIT_WINDOWED_EDGE");\n            if(InterlockedCompareExchange(&g_rc45Borderless,0,0)) rc45_enter_windowed(true,"RC57_DEFERRED_RETURN_WINDOWED");\n            else rc45_follow_windowed_presenter(false);\n            return;\n        }\n        if(!InterlockedCompareExchange(&g_rc45Borderless,0,0))\n            rc45_enter_borderless("RC57_PREF_MODE=BORDERLESS");\n        else {g_monitor=g_rc45Monitor;rc56_presenter_borderless_only(false);}\n    } else if(preference>=0){\n        if(InterlockedCompareExchange(&g_rc45Borderless,0,0))\n            rc45_enter_windowed(true,"RC57_PREF_MODE=WINDOWED");\n        else rc45_follow_windowed_presenter(false);\n    }\n}\n'''
    t=once(t,old,new,'apply preference')

    old='''        if(requested==ptar_rc43::RequestedWindowMode::Borderless){\n            const bool wasBorderless=InterlockedCompareExchange(&g_rc45Borderless,0,0)!=0;'''
    new='''        if(requested==ptar_rc43::RequestedWindowMode::Borderless){\n            if(InterlockedCompareExchange(&g_rc57InitialBorderlessDeferred,0,0)){\n                logline("RC57_STYLE_BORDERLESS_DEFERRED_DURING_STARTUP_HANDSHAKE");\n                return call_next(g_gameNext,h,m,w,l);\n            }\n            const bool wasBorderless=InterlockedCompareExchange(&g_rc45Borderless,0,0)!=0;'''
    if t.count(old)!=2: raise RuntimeError(f'borderless style branches: expected 2 got {t.count(old)}')
    t=t.replace(old,new,2)

    old='''        if(current>=0 && current!=last){\n            last=current;\n            g_rc45LastWindowStylePreference=current;\n            if(g_rc45SyncMessage) PostMessageW(g_game,g_rc45SyncMessage,static_cast<WPARAM>(current),0);\n        }'''
    new='''        if(current>=0 && current!=last){\n            last=current;\n            g_rc45LastWindowStylePreference=current;\n            if(InterlockedCompareExchange(&g_rc57InitialBorderlessDeferred,0,0)){\n                if(current==0){\n                    InterlockedExchange(&g_rc57SawWindowedEdge,1);\n                    logline("RC57_EXPLICIT_WINDOWED_EDGE_SEEN");\n                } else if(current==1){\n                    if(!InterlockedCompareExchange(&g_rc57SawWindowedEdge,0,0)){\n                        logline("RC57_BORDERLESS_EDGE_IGNORED_BEFORE_WINDOWED_HANDSHAKE");\n                        continue;\n                    }\n                    InterlockedExchange(&g_rc57InitialBorderlessDeferred,0);\n                    logline("RC57_STARTUP_HANDSHAKE_UNLOCKED");\n                }\n            }\n            if(g_rc45SyncMessage) PostMessageW(g_game,g_rc45SyncMessage,static_cast<WPARAM>(current),0);\n        }'''
    t=once(t,old,new,'preference worker')
    p45.write_text(t,encoding='utf-8',newline='\n')

    p46=out/'ptar_borderless_rc46_presenter_only.cpp'
    t=p46.read_text(encoding='utf-8')
    old='''    if(preference==1)rc45_enter_borderless("RC46_INITIAL_MODE=BORDERLESS_FROM_WINDOWSTYLE");\n    else if(preference>=0){rc45_enter_windowed(false,"RC46_INITIAL_MODE=WINDOWED_FROM_WINDOWSTYLE");rc46_restore_real_window_if_hijacked();rc46_force_presenter_to_target(true);}'''
    new='''    if(preference==1){\n        InterlockedExchange(&g_rc57InitialBorderlessDeferred,1);\n        InterlockedExchange(&g_rc57SawWindowedEdge,0);\n        rc45_enter_windowed(false,"RC57_INITIAL_BORDERLESS_DEFERRED_WINDOWED_BOOT");\n        rc46_restore_real_window_if_hijacked();rc46_force_presenter_to_target(true);\n        logline("RC57_STARTUP_HANDSHAKE=WINDOWED_BOOT_REQUIRE_0_THEN_1_EDGE");\n    }\n    else if(preference>=0){rc45_enter_windowed(false,"RC57_INITIAL_MODE=WINDOWED_FROM_WINDOWSTYLE");rc46_restore_real_window_if_hijacked();rc46_force_presenter_to_target(true);}'''
    t=once(t,old,new,'initial borderless')
    p46.write_text(t,encoding='utf-8',newline='\n')

    src=(out/'ptar_borderless_rc56_prod.cpp').read_text(encoding='utf-8')
    (out/'ptar_borderless_rc57_prod.cpp').write_text(src,encoding='utf-8',newline='\n')
    (out/'RC57_GENERATION.txt').write_text(
        'RC57_GENERATION=PASS\nBASE=RC56_PRESENTER_ONLY\nINITIAL_WINDOWSTYLE_1=DEFER_TO_WINDOWED_BOOT\nUNLOCK=EXPLICIT_WINDOWSTYLE_0_THEN_1\nRC55_RASTER=UNCHANGED\n',
        encoding='ascii',newline='\n')
    print('RC57_GENERATION=PASS')

if __name__=='__main__': main()
