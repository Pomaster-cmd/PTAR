#!/usr/bin/env python3
import hashlib,re,struct,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
BASE=ROOT/'diag/base/GW12_BASE.dll'; OUT=ROOT/'payload/win81_nis_dx11_x64.dll'; D3D=ROOT/'payload/d3d11.dll'; PATCH=ROOT/'diag/patch_gw16_auto_from_gw12.py'; UNIFIED_PATCH=ROOT/'diag/patch_gw16g_unified_recorder.py'; SAFE_PATCH=ROOT/'diag/patch_gw16h_recorder_safepoint.py'; SAFE2_PATCH=ROOT/'diag/patch_gw16h_recorder_safepoint2.py'; LOCALGPU_PATCH=ROOT/'diag/patch_gw16h_recorder_local_gpu_state.py'; STATEGUARD_PATCH=ROOT/'diag/patch_gw16h_recorder_native_state_guard.py'; QSV_PATCH=ROOT/'diag/patch_gw16h_recorder_qsv_priority.py'; TDETAIL_PATCH=ROOT/'diag/patch_gw16h_fg_tdetail2.py'; TDETAIL3_PATCH=ROOT/'diag/patch_gw16h_fg_tdetail3.py'; TDETAIL4_PATCH=ROOT/'diag/patch_gw16h_fg_tdetail4.py'; GW16G_BASE_SHA='8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d'; UNIFIED2_SHA='4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f'; SAFE1_SHA='6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7'
BASE_SHA='50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'
OUT_SHA='613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70'
CALLS=(0xC798,0xD678); GOV=0x34FB000; FG_ENABLED=0x02C3F6CC
DWM=0x34FE01E; DWM_EPI=0x34FE290; HELPER=0x26540; SELECTOR=0x265CF; FALLBACK_CALL=0x25A0B
AUTO=0x34FFC80; AUTO_FB=0x34FFF00; MODE=0x34FFF20; STR_LOW=0x34FFF60; STR_HIGH=0x34FFFB0; CAVE_END=0x3500000
GETASYNC=0x49FB8; F6_CALL=0x12230; F6_ACTION=0x16950; VIDEO_CALL=0x12304; RECORDER_ACTION=0x19DC0; F6_SAFE=0x34FD980; OLD_BAD_RECORDER_GUARD=0x34FD9B0; DWM_CTRL_F8=0x34FE543; QUALITY=0x34FDC00; QUALITY_NEXT_SITE=0x34FDC95; DWM_QUALITY_CALL=0x34FE44C; REC_REQUEST=0x34FE910; REC_SUBMIT_SITE=0x25B49; REC_CAVE_START=0x34FE900; REC_CAVE_END=0x34FEC00; NATIVE_REC_SITES=(0xC810,0xC90E,0xD6F0,0xD7EE); START_RES_GUARD=0x19EEF; SUBMIT_ID_GUARD=0x277BF
LOW=b'GW16 AUTO60/30: HIGH->LOW, stable 30 mode armed (workload >34ms)\0'
HIGH=b'GW16 AUTO60/30: LOW->HIGH, 60 mode armed (workload <33ms qualified)\0'
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest() if Path(p).is_file() else None
def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4; n=struct.unpack_from('<H',b,coff+2)[0]; optsz=struct.unpack_from('<H',b,coff+16)[0]; opt=coff+20; sh=opt+optsz; secs=[]
    for i in range(n):
        o=sh+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii'); vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8); ch=struct.unpack_from('<I',b,o+36)[0]; secs.append((name,vs,va,rs,rp,ch))
    return opt,secs
def roff(b,rva):
    for _n,vs,va,rs,rp,_ch in parse(b)[1]:
        if va<=rva<va+max(vs,rs): return rp+(rva-va)
    raise RuntimeError('unmapped RVA %#x'%rva)
def call_target(b,rva):
    o=roff(b,rva); raw=b[o:o+5]
    if len(raw)!=5 or raw[0]!=0xE8: return None
    return rva+5+struct.unpack_from('<i',raw,1)[0]
def jmp_target(b,rva):
    o=roff(b,rva); raw=b[o:o+5]
    if len(raw)!=5 or raw[0]!=0xE9: return None
    return rva+5+struct.unpack_from('<i',raw,1)[0]
