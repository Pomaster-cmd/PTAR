from pathlib import Path
import shutil
import sys


def once(t,o,n,label):
    c=t.count(o)
    if c!=1: raise RuntimeError(f'{label}: expected 1 got {c}')
    return t.replace(o,n,1)


def main():
    if len(sys.argv)!=3: raise SystemExit('usage: build_rc58_synthetic_geometry.py <generated> <out>')
    src=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
    for p in src.iterdir():
        if p.is_file(): shutil.copy2(p,out/p.name)

    p45=out/'ptar_borderless_rc45_presenter_only.cpp';t=p45.read_text(encoding='utf-8-sig')
    old='''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);\n    if(mw!=outputW||mh!=outputH){logfmt("FAIL RC45 output/monitor mismatch",mw,mh,outputW,outputH);rc45_starting_clear();return -12;}\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;\n    g_monitor=mi.rcMonitor;g_rc45Monitor=mi.rcMonitor;'''
    new='''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);\n    RECT effectiveMonitor=mi.rcMonitor;\n    if(mw!=outputW||mh!=outputH){effectiveMonitor.right=effectiveMonitor.left+(LONG)outputW;effectiveMonitor.bottom=effectiveMonitor.top+(LONG)outputH;logfmt("RC58_SYNTHETIC_MONITOR_OVERRIDE_RC45",mw,mh,outputW,outputH);}\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;\n    g_monitor=effectiveMonitor;g_rc45Monitor=effectiveMonitor;'''
    t=once(t,old,new,'RC45 monitor');p45.write_text(t,encoding='utf-8',newline='\n')

    p46=out/'ptar_borderless_rc46_presenter_only.cpp';t=p46.read_text(encoding='utf-8-sig')
    old='''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);if(mw!=outputW||mh!=outputH){logfmt("FAIL RC46 output/monitor mismatch",mw,mh,outputW,outputH);InterlockedExchange(&g_rc45Starting,0);return -12;}\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=mi.rcMonitor;g_rc45Monitor=mi.rcMonitor;'''
    new='''    const UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);RECT effectiveMonitor=mi.rcMonitor;if(mw!=outputW||mh!=outputH){effectiveMonitor.right=effectiveMonitor.left+(LONG)outputW;effectiveMonitor.bottom=effectiveMonitor.top+(LONG)outputH;logfmt("RC58_SYNTHETIC_MONITOR_OVERRIDE",mw,mh,outputW,outputH);}\n    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=effectiveMonitor;g_rc45Monitor=effectiveMonitor;'''
    t=once(t,old,new,'RC46 monitor');p46.write_text(t,encoding='utf-8',newline='\n')
    (out/'RC58_SYNTHETIC_GEOMETRY.txt').write_text('RC58_SYNTHETIC_GEOMETRY=PASS\nPRODUCTION_UNCHANGED=1\nRENDER=1280x720\nOUTPUT=1920x1080\n',encoding='ascii',newline='\n')
    print('RC58_SYNTHETIC_GEOMETRY=PASS')

if __name__=='__main__': main()
