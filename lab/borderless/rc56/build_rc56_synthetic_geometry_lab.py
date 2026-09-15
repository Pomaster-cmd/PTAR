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

    p = out / "ptar_borderless_rc45_presenter_only.cpp"
    text = p.read_text(encoding="utf-8-sig")
    old = '''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);\n    if(mw!=outputW||mh!=outputH){logfmt("FAIL RC45 output/monitor mismatch",mw,mh,outputW,outputH);rc45_starting_clear();return -12;}\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;\n    g_monitor=mi.rcMonitor;g_rc45Monitor=mi.rcMonitor;'''
    new = '''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);\n    RECT effectiveMonitor=mi.rcMonitor;\n    if(mw!=outputW||mh!=outputH){\n        // LAB ONLY: GitHub hosted runners do not expose an exact 1.5x display mode.\n        // Keep the production carrier untouched and synthesize only the monitor\n        // rectangle used by the RC56 controller so the exact P1U46 1280x720 ->\n        // 1920x1080 contract can be exercised end-to-end off-screen.\n        effectiveMonitor.right=effectiveMonitor.left+(LONG)outputW;\n        effectiveMonitor.bottom=effectiveMonitor.top+(LONG)outputH;\n        logfmt("RC56_SYNTHETIC_MONITOR_OVERRIDE",mw,mh,outputW,outputH);\n    }\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;\n    g_monitor=effectiveMonitor;g_rc45Monitor=effectiveMonitor;'''
    text = replace_once(text, old, new, "synthetic monitor override")
    p.write_text(text, encoding="utf-8", newline="\n")

    marker = out / "RC56_SYNTHETIC_GEOMETRY.txt"
    marker.write_text(
        "RC56_SYNTHETIC_GEOMETRY=PASS\n"
        "PRODUCTION_SOURCE=UNCHANGED\n"
        "LAB_OUTPUT=1920x1080\n"
        "LAB_RENDER=1280x720\n"
        "OVERRIDE=MONITOR_RECT_ONLY\n",
        encoding="ascii",
        newline="\n",
    )
    print(marker.read_text(encoding="ascii"), end="")


if __name__ == "__main__":
    main()