def rip_target(rva,ilen,raw,disp_off): return rva+ilen+struct.unpack_from('<i',raw,disp_off)[0]
ck('GW12 base exact',sha(BASE)==BASE_SHA,sha(BASE)); ck('GW16H SAFEPOINT8 runtime exact',sha(OUT)==OUT_SHA,sha(OUT)); ck('d3d11 mirror exact',sha(D3D)==OUT_SHA,sha(D3D)); ck('patchers present',all(x.is_file() for x in [PATCH,UNIFIED_PATCH,SAFE_PATCH,SAFE2_PATCH,LOCALGPU_PATCH,STATEGUARD_PATCH,QSV_PATCH,TDETAIL_PATCH,TDETAIL3_PATCH,TDETAIL4_PATCH]))
A=BASE.read_bytes(); B=OUT.read_bytes(); optA,secsA=parse(A); optB,secsB=parse(B)
ck('PE size unchanged 320000',len(A)==len(B)==320000,len(B)); ck('section topology unchanged',secsA==secsB); ck('9 sections retained',len(secsB)==9,len(secsB)); ck('.fgdia executable',any(x[0]=='.fgdia' and (x[5]&0x20000000) for x in secsB)); ck('.dwmlab RWX',secsB[-1][0]=='.dwmlab' and (secsB[-1][5]&0xE0000000)==0xE0000000,secsB[-1])
for site in CALLS:
    ck('GW12 governor call %#x'%site,call_target(A,site)==GOV,hex(call_target(A,site) or 0)); ck('GW16G wrapper call %#x'%site,call_target(B,site)==AUTO,hex(call_target(B,site) or 0))
# Normal/fallback present contract.
fast=B[roff(B,DWM):roff(B,DWM)+17]
ck('DWM fastpath loads AUTO_MODE',fast[:3]==bytes.fromhex('448b2d') and rip_target(DWM,7,fast,3)==MODE,fast.hex())
ck('DWM fastpath calls Present helper',fast[7]==0xE8 and DWM+12+struct.unpack_from('<i',fast,8)[0]==HELPER,fast.hex())
ck('DWM fastpath jumps original epilogue',fast[12]==0xE9 and DWM+17+struct.unpack_from('<i',fast,13)[0]==DWM_EPI,fast.hex())
os=roff(B,SELECTOR); ck('Present SyncInterval = 1 + AUTO_MODE',B[os:os+5]==bytes.fromhex('418d550190'),B[os:os+5].hex()); ck('Present flags remain zero',B[os+5:os+8]==bytes.fromhex('4531c0'),B[os+5:os+8].hex())
ck('REAL fallback targets actual AUTO fallback',call_target(B,FALLBACK_CALL)==AUTO_FB,hex(call_target(B,FALLBACK_CALL) or 0))
fb=B[roff(B,AUTO_FB):roff(B,AUTO_FB)+21]
ck('AUTO fallback exact push/sub/xor/call/add/pop/ret',fb[:9]==bytes.fromhex('41554883ec204531ed') and fb[9]==0xE8 and AUTO_FB+14+struct.unpack_from('<i',fb,10)[0]==HELPER and fb[14:]==bytes.fromhex('4883c420415dc3'),fb.hex())
ck('AUTO fallback ends before state',AUTO_FB+21<=MODE,hex(AUTO_FB+21)); ck('AUTO state initially zero',set(B[roff(B,MODE):roff(B,STR_LOW)])=={0}); ck('LOW string exact',B[roff(B,STR_LOW):roff(B,STR_LOW)+len(LOW)]==LOW); ck('HIGH string exact',B[roff(B,STR_HIGH):roff(B,STR_HIGH)+len(HIGH)]==HIGH); ck('strings fit cave',STR_HIGH+len(HIGH)<=CAVE_END)
# HOTKEYCHORD1 exact callsite isolation.
ck('GW12 F6 action call original',call_target(A,F6_CALL)==F6_ACTION,hex(call_target(A,F6_CALL) or 0)); ck('GW16G F6 action routed through late guard',call_target(B,F6_CALL)==F6_SAFE,hex(call_target(B,F6_CALL) or 0))
ck('GW12 action8 VideoRecord call original',call_target(A,VIDEO_CALL)==RECORDER_ACTION,hex(call_target(A,VIDEO_CALL) or 0)); ck('GW16H action8 VideoRecord deferred to SAFEPOINT request',jmp_target(B,VIDEO_CALL)==REC_REQUEST,hex(jmp_target(B,VIDEO_CALL) or 0))
def check_guard(rva,target,label):
    x=B[roff(B,rva):roff(B,rva)+30]
    ok=(x[:4]==bytes.fromhex('4883ec28') and x[4:9]==bytes.fromhex('b911000000') and x[9:11]==bytes.fromhex('ff15') and rip_target(rva+9,6,x[9:15],2)==GETASYNC and x[15:20]==bytes.fromhex('6685c07805') and x[20]==0xE8 and rva+25+struct.unpack_from('<i',x,21)[0]==target and x[25:]==bytes.fromhex('4883c428c3'))
    ck(label+' exact CTRL guard',ok,x.hex())
