#!/usr/bin/env python3
"""Regression gate: SAFEPOINT8 retains the original B18K18 recorder and SAFEPOINT2-6 fixes; TDETAIL4 changes only profile-3 embedded shader photometric guard."""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
BASE=ROOT/'diag/base/GW12_BASE.dll'; OUT=ROOT/'payload/win81_nis_dx11_x64.dll'; D3D=ROOT/'payload/d3d11.dll'
P1=ROOT/'diag/patch_gw16_auto_from_gw12.py'; P2=ROOT/'diag/patch_gw16g_unified_recorder.py'; P3=ROOT/'diag/patch_gw16h_recorder_safepoint.py'; P4=ROOT/'diag/patch_gw16h_recorder_safepoint2.py'; P5=ROOT/'diag/patch_gw16h_recorder_local_gpu_state.py'; P6=ROOT/'diag/patch_gw16h_recorder_native_state_guard.py'; P7=ROOT/'diag/patch_gw16h_recorder_qsv_priority.py'; P8=ROOT/'diag/patch_gw16h_fg_tdetail2.py'; P9=ROOT/'diag/patch_gw16h_fg_tdetail3.py'; P10=ROOT/'diag/patch_gw16h_fg_tdetail4.py'
GW16G_SHA='8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d'
UNIFIED2_SHA='4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f'
SAFE1_SHA='6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7'
SP5_SHA='50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92'
SP6_SHA='e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53'
SP7_SHA='c573b4c4ca102867ca67dbd16e1e6d36e3057f43d85b343fca94803000de0040'
FINAL_SHA='613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70'
START_JE=0x19EE3; SUBMIT_JE=0x277AB; VIDEO_SITE=0x12304; SUBMIT_SITE=0x25B49
RECORDER_HANDLER=0x19DC0; RECORDER_SUBMIT=0x27760; REQUEST=0x34FE910; WRAPPER=0x34FE930; RESOURCE_HELPER=0x34FEA60; IDENTITY_HELPER=0x34FEAD0
START_NOTICE=0x19FC5; STOP_NOTICE=0x19E78; START_FUNC=0x19F59
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest() if Path(p).is_file() else None
def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0];coff=e+4;n=struct.unpack_from('<H',b,coff+2)[0];optsz=struct.unpack_from('<H',b,coff+16)[0];opt=coff+20;sh=opt+optsz;secs=[]
    for i in range(n):
        o=sh+i*40;name=b[o:o+8].rstrip(b'\0').decode('ascii');vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8);secs.append((name,vs,va,rs,rp))
    return opt,secs
def roff(b,rva):
    for _n,vs,va,rs,rp in parse(b)[1]:
        if va<=rva<va+max(vs,rs):return rp+(rva-va)
    raise RuntimeError('unmapped %#x'%rva)
def target(b,rva,op):
    x=b[roff(b,rva):roff(b,rva)+5]
    return None if len(x)!=5 or x[0]!=op else rva+5+struct.unpack_from('<i',x,1)[0]
