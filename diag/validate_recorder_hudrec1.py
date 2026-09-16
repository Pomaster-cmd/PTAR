#!/usr/bin/env python3
"""Static gate for SAFEPOINT11 HUDREC1."""
from pathlib import Path
import hashlib, struct, sys
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
DLL=ROOT/'payload/d3d11.dll'; MIRROR=ROOT/'payload/win81_nis_dx11_x64.dll'
EXPECTED='e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429'
BASE='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d)))
    print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def pe_info(data):
    pe=struct.unpack_from('<I',data,0x3c)[0]; n=struct.unpack_from('<H',data,pe+6)[0]; opt=struct.unpack_from('<H',data,pe+20)[0]; so=pe+24+opt
    secs=[]
    for i in range(n):
        o=so+i*40; nm=data[o:o+8].split(b'\0')[0].decode('ascii','replace'); vs,va,rs,rp=struct.unpack_from('<IIII',data,o+8); secs.append((nm,va,vs,rp,rs))
    return pe,secs
def off(rva,secs):
    for nm,va,vs,rp,rs in secs:
        if va<=rva<va+max(vs,rs): return rp+(rva-va)
    raise ValueError(hex(rva))
def rip_target(data,rva,disp_off,instr_len,secs):
    o=off(rva,secs); disp=struct.unpack_from('<i',data,o+disp_off)[0]; return rva+instr_len+disp

a=DLL.read_bytes(); b=MIRROR.read_bytes(); pe,secs=pe_info(a)
ck('runtime exact HUDREC1',sha(DLL)==EXPECTED,sha(DLL))
ck('mirror exact HUDREC1',sha(MIRROR)==EXPECTED,sha(MIRROR))
ck('runtime mirrors exact',a==b)
ck('runtime size unchanged',len(a)==320000,len(a))
rva=0x34FE9B9; o=off(rva,secs)
ck('native wrapper mov signature',a[o:o+3]==bytes.fromhex('48 8b 0d'),a[o:o+7].hex())
ck('FG-OFF recorder uses native presenter swapchain',rip_target(a,rva,3,7,secs)==0x02C7DFC8,hex(rip_target(a,rva,3,7,secs)))
call=0x34FEA16; co=off(call,secs)
ck('native guarded submit call opcode',a[co]==0xE8,hex(a[co]))
if a[co]==0xE8:
    tgt=call+5+struct.unpack_from('<i',a,co+1)[0]
    ck('native guarded submit target unchanged',tgt==0x34FE804,hex(tgt))
for rva,pat,name in [
    (0x25B34,bytes.fromhex('4c 8b 05'),'FG-ON recorder texture load'),
    (0x25B49,b'\xe8','FG-ON recorder wrapper call'),
    (0x27589,bytes.fromhex('48 83 3d'),'F9 isolated backbuffer presence check'),
    (0x2764A,bytes.fromhex('4c 8b 05'),'F9 isolated source load')]:
    oo=off(rva,secs); ck(name+' signature',a[oo:oo+len(pat)]==pat,a[oo:oo+8].hex())
ck('FG-ON recorder texture is isolated final backbuffer',rip_target(a,0x25B34,3,7,secs)==0x02C93CE8,hex(rip_target(a,0x25B34,3,7,secs)))
ck('F9 isolated source is same final backbuffer',rip_target(a,0x2764A,3,7,secs)==0x02C93CE8,hex(rip_target(a,0x2764A,3,7,secs)))
fo=off(0x14ACD,secs)
ck('native F9 presenter selector signature',a[fo:fo+3]==bytes.fromhex('48 8b 05'),a[fo:fo+7].hex())
ck('native F9 selects same presenter swapchain',rip_target(a,0x14ACD,3,7,secs)==0x02C7DFC8,hex(rip_target(a,0x14ACD,3,7,secs)))
def call_target(data,rva,secs):
    oo=off(rva,secs)
    if data[oo] != 0xE8: return None
    return rva+5+struct.unpack_from('<i',data,oo+1)[0]
order_ok=True
for hud_call,rec_call in [(0xC80B,0xC810),(0xC909,0xC90E),(0xD6EB,0xD6F0),(0xD7E9,0xD7EE)]:
    order_ok &= call_target(a,hud_call,secs)==0x13CE0
    order_ok &= call_target(a,rec_call,secs)==0x34FE980
    order_ok &= rec_call==hud_call+5
