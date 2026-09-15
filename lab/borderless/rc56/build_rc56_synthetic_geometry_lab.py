from pathlib import Path
import shutil
import sys


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: build_rc56_synthetic_geometry_lab.py <generated_dir> <synthetic_dir>")
    src = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)

    for name in (
        "ptar_borderless_rc45_presenter_only.cpp",
        "ptar_borderless_rc46_presenter_only.cpp",
        "ptar_borderless_rc56_prod.cpp",
        "RC56_GENERATION.txt",
    ):
        shutil.copy2(src / name, out / name)

    # RC45 monitor gate. This path is retained in the generated RC56 source and
    # is still reachable through inherited RC45 entry points, so keep the lab
    # variant coherent even though production remains untouched.
    p45 = out / "ptar_borderless_rc45_presenter_only.cpp"
    text45 = p45.read_text(encoding="utf-8-sig")
    old45 = '''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);\n    if(mw!=outputW||mh!=outputH){logfmt("FAIL RC45 output/monitor mismatch",mw,mh,outputW,outputH);rc45_starting_clear();return -12;}\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;\n    g_monitor=mi.rcMonitor;g_rc45Monitor=mi.rcMonitor;'''
    new45 = '''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);\n    RECT effectiveMonitor=mi.rcMonitor;\n    if(mw!=outputW||mh!=outputH){\n        // LAB ONLY: GitHub hosted runners do not expose an exact 1.5x display mode.\n        effectiveMonitor.right=effectiveMonitor.left+(LONG)outputW;\n        effectiveMonitor.bottom=effectiveMonitor.top+(LONG)outputH;\n        logfmt("RC56_SYNTHETIC_MONITOR_OVERRIDE_RC45",mw,mh,outputW,outputH);\n    }\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;\n    g_monitor=effectiveMonitor;g_rc45Monitor=effectiveMonitor;'''
    text45 = replace_once(text45, old45, new45, "synthetic RC45 monitor override")
    p45.write_text(text45, encoding="utf-8", newline="\n")

    # RC46 is the actual exported attach/autostart layer in RC56. Its own monitor
    # guard executes before RC45 state is installed, so it must be syntheticized
    # separately for the CI-only off-screen 1920x1080 monitor rectangle.
    p46 = out / "ptar_borderless_rc46_presenter_only.cpp"
    text46 = p46.read_text(encoding="utf-8-sig")
    old46 = '''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);if(mw!=outputW||mh!=outputH){logfmt("FAIL RC46 output/monitor mismatch",mw,mh,outputW,outputH);InterlockedExchange(&g_rc45Starting,0);return -12;}\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=mi.rcMonitor;g_rc45Monitor=mi.rcMonitor;'''
    new46 = '''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);\n    RECT effectiveMonitor=mi.rcMonitor;\n    if(mw!=outputW||mh!=outputH){\n        effectiveMonitor.right=effectiveMonitor.left+(LONG)outputW;\n        effectiveMonitor.bottom=effectiveMonitor.top+(LONG)outputH;\n        logfmt("RC56_SYNTHETIC_MONITOR_OVERRIDE",mw,mh,outputW,outputH);\n    }\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=effectiveMonitor;g_rc45Monitor=effectiveMonitor;'''
    text46 = replace_once(text46, old46, new46, "synthetic RC46 monitor override")
    p46.write_text(text46, encoding="utf-8", newline="\n")

    marker = out / "RC56_SYNTHETIC_GEOMETRY.txt"
    marker.write_text(
        "RC56_SYNTHETIC_GEOMETRY=PASS\n"
        "PRODUCTION_SOURCE=UNCHANGED\n"
        "LAB_OUTPUT=1920x1080\n"
        "LAB_RENDER=1280x720\n"
        "OVERRIDE=RC45_RC46_MONITOR_RECT_ONLY\n",
        encoding="ascii",
        newline="\n",
    )
    print(marker.read_text(encoding="ascii"), end="")


if __name__ == "__main__":
    main()
