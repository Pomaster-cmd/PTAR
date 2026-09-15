from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


def build(out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)

    rc45 = read("lab/borderless/rc45/ptar_borderless_rc45_prod.cpp")
    # The generated copy lives one directory deeper than the canonical RC45
    # source, so preserve its dependencies with explicit relocated paths.
    rc45 = replace_once(rc45, '#include "../rc38/ptar_borderless_rc38.cpp"', '#include "../../rc38/ptar_borderless_rc38.cpp"', "relocate RC38 include")
    rc45 = replace_once(rc45, '#include "../rc43/ptar_borderless_mode_policy.h"', '#include "../../rc43/ptar_borderless_mode_policy.h"', "relocate RC43 include")
    rc45 = replace_once(rc45, '#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"', '#include "../../../raster/rc41b/ptar_rc41b_bootstrap.h"', "relocate bootstrap include")

    helper_anchor = "static void rc45_follow_windowed_presenter(bool raiseZ) noexcept {"
    helper = r'''static void rc56_presenter_borderless_only(bool raiseZ) noexcept {
    if(!IsWindow(g_presenter)) return;
    const RECT target=g_rc45Monitor;
    if(target.right<=target.left || target.bottom<=target.top) return;
    g_monitor=target;
    InterlockedExchange(&g_internal,1);
    rc45_set_presenter_window_style();
    const HWND after=raiseZ?HWND_TOP:nullptr;
    const UINT flags=SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW|(raiseZ?0u:SWP_NOZORDER);
    SetWindowPos(g_presenter,after,target.left,target.top,target.right-target.left,target.bottom-target.top,flags);
    InterlockedExchange(&g_internal,0);
    InterlockedIncrement64(&g_rc45PresenterFollows);
}

'''
    rc45 = replace_once(rc45, helper_anchor, helper + helper_anchor,
                        "insert presenter-only helper")

    old_enter = r'''static void rc45_enter_borderless(const char* reason) noexcept {
    const bool wasBorderless=InterlockedCompareExchange(&g_rc45Borderless,0,0)!=0;
    if(!wasBorderless) rc45_capture_windowed_state();
    InterlockedExchange(&g_rc45Borderless,1);
    InterlockedExchange(&g_active,1);
    g_monitor=g_rc45Monitor;
    enforce_geometry();
    if(!wasBorderless) InterlockedIncrement64(&g_rc45ToBorderless);
    logline(reason);
}'''
    new_enter = r'''static void rc45_enter_borderless(const char* reason) noexcept {
    const bool wasBorderless=InterlockedCompareExchange(&g_rc45Borderless,0,0)!=0;
    if(!wasBorderless) rc45_capture_windowed_state();
    InterlockedExchange(&g_rc45Borderless,1);
    InterlockedExchange(&g_active,1);
    g_monitor=g_rc45Monitor;
    // RC56: P1U46 owns the low-resolution game HWND and explicitly requires it
    // to stay windowed. Borderless therefore means presenter-only geometry.
    rc56_presenter_borderless_only(true);
    if(!wasBorderless) InterlockedIncrement64(&g_rc45ToBorderless);
    logline("RC56_BORDERLESS_POLICY=PRESENTER_ONLY_GAME_UNTOUCHED");
    logline(reason);
}'''
    rc45 = replace_once(rc45, old_enter, new_enter, "replace borderless entry")

    old_repeat = "else {g_monitor=g_rc45Monitor;enforce_geometry();}"
    new_repeat = "else {g_monitor=g_rc45Monitor;rc56_presenter_borderless_only(false);}"
    rc45 = replace_once(rc45, old_repeat, new_repeat,
                        "replace repeated borderless enforcement")

    old_style_changing = r'''            g_monitor=g_rc45Monitor;
            return GameProc(h,m,w,l);'''
    new_style_changing = r'''            g_monitor=g_rc45Monitor;
            // Do not let the legacy RC38 authority rewrite the game HWND.
            // The P1U46 WndProc remains next in chain and owns game-window policy.
            rc56_presenter_borderless_only(false);
            return call_next(g_gameNext,h,m,w,l);'''
    rc45 = replace_once(rc45, old_style_changing, new_style_changing,
                        "replace borderless WM_STYLECHANGING")

    old_style_changed = r'''            g_monitor=g_rc45Monitor;
            const LRESULT r=GameProc(h,m,w,l);
            enforce_geometry();
            if(!wasBorderless) InterlockedIncrement64(&g_rc45ToBorderless);
            return r;'''
    new_style_changed = r'''            g_monitor=g_rc45Monitor;
            const LRESULT r=call_next(g_gameNext,h,m,w,l);
            rc56_presenter_borderless_only(false);
            if(!wasBorderless) InterlockedIncrement64(&g_rc45ToBorderless);
            return r;'''
    rc45 = replace_once(rc45, old_style_changed, new_style_changed,
                        "replace borderless WM_STYLECHANGED")

    old_dispatch = "    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)) return GameProc(h,m,w,l);"
    new_dispatch = r'''    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)) {
        // RC56: the game WndProc chain is never replaced by RC38 borderless
        // authority. Only the separate presenter occupies the monitor.
        const LRESULT r=call_next(g_gameNext,h,m,w,l);
        if(!internal && (m==WM_WINDOWPOSCHANGED || m==WM_SIZE || m==WM_MOVE ||
                         m==WM_SHOWWINDOW || m==WM_ACTIVATE || m==WM_SETFOCUS)) {
            if(!(m==WM_SIZE && w==SIZE_MINIMIZED))
                rc56_presenter_borderless_only((m==WM_ACTIVATE&&LOWORD(w)!=WA_INACTIVE)||m==WM_SETFOCUS||m==WM_SHOWWINDOW);
        }
        return r;
    }'''
    rc45 = replace_once(rc45, old_dispatch, new_dispatch,
                        "replace borderless dispatch")

    if "enforce_geometry();" in rc45:
        raise RuntimeError("RC56 generated RC45 still contains direct enforce_geometry call")
    if "return GameProc(h,m,w,l);" in rc45:
        raise RuntimeError("RC56 generated RC45 still routes game HWND through legacy GameProc")

    write(out_dir / "ptar_borderless_rc45_presenter_only.cpp", rc45)

    rc46 = read("lab/borderless/rc46/ptar_borderless_rc46_prod.cpp")
    rc46 = replace_once(
        rc46,
        '#include "../rc45/ptar_borderless_rc45_prod.cpp"',
        '#include "ptar_borderless_rc45_presenter_only.cpp"',
        "redirect RC46 to presenter-only RC45",
    )
    rc46 = rc46.replace("RC46_WINDOWED_GEOMETRY_GUARD", "RC56_PRESENTER_ONLY_BORDERLESS")
    write(out_dir / "ptar_borderless_rc46_presenter_only.cpp", rc46)

    rc51 = read("lab/presenter/rc51/ptar_borderless_rc51_prod.cpp")
    rc51 = replace_once(
        rc51,
        '#include "../../borderless/rc46/ptar_borderless_rc46_prod.cpp"',
        '#include "ptar_borderless_rc46_presenter_only.cpp"',
        "redirect RC51 to RC56 presenter-only RC46",
    )
    rc51 = rc51.replace(
        "// RC51 keeps RC46's field-proven window geometry/input policy byte-for-byte at\n// source level. The only new behaviour is a presenter-swapchain backing-size\n// reconciliation executed on the P1U46 presenter Present thread after a frame\n// has been submitted. No SetParent/owner/WS_CHILD policy is introduced.",
        "// RC56 retains RC51 presenter swapchain reconciliation and RC55 raster behavior,\n// but changes Borderless authority only: P1U46 keeps the game HWND/windowed DXGI\n// contract, while the separate presenter alone occupies the native monitor.\n// No SetParent/owner/WS_CHILD policy is introduced."
    )
    write(out_dir / "ptar_borderless_rc56_prod.cpp", rc51)

    summary = [
        "RC56_GENERATION=PASS",
        "BASE=RC51+RC46+RC45",
        "BORDERLESS_AUTHORITY=PRESENTER_ONLY",
        "GAME_HWND_BORDERLESS_MUTATION=DISABLED",
        "RC51_PRESENTER_RESYNC=UNCHANGED",
        "RC55_RASTER=UNCHANGED",
    ]
    write(out_dir / "RC56_GENERATION.txt", "\n".join(summary) + "\n")


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "lab/borderless/rc56/generated"
    build(out)
