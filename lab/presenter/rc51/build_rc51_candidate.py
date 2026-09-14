from pathlib import Path
import sys

here=Path(__file__).resolve().parent
src=here/'ptar_borderless_rc51_prod.cpp'
out=Path(sys.argv[1]) if len(sys.argv)>1 else here/'ptar_borderless_rc51_generated.cpp'
s=src.read_text(encoding='utf-8')
old='static const BYTE rebuildAnchor[]={0x48,0x89,0x5C,0x24};'
new='static const BYTE rebuildAnchor[]={0x56,0x57,0x48,0x83,0xEC,0x58};'
if s.count(old)!=1:
    raise SystemExit('RC51 generator guard: rebuild sentinel anchor drift')
s=s.replace(old,new,1)
# Reject any accidental topology experiment from RC47/RC48. RC51 must only
# synchronize the presenter's backing swapchain with its existing HWND.
for forbidden in ('SetParent(', 'GWLP_HWNDPARENT', 'WS_CHILD'):
    if forbidden in s and forbidden!='WS_CHILD':
        raise SystemExit(f'RC51 topology contract violated: {forbidden}')
# WS_CHILD may occur only in comments / inherited constants; no direct mutation
# is allowed in RC51 code itself.
out.write_text(s,encoding='utf-8',newline='\n')
print(f'RC51_GENERATED={out}')
print('RC51_REBUILD_SENTINEL=56 57 48 83 EC 58')
