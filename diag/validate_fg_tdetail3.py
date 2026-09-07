#!/usr/bin/env python3
"""Static/reproducibility gate for SAFEPOINT7/TDETAIL3_1."""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile, math
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
D=ROOT/'diag'; PAY=ROOT/'payload'; BASE=D/'base/GW12_BASE.dll'
PATCHES=[
 D/'patch_gw16_auto_from_gw12.py',D/'patch_gw16g_unified_recorder.py',D/'patch_gw16h_recorder_safepoint.py',
 D/'patch_gw16h_recorder_safepoint2.py',D/'patch_gw16h_recorder_local_gpu_state.py',D/'patch_gw16h_recorder_native_state_guard.py',
 D/'patch_gw16h_recorder_qsv_priority.py',D/'patch_gw16h_fg_tdetail2.py',D/'patch_gw16h_fg_tdetail3.py']
SHA_CHAIN=[
 '50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59',
 '8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d',
 '4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f',
 '6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7',
 '7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630',
 '961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2',
 '1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b',
 '50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92',
 'e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53',
 'c573b4c4ca102867ca67dbd16e1e6d36e3057f43d85b343fca94803000de0040']
SP6=SHA_CHAIN[-2]; FINAL=SHA_CHAIN[-1]; SIZE=320000
OLD=b'k=saturate(1-max(e.x,e.y)/(9.5+.35*max(abs(v.x),abs(v.y))));k=k;;'
NEW=b'k=.8+saturate(1-max(e.x,e.y)/(9.5+.35*max(abs(v.x),abs(v.y))))/5;'
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest() if Path(p).is_file() else None
def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+optsz;secs=[]
    for i in range(n):
        o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii','ignore');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs.append((name,vs,va,rs,rp))
    return opt,secs
def pe_checksum(blob,off):
    x=bytearray(blob);struct.pack_into('<I',x,off,0);s=0
    for i in range(0,len(x)-1,2):s+=x[i]|(x[i+1]<<8);s=(s&0xffff)+(s>>16)
    if len(x)&1:s+=x[-1]
    s=(s&0xffff)+(s>>16);s=(s&0xffff)+(s>>16);return (s+len(x))&0xffffffff
def rawk(e,v): return max(0.0,min(1.0,1.0-e/(9.5+.35*abs(v))))
def td3(e,v): return .8+rawk(e,v)/5.0

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
        ck('stage %d SHA'%i,out.is_file() and sha(out)==SHA_CHAIN[i],sha(out) if out.is_file() else 'missing')
        stages.append(out);cur=out
    sp6=stages[-2].read_bytes(); rebuilt=stages[-1].read_bytes()
    ck('full lineage reproduces final',rebuilt==final,hashlib.sha256(rebuilt).hexdigest()); ck('SP6 stage exact',hashlib.sha256(sp6).hexdigest()==SP6,hashlib.sha256(sp6).hexdigest())
    ck('section topology unchanged',parse(sp6)[1]==parse(final)[1]); cso=parse(final)[0]+64; stored=struct.unpack_from('<I',final,cso)[0]; ck('PE checksum exact',stored==pe_checksum(final,cso),hex(stored))
    ck('SP6 has one TDETAIL2 trust expression',sp6.count(OLD)==1,sp6.count(OLD)); ck('SP7 removes old trust expression',final.count(OLD)==0,final.count(OLD)); ck('SP7 has one TDETAIL3 expression',final.count(NEW)==1,final.count(NEW))
    changed={i for i,(a,b) in enumerate(zip(sp6,final)) if a!=b}; func=changed-set(range(cso,cso+4))
    ck('functional delta confined to embedded FG source',all(0x34500<=x<0x34600 for x in func),(min(func),max(func),len(func)))
    ck('no delta outside shader source + PE checksum',all((0x34500<=x<0x34600) or (cso<=x<cso+4) for x in changed),len(changed))
    # Critical recorder/QSV/state machine regions remain byte-identical SP6->SP7.
    for off,n,label in [(0x1f679,5,'encode priority'),(0x20687,8,'FFmpeg flags'),(0x4ae04,109,'state guard A'),(0x4b2b3,282,'state guard B')]:
        ck(label+' unchanged',sp6[off:off+n]==final[off:off+n])

for e in [0,.5,1,2,4,8,16,64]:
    for v in [0,1,4,12,64]:
        k=rawk(e,v); t=td3(e,v)
        ck('trust range e=%s v=%s'%(e,v),.8-1e-12<=t<=1+1e-12,(k,t))
        ck('trust compression exact e=%s v=%s'%(e,v),abs(t-(.8+.2*k))<1e-12,(k,t))
# Endpoints and monotonicity.
ck('raw k=1 maps to 1',abs(td3(0,0)-1)<1e-12,td3(0,0)); ck('raw k=0 maps to .8',abs(td3(1e9,0)-.8)<1e-9,td3(1e9,0))
vals=[td3(e,4) for e in [0,.5,1,2,4,8,16,32,64]]; ck('trust monotone with discontinuity',all(vals[i+1]<=vals[i]+1e-12 for i in range(len(vals)-1)),vals)
ver=(PAY/'win81_nis_version.txt').read_text(errors='replace'); ini=(PAY/'win81_nis.ini').read_text(errors='replace'); finding=(D/'GW16H_SAFEPOINT6_TDETAIL2_FIELD_FINDING.txt').read_text(errors='replace')
for tok in ['GW16H_UNIFIEDREC3_SAFEPOINT7=TDETAIL3_PROFILE3_COMPRESSED_MOTION_TRUST','FG_PROFILE3_TDETAIL3_TRUST_COMPRESSION=K_FINAL_0.80_PLUS_0.20_K_RAW','FG_PROFILE3_TDETAIL3_MIN_WARP_TRUST=0.80','FG_PROFILE2_QUALITY_SEMANTICS=UNCHANGED_GUARD35_NO_Q3_BRANCH']:
    ck('version token '+tok[:48],tok in ver)
for tok in ['CONSERVATIVE/TDETAIL3','80% minimum warped trust','profile 2 QUALITY is semantically unchanged']:
    ck('INI TDETAIL3 token '+tok,tok.lower() in ini.lower())
for tok in ['12.4%','14.0%','13.1%','k_final = 0.80 + 0.20*k_raw']:
    ck('field finding '+tok,tok in finding)
failed=[x for x in checks if not x[1]]
print('FG_TDETAIL3_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
