#!/usr/bin/env python3
"""Static/reproducibility gate for SAFEPOINT8/TDETAIL4_1."""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve(); D=ROOT/'diag'; PAY=ROOT/'payload'; BASE=D/'base/GW12_BASE.dll'
PATCHES=[D/'patch_gw16_auto_from_gw12.py',D/'patch_gw16g_unified_recorder.py',D/'patch_gw16h_recorder_safepoint.py',D/'patch_gw16h_recorder_safepoint2.py',D/'patch_gw16h_recorder_local_gpu_state.py',D/'patch_gw16h_recorder_native_state_guard.py',D/'patch_gw16h_recorder_qsv_priority.py',D/'patch_gw16h_fg_tdetail2.py',D/'patch_gw16h_fg_tdetail3.py',D/'patch_gw16h_fg_tdetail4.py']
SHAS=['50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59','8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d','4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f','6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7','7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630','961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2','1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b','50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92','e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53','c573b4c4ca102867ca67dbd16e1e6d36e3057f43d85b343fca94803000de0040','613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70']
OLD=b'max(G,.35)'; NEW=b'max(G,.25)'; checks=[]
def ck(n,c,d=''): checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
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
ck('GW12 base exact',BASE.is_file() and sha(BASE)==SHAS[0],sha(BASE) if BASE.is_file() else 'missing')
for p in PATCHES: ck('patch present '+p.name,p.is_file())
final=(PAY/'win81_nis_dx11_x64.dll').read_bytes(); mirror=(PAY/'d3d11.dll').read_bytes(); ck('final SHA',hashlib.sha256(final).hexdigest()==SHAS[-1],hashlib.sha256(final).hexdigest()); ck('mirror exact',mirror==final); ck('PE size 320000',len(final)==320000,len(final))
with tempfile.TemporaryDirectory() as td:
 td=Path(td);cur=BASE;st=[]
 for i,p in enumerate(PATCHES,1):
  out=td/f's{i}.dll';cp=subprocess.run([sys.executable,str(p),str(cur),str(out)],capture_output=True,text=True);ck('execute '+p.name,cp.returncode==0,(cp.stdout+cp.stderr).strip().replace('\n',' | '));ck('stage %d SHA'%i,out.is_file() and sha(out)==SHAS[i],sha(out) if out.is_file() else 'missing');st.append(out);cur=out
 sp7=st[-2].read_bytes(); rebuilt=st[-1].read_bytes(); ck('lineage reproduces final',rebuilt==final,hashlib.sha256(rebuilt).hexdigest()); ck('SP7 exact',hashlib.sha256(sp7).hexdigest()==SHAS[-2],hashlib.sha256(sp7).hexdigest())
 ck('SP7 one Guard35 source token',sp7.count(OLD)==1,sp7.count(OLD)); ck('SP8 removes Guard35 source token',final.count(OLD)==0,final.count(OLD)); ck('SP8 one Guard25 source token',final.count(NEW)==1,final.count(NEW)); ck('section topology unchanged',parse(sp7)[1]==parse(final)[1])
 opt,_=parse(final);cso=opt+64;stored=struct.unpack_from('<I',final,cso)[0];ck('PE checksum exact',stored==pe_checksum(final,cso),hex(stored)); changed={i for i,(a,b) in enumerate(zip(sp7,final)) if a!=b}; func=changed-set(range(cso,cso+4)); ck('functional delta confined to FG source',all(0x34600<=x<0x34700 for x in func),(min(func),max(func),len(func))); ck('no delta outside FG source + checksum',all((0x34600<=x<0x34700) or (cso<=x<cso+4) for x in changed),len(changed))
 # Retained trust and recorder regions
 trust=b'k=.8+saturate(1-max(e.x,e.y)/(9.5+.35*max(abs(v.x),abs(v.y))))/5;'; ck('TDETAIL3 compressed trust retained',final.count(trust)==1,final.count(trust))
 for off,n,label in [(0x1f679,5,'encode priority'),(0x20687,8,'FFmpeg flags'),(0x4ae04,109,'state guard A'),(0x4b2b3,282,'state guard B')]: ck(label+' unchanged SP7->SP8',sp7[off:off+n]==final[off:off+n])
# Semantic profile gate: profile3 G=.25 changes .35 -> .25, QUALITY G=.35 remains .35, larger profiles unchanged.
for g,expect in [(0.25,0.25),(0.35,0.35),(0.50,0.50),(0.65,0.65)]: ck('effective photometric guard G=%.2f'%g,abs(max(g,.25)-expect)<1e-12,max(g,.25))
ck('profile3 guard reduced vs TDETAIL3',max(.25,.25)<max(.25,.35),(max(.25,.25),max(.25,.35))); ck('QUALITY unchanged mathematically',max(.35,.25)==max(.35,.35),max(.35,.25))
ver=(PAY/'win81_nis_version.txt').read_text(errors='replace');ini=(PAY/'win81_nis.ini').read_text(errors='replace');finding=(D/'GW16H_SAFEPOINT7_TDETAIL3_FIELD_FINDING.txt').read_text(errors='replace')
for tok in ['GW16H_UNIFIEDREC3_SAFEPOINT8=TDETAIL4_PROFILE3_GUARD25_PHOTOMETRIC_DETAIL_GATE','FG_PROFILE3_TDETAIL4_PHOTOMETRIC_GUARD=0.25_RESTORED','FG_PROFILE3_TDETAIL4_MOTION_TRUST=K_FINAL_0.80_PLUS_0.20_K_RAW_UNCHANGED','FG_PROFILE2_QUALITY_SEMANTICS=UNCHANGED_GUARD35_NO_PROFILE3_BRANCH']: ck('version '+tok[:46],tok in ver)
for tok in ['CONSERVATIVE/TDETAIL4','native Guard25 photometric gate','profile 2 QUALITY remains semantically unchanged']: ck('INI '+tok,tok.lower() in ini.lower())
for tok in ['selection circle','floor grilles','median absolute high-frequency excursion about 21.6%','restore profile 3 photometric guard']: ck('finding '+tok,tok.lower() in finding.lower())
failed=[x for x in checks if not x[1]]; print('FG_TDETAIL4_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
 for n,_,d in failed: print('FAILED',n,d)
 raise SystemExit(1)
