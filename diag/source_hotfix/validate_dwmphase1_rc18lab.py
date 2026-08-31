#!/usr/bin/env python3
import hashlib, struct, subprocess, tempfile, sys
from pathlib import Path
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
BASE=ROOT/'diag/r/r071.dll'; OUT=ROOT/'win81_nis_dx11_x64.dll'; OBJ=ROOT/'diag/source_hotfix/dwmphase1_rc18.obj'; PATCH=ROOT/'diag/source_hotfix/patch_dwmphase1_rc18.py'
BASE_SHA='2b6fafabeedc89857dbb6d5a318ca143ce8e30ec912050116f70a4a58ad55ad3'
OUT_SHA='f5245d92ff942d58d8a8be890d46ed88b44a6d78eef6b43b9357fb8c031c4933'
VISIBLE_SHA='67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305'
checks=[]
def ck(n,c,d=''):
    ok=bool(c);checks.append(ok);print(('[PASS] ' if ok else '[FAIL] ')+n+((' :: '+d) if d else ''))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pe(b):
    e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+optsz;secs={}
    for i in range(n):
        o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii','replace');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);ch=struct.unpack_from('<I',b,o+36)[0];secs[name]=(va,vs,rp,rs,ch)
    return opt,secs
def off(secs,r):
    for va,vs,rp,rs,ch in secs.values():
        if va<=r<va+max(vs,rs):return rp+r-va
    raise KeyError(hex(r))
def target(b,secs,r):
    o=off(secs,r);raw=b[o:o+5]
    if raw[0]!=0xe8:return None
    return r+5+struct.unpack_from('<i',raw,1)[0]

ck('exact RC18 base reference',BASE.is_file() and sha(BASE)==BASE_SHA,sha(BASE) if BASE.exists() else 'missing')
ck('DWMPHASE1 runtime exact hash',OUT.is_file() and sha(OUT)==OUT_SHA,sha(OUT) if OUT.exists() else 'missing')
ck('visible verifier unchanged',sha(ROOT/'diag/visible_verifier/win81_vblank2_visible_marker_x64.exe')==VISIBLE_SHA)
A=BASE.read_bytes();B=OUT.read_bytes();oa,sa=pe(A);ob,sb=pe(B)
ck('new .dwmlab section present','.dwmlab' in sb)
if '.dwmlab' in sb:
    va,vs,rp,rs,ch=sb['.dwmlab'];ck('.dwmlab geometry',(va,vs,rp,rs)==(0x34FE000,0x2000,0x4C200,0x2000),str((hex(va),hex(vs),hex(rp),hex(rs))))
    ck('.dwmlab executable/read/write',(ch & 0xE0000000)==0xE0000000,hex(ch))
ck('Present call routed to DWM lab wrapper',target(B,sb,0x25B92)==0x34FE000,hex(target(B,sb,0x25B92) or 0))
# hotkey wrapper symbol is deterministically at 0x34FE448 in current object
ck('diagnostic dispatch routed into .dwmlab',target(B,sb,0x12225)==0x34FE448,hex(target(B,sb,0x12225) or 0))
ck('RC18 Present args unchanged',B[off(sb,0x265CF):off(sb,0x265CF)+8]==bytes.fromhex('ba010000004531c0'))
for r in (0x259C9,0x25B58):ck('prewait remains removed '+hex(r),B[off(sb,r):off(sb,r)+5]==b'\x90'*5)
for name in ('.fgov','.fgdat','.fgdia'):
    ava,avs,arp,ars,ach=sa[name];bva,bvs,brp,brs,bch=sb[name];ck(name+' byte-identical',A[arp:arp+ars]==B[brp:brp+brs])