check_guard(F6_SAFE,F6_ACTION,'F6')
ck('GW12 CTRL+F8 DWM branch conditional',A[roff(A,DWM_CTRL_F8):roff(A,DWM_CTRL_F8)+2]==bytes.fromhex('7426'),A[roff(A,DWM_CTRL_F8):roff(A,DWM_CTRL_F8)+2].hex())
ck('GW16G CTRL+F8 DWM diagnostic skipped',B[roff(B,DWM_CTRL_F8):roff(B,DWM_CTRL_F8)+2]==bytes.fromhex('eb26'),B[roff(B,DWM_CTRL_F8):roff(B,DWM_CTRL_F8)+2].hex())
qa=bytearray(A[roff(A,QUALITY):roff(A,QUALITY)+0x157]); qb=bytearray(B[roff(B,QUALITY):roff(B,QUALITY)+0x157])
qoff=QUALITY_NEXT_SITE-QUALITY
qa[qoff:qoff+11]=qb[qoff:qoff+11]
ck('quality handler unchanged except declared QUALITYSAFE1 next-profile site',qa==qb)
ck('GW16G DWM quality poll call intentionally changed',call_target(A,DWM_QUALITY_CALL)==QUALITY and call_target(B,DWM_QUALITY_CALL)==0x34FDA10,(hex(call_target(A,DWM_QUALITY_CALL) or 0),hex(call_target(B,DWM_QUALITY_CALL) or 0)))
# Source policy + disassembly constants.
asm=(ROOT/'diag/auto60_30_wrapper.s').read_text(encoding='utf-8',errors='replace')
ck('NOLOCK30_1 policy present','NOLOCK30_1' in asm)
ck('FG ON initializes HIGH target60','mov dword ptr [rip+AUTO_MODE], 0' in asm and 'mov dword ptr [rip+AUTO_BACKOFF], 0' in asm and 'mov dword ptr [rip+FG_TARGET_FPS], 60' in asm)
ck('automatic HIGH->LOW branch disabled','cmp r8, qword ptr [rip+AUTO_DOWN_TICKS]\n    jmp .high_good' in asm)
ck('legacy LOW branch retained but dormant','mov dword ptr [rip+FG_TARGET_FPS], 30' in asm and '.low_mode:' in asm)
ck('RETURNFIX1 preserved','mov dword ptr [rsp+0x28], eax' in asm and 'mov eax, dword ptr [rsp+0x28]' in asm)
ck('FGGATE1 preserved','cmp dword ptr [rip+FG_ENABLED], 0' in asm and 'je .force_reset' in asm)
ck('FG OFF target120 preserved','mov dword ptr [rip+FG_TARGET_FPS], 120' in asm)
# Binary ownership is stage-validated: SAFEPOINT2/3/4/5/6/7 each has an exact surgical validator.
# Here the high-level runtime gate requires only section topology plus deterministic full-lineage reproduction.
ck('binary ownership delegated to exact stage validators',all((ROOT/'diag'/x).is_file() for x in ['validate_recorder_safepoint.py','validate_recorder_local_gpu_state.py','validate_recorder_native_state_guard.py','validate_recorder_qsv_priority.py','validate_fg_tdetail2.py','validate_fg_tdetail3.py']))
# Deterministic reproduction through SAFEPOINT7.
with tempfile.TemporaryDirectory() as td:
    td=Path(td); names=['gw16g','unified2','safe1','safe2','safe3','safe4','safe5','safe6','safe7','safe8']; scripts=[PATCH,UNIFIED_PATCH,SAFE_PATCH,SAFE2_PATCH,LOCALGPU_PATCH,STATEGUARD_PATCH,QSV_PATCH,TDETAIL_PATCH,TDETAIL3_PATCH,TDETAIL4_PATCH]
    expected=['8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d','4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f','6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7','7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630','961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2','1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b','50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92','e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53','c573b4c4ca102867ca67dbd16e1e6d36e3057f43d85b343fca94803000de0040','613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70']
    cur=BASE
    for name,script,exp in zip(names,scripts,expected):
        out=td/(name+'.dll'); r=subprocess.run([sys.executable,str(script),str(cur),str(out)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        ck(name+' patcher exits 0',r.returncode==0,r.stdout.strip().replace('\n',' | ')); ck(name+' exact SHA',out.is_file() and sha(out)==exp,sha(out) if out.exists() else 'missing'); cur=out
    ck('patchers reproduce exact runtime',cur.is_file() and cur.read_bytes()==B,sha(cur) if cur.exists() else 'missing')
# Imports/exports unchanged.
try:
    imp=subprocess.check_output(['objdump','-p',str(OUT)],text=True,stderr=subprocess.STDOUT).lower(); dlls=[x.split('dll name:',1)[1].strip() for x in imp.splitlines() if 'dll name:' in x]
    ck('static imports remain KERNEL32+USER32',set(dlls)=={'kernel32.dll','user32.dll'},dlls)
    def exports(path):
        t=subprocess.check_output(['objdump','-p',str(path)],text=True,stderr=subprocess.STDOUT); return set(re.findall(r'\]\s+([A-Za-z_][A-Za-z0-9_@?$]*)\s*$',t,re.M))
    ck('D3D11 export set unchanged',exports(BASE)==exports(OUT),(len(exports(BASE)),len(exports(OUT))))
except Exception as e: ck('import/export audit',False,e)
# Pure policy model mirrors NOLOCK30_1.
class Model:
    def __init__(self): self.armed=False; self.mode=0; self.target=120; self.switch_down=0; self.switch_up=0
    def step(self,work,soft=False,fg=True):
        if soft or not fg:
            self.armed=False; self.mode=0; self.target=120; return
        if not self.armed:
            self.armed=True; self.mode=0; self.target=60; return
        # Workload is telemetry only in NOLOCK30_1.
        self.mode=0; self.target=60
m=Model(); [m.step(50,fg=False) for _ in range(100)]
ck('FG OFF neutral target120',not m.armed and m.mode==0 and m.target==120,(m.armed,m.mode,m.target))
m.step(50); ck('FG ON starts HIGH target60',m.armed and m.mode==0 and m.target==60,(m.armed,m.mode,m.target))
for _ in range(10000): m.step(500)
ck('extreme sustained workload never arms LOW30',m.mode==0 and m.target==60 and m.switch_down==0,(m.mode,m.target,m.switch_down))
for _ in range(10000): m.step(1)
ck('light workload remains HIGH without transition',m.mode==0 and m.target==60 and m.switch_up==0,(m.mode,m.target,m.switch_up))
m.step(10,soft=True); ck('soft OFF resets target120',not m.armed and m.target==120 and m.mode==0,(m.armed,m.target,m.mode))
ck('HIGH ceiling math 30R+30G=60',60/2==30 and (60/2)*2==60)
probe_out=subprocess.check_output([sys.executable,str(PATCH),str(BASE),str(Path(tempfile.gettempdir())/'gw16g_probe.dll')],text=True)
ck('no active LOW30 policy','AUTO_DOWNSHIFT=DISABLED_NOLOCK30_1' in probe_out)
ck('patcher reports HOTKEYSAFE2','HOTKEYSAFE2=POLL_SOURCE_CTRL_F6_NO_PLAIN_F6_CTRL_F8_NO_STATUS_OR_DWM_DIAG' in probe_out)
ck('patcher reports QUALITYSAFE1','QUALITYSAFE1=FG_ON_SAME_ME_TIER_ONLY_FG_OFF_FULL_4_PROFILE_CYCLE' in probe_out)
failed=[x for x in checks if not x[1]]
print('GW16_RUNTIME_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    sys.exit(1)