ck('native HUD draw precedes recorder at all four routes',order_ok)
hudlog=b'OK: simplified active HUD drawn on native USR presenter'
hpos=a.find(hudlog)
ck('native HUD success string present',hpos>=0)
peoff=struct.unpack_from('<I',a,0x3c)[0]
ck('PE machine x64',struct.unpack_from('<H',a,peoff+4)[0]==0x8664,hex(struct.unpack_from('<H',a,peoff+4)[0]))
opt=peoff+24
ck('PE32+ magic',struct.unpack_from('<H',a,opt)[0]==0x20B,hex(struct.unpack_from('<H',a,opt)[0]))
maj_sub,min_sub=struct.unpack_from('<HH',a,opt+48)
ck('PE subsystem version unchanged from validated SAFEPOINT11',(maj_sub,min_sub)==(6,0),f'{maj_sub}.{min_sub}')
BASEFILE=ROOT/'diag/base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll'
ck('canonical SAFEPOINT11 base bundled exact',BASEFILE.is_file() and sha(BASEFILE)==BASE,sha(BASEFILE) if BASEFILE.is_file() else 'missing')
if BASEFILE.is_file():
    base_bytes=BASEFILE.read_bytes()
    diffs=[i for i,(x,y) in enumerate(zip(base_bytes,a)) if x!=y]
    ck('HUDREC1 exact 3-byte whole-binary delta',len(base_bytes)==len(a) and diffs==[0xD0,0xD1,0x4Cbbc],','.join(hex(x) for x in diffs))
    ck('only functional byte outside PE checksum', [x for x in diffs if x not in (0xD0,0xD1)]==[0x4Cbbc],','.join(hex(x) for x in diffs))
import tempfile, subprocess
with tempfile.TemporaryDirectory(prefix='ptar_hudrec1_') as td:
    rebuilt=Path(td)/'d3d11.dll'
    cp=subprocess.run([sys.executable,str(ROOT/'diag/patch_gw16h_recorder_hudrec1.py'),str(BASEFILE),str(rebuilt)],capture_output=True,text=True)
    ck('HUDREC1 patcher exits PASS',cp.returncode==0 and 'HUDREC1=PASS' in cp.stdout,(cp.stdout+cp.stderr).strip().splitlines()[-1:] if (cp.stdout+cp.stderr).strip() else '')
    ck('HUDREC1 patcher reproduces payload exact',rebuilt.is_file() and rebuilt.read_bytes()==a,sha(rebuilt) if rebuilt.is_file() else 'missing')
ck('FG-ON isolated frame render call',call_target(a,0x25A8C,secs)==0x26D50,hex(call_target(a,0x25A8C,secs) or 0))
ck('FG-ON dedicated HUD draw occurs inside render helper',call_target(a,0x2750D,secs)==0x28750,hex(call_target(a,0x2750D,secs) or 0))
ck('FG-ON recorder call follows render/HUD call',0x25A8C < 0x25B49)
ck('dedicated HUD helper live FPS selector signature',rip_target(a,0x28957,3,7,secs)==0x02CCE5BC,hex(rip_target(a,0x28957,3,7,secs)))
ck('dedicated HUD helper fallback FPS selector signature',rip_target(a,0x28966,4,8,secs)==0x02CCE4FC,hex(rip_target(a,0x28966,4,8,secs)))
ini=(ROOT/'payload/win81_nis.ini').read_text(errors='replace')
ps=(ROOT/'diag/set_vblank_diagnostics.ps1').read_text(errors='replace')
bat=(ROOT/'diag/FG_MARKER_VISIBILITY.bat').read_text(errors='replace')
ck('Overlay remains enabled','Overlay=1' in ini)
ck('VBlankDiagnostics key present','VBlankDiagnostics=1' in ini)
ck('marker powershell writes VBlankDiagnostics','VBlankDiagnostics' in ps and '$Value' in ps)
ck('marker tool leaves Overlay untouched','Overlay=' not in ps)
ck('friendly marker menu has status/on/off',all(x in bat for x in ['-Value -1','-Value 1','-Value 0']))
ver=(ROOT/'payload/win81_nis_version.txt').read_text(errors='replace')
ck('HUDREC1 version marker','HUDREC1' in ver)
ck('HUD recording contract marker','RECORDER_HUD_CAPTURE=FINAL_PRESENTER_BACKBUFFER' in ver)
failed=[x for x in checks if not x[1]]
print('RECORDER_HUDREC1_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