def call_target(b,rva):return target(b,rva,0xE8)
def jmp_target(b,rva):return target(b,rva,0xE9)
B=OUT.read_bytes(); ck('final runtime exact',sha(OUT)==FINAL_SHA,sha(OUT));ck('d3d11 mirror exact',sha(D3D)==FINAL_SHA,sha(D3D));ck('size 320000',len(B)==320000,len(B))
with tempfile.TemporaryDirectory() as td:
    td=Path(td);g=td/'g.dll';u=td/'u.dll';s1=td/'s1.dll';s2=td/'s2.dll';s3=td/'s3.dll';s4=td/'s4.dll';s5=td/'s5.dll';s6=td/'s6.dll';s7=td/'s7.dll';f=td/'f.dll'
    r1=subprocess.run([sys.executable,str(P1),str(BASE),str(g)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('GW16G patch exits 0',r1.returncode==0,r1.stdout.strip().replace('\n',' | '));ck('GW16G exact',sha(g)==GW16G_SHA,sha(g))
    r2=subprocess.run([sys.executable,str(P2),str(g),str(u)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('UNIFIEDREC2 patch exits 0',r2.returncode==0,r2.stdout.strip().replace('\n',' | '));ck('UNIFIEDREC2 intermediate exact',sha(u)==UNIFIED2_SHA,sha(u))
    r3=subprocess.run([sys.executable,str(P3),str(u),str(s1)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT1 patch exits 0',r3.returncode==0,r3.stdout.strip().replace('\n',' | '));ck('SAFEPOINT1 intermediate exact',sha(s1)==SAFE1_SHA,sha(s1))
    r4=subprocess.run([sys.executable,str(P4),str(s1),str(s2)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT2 patch exits 0',r4.returncode==0,r4.stdout.strip().replace('\n',' | '))
    r5=subprocess.run([sys.executable,str(P5),str(s2),str(s3)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT3 patch exits 0',r5.returncode==0,r5.stdout.strip().replace('\n',' | '))
    r6=subprocess.run([sys.executable,str(P6),str(s3),str(s4)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT4 patch exits 0',r6.returncode==0,r6.stdout.strip().replace('\n',' | ')); r7=subprocess.run([sys.executable,str(P7),str(s4),str(s5)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT5 patch exits 0',r7.returncode==0,r7.stdout.strip().replace('\n',' | '));ck('SAFEPOINT5 intermediate exact',sha(s5)==SP5_SHA,sha(s5)); r8=subprocess.run([sys.executable,str(P8),str(s5),str(s6)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT6 patch exits 0',r8.returncode==0,r8.stdout.strip().replace('\n',' | '));ck('SAFEPOINT6 intermediate exact',sha(s6)==SP6_SHA,sha(s6)); r9=subprocess.run([sys.executable,str(P9),str(s6),str(s7)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT7 patch exits 0',r9.returncode==0,r9.stdout.strip().replace('\n',' | '));ck('SAFEPOINT7 intermediate exact',sha(s7)==SP7_SHA,sha(s7)); r10=subprocess.run([sys.executable,str(P10),str(s7),str(f)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);ck('SAFEPOINT8 patch exits 0',r10.returncode==0,r10.stdout.strip().replace('\n',' | '));ck('full lineage reproduces final',f.is_file() and f.read_bytes()==B,sha(f));U=u.read_bytes();G=g.read_bytes()
# UNIFIEDREC2 intent: only explicit FG-only gates removed in recorder implementation.
ck('GW16G start FG gate existed',G[roff(G,START_JE):roff(G,START_JE)+2]==bytes.fromhex('7455'))
ck('UNIFIEDREC2 start FG gate NOP',U[roff(U,START_JE):roff(U,START_JE)+2]==b'\x90\x90')
ck('final start FG gate still NOP',B[roff(B,START_JE):roff(B,START_JE)+2]==b'\x90\x90')
ck('GW16G submit FG gate existed',G[roff(G,SUBMIT_JE):roff(G,SUBMIT_JE)+6]==bytes.fromhex('0f8459040000'))
ck('UNIFIEDREC2 submit FG gate NOP',U[roff(U,SUBMIT_JE):roff(U,SUBMIT_JE)+6]==b'\x90'*6)
ck('final submit FG gate still NOP',B[roff(B,SUBMIT_JE):roff(B,SUBMIT_JE)+6]==b'\x90'*6)
# SAFEPOINT2 keeps the single original B18K18 implementation. Only two small
# admission blocks are deliberately extended to accept the native D3D tuple while FG OFF.
ck('UNIFIEDREC2 action8 direct handler',call_target(U,VIDEO_SITE)==RECORDER_HANDLER,hex(call_target(U,VIDEO_SITE) or 0))
ck('final action8 deferred request',jmp_target(B,VIDEO_SITE)==REQUEST,hex(jmp_target(B,VIDEO_SITE) or 0))
ck('UNIFIEDREC2 common submit direct',call_target(U,SUBMIT_SITE)==RECORDER_SUBMIT,hex(call_target(U,SUBMIT_SITE) or 0))
ck('final isolated submit wrapper',call_target(B,SUBMIT_SITE)==WRAPPER,hex(call_target(B,SUBMIT_SITE) or 0))
def masked_equal(a,z,masks):
    x=bytearray(U[roff(U,a):roff(U,a)+(z-a)]); y=bytearray(B[roff(B,a):roff(B,a)+(z-a)])
    for m0,m1 in masks:
        lo=max(a,m0); hi=min(z,m1)
        if lo<hi:
            x[lo-a:hi-a]=b'\0'*(hi-lo); y[lo-a:hi-a]=b'\0'*(hi-lo)
    return x==y
ck('hotkey recorder handler retained except mode-aware resource admission',masked_equal(0x19DC0,0x1A000,[(0x19EEF,0x19F03)]))
ck('frame submit retained except mode-aware device identity admission',masked_equal(0x27760,0x27C20,[(0x277BF,0x277CC),(0x27AD5,0x27ADA)]))
ck('start auto-disable guard byte-identical',B[roff(B,0x19EE5):roff(B,0x19EEF)]==U[roff(U,0x19EE5):roff(U,0x19EEF)])
ck('start resource admission extended through helper',call_target(B,0x19EEF)==RESOURCE_HELPER,hex(call_target(B,0x19EEF) or 0))
ck('submit auto-disable guard byte-identical',B[roff(B,0x277B1):roff(B,0x277BF)]==U[roff(U,0x277B1):roff(U,0x277BF)])
ck('submit device identity extended through helper',call_target(B,0x277BF)==IDENTITY_HELPER,hex(call_target(B,0x277BF) or 0))
# Production start/stop/HUD QSV behavior remains original.
ck('original recorder start function call retained',call_target(B,START_FUNC)==0x1EE20,hex(call_target(B,START_FUNC) or 0))
ck('original HUD notice 14 retained',B[roff(B,START_NOTICE):roff(B,START_NOTICE)+5]==bytes.fromhex('b90e000000'))
ck('original HUD notice 16 retained',B[roff(B,STOP_NOTICE):roff(B,STOP_NOTICE)+5]==bytes.fromhex('b910000000'))
ck('no external recorder bridge',not (ROOT/'tools/recorder/PTAR_RECORDER_BRIDGE.ps1').exists() and not (ROOT/'07-START_RECORDER_BRIDGE.bat').exists() and not (ROOT/'08-STOP_RECORDER_BRIDGE.bat').exists())
ini=(ROOT/'payload/win81_nis.ini').read_text(encoding='utf-8',errors='replace');ver=(ROOT/'payload/win81_nis_version.txt').read_text(encoding='utf-8',errors='replace')
for tok in ['FG ON','FG OFF','No external bridge','VideoRecord=CTRL+F9','SAFEPOINT2']:
    ck('INI documents '+tok,tok.lower() in ini.lower())
for tok in ['GW16H_UNIFIEDREC2=SAME_ORIGINAL_B18K18_RECORDER_WITH_ACTION8_DIRECT_DISPATCH_FG_ON_OR_OFF','GW16H_UNIFIEDREC3_SAFEPOINT2=MULTIPATH_NATIVE_USR_FGOFF_BACKBUFFER_FEED_ORIGINAL_B18K18','RECORDER_RESOURCE_GUARDS=MODE_AWARE_RETAINED_NOT_BYPASSED','RECORDER_EXTERNAL_BRIDGE=ABSENT','RECORDER_START_HUD=ORIGINAL_NOTICE_14','RECORDER_STOP_HUD=ORIGINAL_NOTICE_16']:
    ck('version '+tok[:48],tok in ver)
# SAFEPOINT4/5/6/7 regression ownership: the final version must explicitly retain the
# SAFEPOINT3 recorder-local GPU objects and declare the native-USR-only state guard.
ck('version SAFEPOINT3 recorder-local GPU state declared',
   'GW16H_UNIFIEDREC3_SAFEPOINT3=B18K18_RECORDER_LOCAL_VS_RS_ACTIVE_DEVICE' in ver)
ck('version SAFEPOINT4 native-USR state guard declared',
   'GW16H_UNIFIEDREC3_SAFEPOINT4=NATIVE_USR_RECORDER_D3D11_IMMEDIATE_CONTEXT_STATE_GUARD' in ver and
   'RECORDER_NATIVE_STATE_GUARD_SCOPE=FG_OFF_NATIVE_USR_ONLY' in ver)
ck('version SAFEPOINT5 consumer scheduling declared',
   'GW16H_UNIFIEDREC3_SAFEPOINT5=FG_PROFILE3_QSV_CONSUMER_PRIORITY_NORMAL' in ver and
   'RECORDER_QSV_ENCODE_THREAD_PRIORITY=NORMAL' in ver and
   'RECORDER_GPU_READBACK_THREAD_PRIORITY=BELOW_NORMAL_UNCHANGED' in ver)
ck('version SAFEPOINT6 TDETAIL2 declared',
   'GW16H_UNIFIEDREC3_SAFEPOINT6=TDETAIL2_PROFILE3_TEMPORAL_DETAIL_STABILITY' in ver and
   'FG_PROFILE2_QUALITY_SEMANTICS=UNCHANGED_GUARD35_NO_Q3_BRANCH' in ver)
ck('version SAFEPOINT7 TDETAIL3 retained', 'GW16H_UNIFIEDREC3_SAFEPOINT7=TDETAIL3_PROFILE3_COMPRESSED_MOTION_TRUST' in ver and 'FG_PROFILE3_TDETAIL3_MIN_WARP_TRUST=0.80' in ver)
ck('version SAFEPOINT8 TDETAIL4 declared', 'GW16H_UNIFIEDREC3_SAFEPOINT8=TDETAIL4_PROFILE3_GUARD25_PHOTOMETRIC_DETAIL_GATE' in ver and 'FG_PROFILE3_TDETAIL4_PHOTOMETRIC_GUARD=0.25_RESTORED' in ver)
failed=[x for x in checks if not x[1]]
print('UNIFIED_RECORDER_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed:print('FAILED',n,d)
    sys.exit(1)
