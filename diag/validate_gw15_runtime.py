#!/usr/bin/env python3
import hashlib,struct,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
BASE=ROOT/'diag/base/GW12_BASE.dll'; OUT=ROOT/'payload/win81_nis_dx11_x64.dll'; D3D=ROOT/'payload/d3d11.dll'; PATCH=ROOT/'diag/patch_gw15_runtime_from_gw12.py'
BASE_SHA='50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'; OUT_SHA='2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d'; PRESENT_RVA=0x265cf
checks=[]
def ck(n,c,d=''):checks.append((n,bool(c),str(d)));print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest() if Path(p).is_file() else None
def parse(b):
 e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;n=struct.unpack_from('<H',b,coff+2)[0];opts=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+opts;secs=[]
 for i in range(n):
  o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode();vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs.append((name,vs,va,rs,rp))
 return opt,secs
def roff(b,rva):
 for _n,vs,va,rs,rp in parse(b)[1]:
  if va<=rva<va+max(vs,rs):return rp+(rva-va)
 raise RuntimeError(rva)
ck('GW12 base exact',sha(BASE)==BASE_SHA,sha(BASE));ck('GW15 runtime exact',sha(OUT)==OUT_SHA,sha(OUT));ck('d3d11 mirror exact',sha(D3D)==OUT_SHA,sha(D3D));ck('patcher exists',PATCH.is_file())
A=BASE.read_bytes();B=OUT.read_bytes()
ck('size unchanged 320000',len(A)==len(B)==320000,len(B));ck('section count unchanged 9',len(parse(A)[1])==len(parse(B)[1])==9);ck('section topology identical',parse(A)[1]==parse(B)[1])
o=roff(A,PRESENT_RVA)
ck('GW12 Present SyncInterval immediate is 1',A[o:o+5]==bytes.fromhex('ba01000000'),A[o:o+5].hex())
ck('GW15 Present SyncInterval immediate is 2',B[o:o+5]==bytes.fromhex('ba02000000'),B[o:o+5].hex())
# Only Present immediate and PE checksum may differ.
opt,_=parse(A);csoff=opt+64;allowed=set(range(o,o+5))|set(range(csoff,csoff+4));bad=[];chg=[]
for i,(x,y) in enumerate(zip(A,B)):
 if x!=y:
  chg.append(i)
  if i not in allowed:bad.append(i)
ck('binary delta confined to Present immediate + PE checksum',not bad,'changed=%d bad=%s'%(len(chg),bad[:8]))
# No topology, soft-OFF or cave changes.
for r0,r1,name in [(0x23F00,0x265CF,'pre-Present hot region'),(0x265D4,0x27000,'post-immediate hot region'),(0x34FF8F0,0x34FFC80,'GW12 soft-OFF cave')]:
 ck(name+' byte-identical GW12',A[roff(A,r0):roff(A,r1)]==B[roff(B,r0):roff(B,r1)])
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'x.dll';r=subprocess.run([sys.executable,str(PATCH),str(BASE),str(p)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 ck('patcher exits 0',r.returncode==0,r.stdout.strip().replace('\n',' | '));ck('patcher reproduces byte-for-byte',p.is_file() and p.read_bytes()==B,sha(p) if p.exists() else 'missing')
try:
 imp=subprocess.check_output(['objdump','-p',str(OUT)],text=True,stderr=subprocess.STDOUT).lower();dlls=[l.split('dll name:',1)[1].strip() for l in imp.splitlines() if 'dll name:' in l]
 ck('static imports remain KERNEL32+USER32',set(dlls)=={'kernel32.dll','user32.dll'},dlls)
except Exception as e:ck('objdump import check',False,e)
bad=[x for x in checks if not x[1]]
print('GW15_RUNTIME_VALIDATION=%d/%d %s'%(len(checks)-len(bad),len(checks),'PASS' if not bad else 'FAIL'))
raise SystemExit(1 if bad else 0)