# Existing .text changes are only the two five-byte call sites.
ata=A[sa['.text'][2]:sa['.text'][2]+sa['.text'][3]];bt=B[sb['.text'][2]:sb['.text'][2]+sb['.text'][3]]
d=[i for i,(x,y) in enumerate(zip(ata,bt)) if x!=y];ck('.text bounded to call redirects',len(d)<=10,f'{len(d)} changed bytes')
ck('dynamic dwmapi name present',b'd\x00w\x00m\x00a\x00p\x00i\x00.\x00d\x00l\x00l\x00\x00\x00' in B)
ck('DwmGetCompositionTimingInfo name present',b'DwmGetCompositionTimingInfo\0' in B)
ck('DWM lab banner present',b'RC18 LAB DWMPHASE1 PASSIVE DWM PHASE SNAPSHOT' in B)
ck('HUD DWM LAB encoding',b'm=max(m,Cx(p,o,0,68));m=max(m,Cx(p,o,1,87));m=max(m,Cx(p,o,2,77));m=max(m,Cx(p,o,3,32));' in B)
ck('HUD VISIB TEST encoding',b'if(t==10){m=max(m,Cx(p,o,0,86));m=max(m,Cx(p,o,1,73));m=max(m,Cx(p,o,2,83));m=max(m,Cx(p,o,3,73));m=max(m,Cx(p,o,4,66));m=max(m,Cx(p,o,6,84));m=max(m,Cx(p,o,7,69));m=max(m,Cx(p,o,8,83));m=max(m,Cx(p,o,9,84));return m;}' in B)
try:
    imp=subprocess.check_output(['objdump','-p',str(OUT)],text=True,stderr=subprocess.STDOUT)
    dlls=[ln.split(':',1)[1].strip().lower() for ln in imp.splitlines() if 'DLL Name:' in ln]
    ck('no static dwmapi import','dwmapi.dll' not in dlls,str(dlls));ck('imports remain KERNEL32+USER32',dlls==['kernel32.dll','user32.dll'],str(dlls))
except Exception as e:ck('objdump imports',False,str(e))
with tempfile.TemporaryDirectory() as td:
    q=Path(td)/'rebuilt.dll';cp=subprocess.run([sys.executable,str(PATCH),str(BASE),str(OBJ),str(q)],capture_output=True,text=True)
    ck('deterministic patcher exits 0',cp.returncode==0,(cp.stdout+cp.stderr)[-300:]);ck('deterministic rebuild byte-identical',q.exists() and q.read_bytes()==B,sha(q) if q.exists() else 'missing')
# Package verification policy
core=(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat').read_text('ascii',errors='replace');ps=(ROOT/'diag/PTAR_VERIFY_FULLSTACK1.ps1').read_text('utf-8',errors='replace');bat=(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat').read_text('ascii',errors='replace')
for tok in (OUT_SHA,'FG_DWM_PHASE_DIAG=DWMPHASE1_SAMPLE_EVERY_8_G_AND_R','FG_DWM_PHASE_HOTKEY=CTRL_F4_SNAPSHOT_HUD_DWM_LAB','DWM_TIMING_INFO_SIZE=292_PACK4'):
    ck('core marker '+tok,tok in core)
    ck('verify marker '+tok,(tok in ps) if tok!=OUT_SHA else (tok in ps))
ck('VERIFYFIX5 uses PSScriptRoot','$PSScriptRoot' in ps)
ck('VERIFYFIX5 has no GetFullPath','GetFullPath' not in ps)
ck('VERIFY launcher does not pass package path to PS','-File "%PS%"' in bat and '-PackageRoot' not in bat)
for p in sorted(ROOT.rglob('*.bat')):
    raw=p.read_bytes();rel=p.relative_to(ROOT).as_posix();ck('batch ASCII '+rel,all(x<128 for x in raw));ck('batch CRLF '+rel,b'\r\n' in raw and b'\n' not in raw.replace(b'\r\n',b''));ck('batch no BOM '+rel,not raw.startswith(b'\xef\xbb\xbf'))
print(f'DWMPHASE1 STATIC VALIDATION: {sum(checks)}/{len(checks)} '+('PASS' if all(checks) else 'FAIL'))
raise SystemExit(0 if all(checks) else 1)
