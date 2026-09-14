from pathlib import Path
import sys

src = Path('lab/presenter/rc51/ptar_rc51_auto_stress_host.cpp')
out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('lab/presenter/rc52/ptar_rc52_field_faithful_generated.cpp')
s = src.read_text(encoding='utf-8')
old = "const UINT outW=(UINT)GetSystemMetrics(SM_CXSCREEN),outH=(UINT)GetSystemMetrics(SM_CYSCREEN),renderW=(outW*2u)/3u,renderH=(outH*2u)/3u;"
new = "const UINT screenW=(UINT)GetSystemMetrics(SM_CXSCREEN),screenH=(UINT)GetSystemMetrics(SM_CYSCREEN),outW=(screenW/3u)*3u,outH=(screenH/3u)*3u,renderW=(outW*2u)/3u,renderH=(outH*2u)/3u;"
if s.count(old) != 1:
    raise SystemExit('RC52 field-host guard: geometry anchor drift')
s = s.replace(old, new, 1)
s = s.replace('RC51_AUTO_STRESS.txt', 'RC52_FIELD_SCALE_ORACLE.txt')
s = s.replace('RC51_AUTO_BEGIN', 'RC52_FIELD_SCALE_BEGIN')
s = s.replace('RC51_AUTO_STRESS=PASS', 'RC52_FIELD_SCALE_ORACLE=PASS')
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding='utf-8', newline='\n')
print(f'RC52_FIELD_HOST_GENERATED={out}')
print('RC52_GEOMETRY_POLICY=largest-screen-contained exact x1.5 output; render=2/3 output')
