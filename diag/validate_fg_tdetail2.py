#!/usr/bin/env python3
"""Static/reproducibility gate for SAFEPOINT6/TDETAIL2_1.
Confirms the exact 6-byte functional delta from SAFEPOINT5, the full lineage,
and the semantic isolation of the change to effective profile 3 behavior.
"""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile, math
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
D=ROOT/'diag'; PAY=ROOT/'payload'
BASE=D/'base/GW12_BASE.dll'
PATCHES=[
 D/'patch_gw16_auto_from_gw12.py',D/'patch_gw16g_unified_recorder.py',D/'patch_gw16h_recorder_safepoint.py',
 D/'patch_gw16h_recorder_safepoint2.py',D/'patch_gw16h_recorder_local_gpu_state.py',D/'patch_gw16h_recorder_native_state_guard.py',
 D/'patch_gw16h_recorder_qsv_priority.py',D/'patch_gw16h_fg_tdetail2.py']
SHA_CHAIN=[
 '50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59',
 '8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d',
 '4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f',
 '6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7',
 '7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630',
 '961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2',
 '1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b',
 '50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92',
 'e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53']
FINAL=SHA_CHAIN[-1]; SP5=SHA_CHAIN[-2]; SIZE=320000
OLD_A=b'1.5+.35*max(abs(v.x),abs(v.y))'; NEW_A=b'9.5+.35*max(abs(v.x),abs(v.y))'
OLD_B=b'k*=k;'; NEW_B=b'k=k;;'; OLD_C=b'max(G,.02)'; NEW_C=b'max(G,.35)'
EXPECTED_FUNCTIONAL={0x3456e,0x34590,0x34591,0x34592,0x34663,0x34664}
# SAFEPOINT5 consumer scheduling regions must remain untouched.
QSV_REGIONS=[(0x1F679,5),(0x20687,8)]
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest() if Path(p).is_file() else None
def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4; n=struct.unpack_from('<H',b,coff+2)[0]; optsz=struct.unpack_from('<H',b,coff+16)[0]; opt=coff+20; sh=opt+optsz; secs=[]
    for i in range(n):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','ignore'); vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8); secs.append((name,vs,va,rs,rp))
    return opt,secs
def roff(b,rva):
    for _n,vs,va,rs,rp in parse(b)[1]:
        if va<=rva<va+max(vs,rs): return rp+(rva-va)
    raise RuntimeError('unmapped %#x'%rva)
