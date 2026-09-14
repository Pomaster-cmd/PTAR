from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
src = ROOT / 'lab' / 'borderless' / 'rc48' / 'ptar_rc48_true_runtime_host.cpp'
out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name('ptar_rc50_hostile_true_runtime_host.cpp')
s = src.read_text(encoding='utf-8')

old = 'last=swap->Present(1,0);pump();if(FAILED(last))'
new = '''last=swap->Present(1,0);pump();
        // Exact RC46 field regression pressure: P1U46/game policy repeatedly
        // raises the opaque game HWND. An unowned top-level presenter must be
        // reinserted above it after the game z-order transaction completes.
        SetWindowPos(game,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        pump();
        if(FAILED(last))'''
if old not in s:
    raise SystemExit('hostile host guard: present loop anchor missing')
s = s.replace(old, new, 1)

s = s.replace('TRUE_RUNTIME_HOST_BEGIN', 'HOSTILE_TRUE_RUNTIME_HOST_BEGIN', 1)
s = s.replace('TRUE_P1U46_WINDOWED_VISIBLE_PIXELS=', 'HOSTILE_P1U46_WINDOWED_VISIBLE_PIXELS=', 1)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8', newline='\n')
print(f'RC50_HOSTILE_HOST_GENERATED={out}')