def pe_checksum(blob,off):
    b=bytearray(blob); struct.pack_into('<I',b,off,0); s=0
    for i in range(0,len(b)-1,2): s+=b[i]|(b[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(b)&1:s+=b[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(b))&0xffffffff
def sat(x): return max(0.0,min(1.0,x))
def weight(G,d,e,v,new):
    if G<.3:
        den=(9.5 if new else 1.5)+.35*abs(v)
        k=sat(1-e/den)
        if not new:k*=k
    else:k=1.0
    floor=.35 if new else .02
    return sat(1-d/max(G,floor))*k

ck('GW12 base exists',BASE.is_file()); ck('GW12 exact SHA',sha(BASE)==SHA_CHAIN[0],sha(BASE))
for p in PATCHES: ck('patch present '+p.name,p.is_file())
final=(PAY/'win81_nis_dx11_x64.dll').read_bytes(); mirror=(PAY/'d3d11.dll').read_bytes()
ck('final runtime SHA',hashlib.sha256(final).hexdigest()==FINAL,hashlib.sha256(final).hexdigest())
ck('mirror exact',mirror==final,hashlib.sha256(mirror).hexdigest()); ck('PE size unchanged',len(final)==SIZE,len(final))
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cur=BASE; stages=[]
    for i,p in enumerate(PATCHES,1):
        out=td/f'stage{i}.dll'; cp=subprocess.run([sys.executable,str(p),str(cur),str(out)],capture_output=True,text=True)
        ck('execute '+p.name,cp.returncode==0,(cp.stdout+cp.stderr).strip().replace('\n',' | '))
        exp=SHA_CHAIN[i]; ck('stage %d SHA'%i,out.is_file() and sha(out)==exp,sha(out) if out.is_file() else 'missing')
        stages.append(out); cur=out
    sp5=stages[-2].read_bytes(); rebuilt=stages[-1].read_bytes()
    ck('full lineage reproduces final',rebuilt==final,hashlib.sha256(rebuilt).hexdigest())
    ck('SP5 stage exact',hashlib.sha256(sp5).hexdigest()==SP5,hashlib.sha256(sp5).hexdigest())
    ck('section topology unchanged',parse(sp5)[1]==parse(final)[1])
    csoff=parse(final)[0]+64; stored=struct.unpack_from('<I',final,csoff)[0]
    ck('PE checksum exact',stored==pe_checksum(final,csoff),hex(stored))
    changed={i for i,(a,b) in enumerate(zip(sp5,final)) if a!=b}
    cbytes=set(range(csoff,csoff+4)); functional=changed-cbytes
    ck('8 total changed bytes',len(changed)==8,len(changed)); ck('6 functional changed bytes',functional==EXPECTED_FUNCTIONAL,sorted(hex(x) for x in functional))
    ck('only checksum plus expected functional bytes',changed <= EXPECTED_FUNCTIONAL|cbytes,sorted(hex(x) for x in changed-(EXPECTED_FUNCTIONAL|cbytes)))
    for old,new,label in [(OLD_A,NEW_A,'motion denominator'),(OLD_B,NEW_B,'linear trust'),(OLD_C,NEW_C,'photometric floor')]:
        ck('SP5 has one old '+label,sp5.count(old)==1,sp5.count(old)); ck('SP6 removes old '+label,final.count(old)==0,final.count(old)); ck('SP6 has one new '+label,final.count(new)==1,final.count(new))
    # QSV consumer scheduling and state-guard/recorder code must not be touched by this HLSL-only patch.
    for r,n in QSV_REGIONS: ck('QSV region unchanged %#x'%r,sp5[roff(sp5,r):roff(sp5,r)+n]==final[roff(final,r):roff(final,r)+n])
    # All changed functional bytes are in the embedded ASCII shader source neighborhood.
    ck('all functional deltas in FG shader source area',all(0x34500<=x<0x34700 for x in functional),sorted(hex(x) for x in functional))

# Semantic isolation: for LEGACY/BALANCED/QUALITY values, numerical weight is identical over a sample grid.
for G,name in [(.65,'LEGACY'),(.50,'BALANCED'),(.35,'QUALITY')]:
    vals=[]
    for d in [0,.02,.08,.2,.4,.7]:
        for e in [0,.5,2,8]:
            for v in [0,2,10]: vals.append(abs(weight(G,d,e,v,False)-weight(G,d,e,v,True)))
    ck(name+' sampled semantics unchanged',max(vals)<1e-12,max(vals))
# Profile 3 should become less fallback-prone, never more, on sampled valid inputs.
oldnew=[]
for d in [0,.02,.08,.15,.25,.4]:
    for e in [0,.5,1,2,4,8,16]:
        for v in [0,1,4,12]: oldnew.append((weight(.25,d,e,v,False),weight(.25,d,e,v,True)))
ck('TDETAIL2 profile3 warped trust never lower on sample grid',all(n+1e-12>=o for o,n in oldnew))
ck('TDETAIL2 profile3 differs materially',any(n-o>.05 for o,n in oldnew),max(n-o for o,n in oldnew))
ver=(PAY/'win81_nis_version.txt').read_text(errors='replace'); ini=(PAY/'win81_nis.ini').read_text(errors='replace'); finding=(D/'GW16H_SAFEPOINT5_Q3_TDETAIL_FINDING.txt').read_text(errors='replace')
for tok in ['GW16H_UNIFIEDREC3_SAFEPOINT6=TDETAIL2_PROFILE3_TEMPORAL_DETAIL_STABILITY','FG_PROFILE2_QUALITY_SEMANTICS=UNCHANGED','FG_PROFILE3_TDETAIL2_PHOTOMETRIC_FLOOR=0.35','FG_PROFILE3_TDETAIL2_TRUST_CURVE=LINEAR_NO_K_SQUARE']:
    ck('version token '+tok[:48],tok in ver)
for tok in ['CONSERVATIVE/TDETAIL2','profile 2 QUALITY is semantically unchanged']:
    ck('INI TDETAIL2 documentation '+tok,tok.lower() in ini.lower())
for tok in ['53.9%','59.2%','57.9%','1.5 -> 9.5','k*=k -> linear k']:
    ck('field finding token '+tok,tok in finding)
failed=[x for x in checks if not x[1]]
print('FG_TDETAIL2_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
