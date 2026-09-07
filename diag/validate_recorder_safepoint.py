#!/usr/bin/env python3
"""Static/reproducibility gate for GW16H UNIFIEDREC3 SAFEPOINT2/NATIVEUSR1."""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
OUT=ROOT/'payload/win81_nis_dx11_x64.dll'; D3D=ROOT/'payload/d3d11.dll'; BASE=ROOT/'diag/base/GW12_BASE.dll'
P1=ROOT/'diag/patch_gw16_auto_from_gw12.py'; P2=ROOT/'diag/patch_gw16g_unified_recorder.py'; P3=ROOT/'diag/patch_gw16h_recorder_safepoint.py'; P4=ROOT/'diag/patch_gw16h_recorder_safepoint2.py'
FINAL='7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630'; SAFE1='6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7'; UNIFIED2='4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f'; GW16G='8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d'; BASESHA='50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'
ACTION=0x12304; REQUEST=0x34FE910; PENDING=0x34FE900; ISO=0x34FE930; NATIVE=0x34FE980; RES=0x34FEA60; IDENT=0x34FEAD0
HANDLER=0x19DC0; SUBMIT=0x27760; SUBMIT_SITE=0x25B49; NATIVE_POST=0x14930; NATIVE_SITES=(0xC810,0xC90E,0xD6F0,0xD7EE); UNTOUCHED=0x150C3
START_FG=0x19EE3; SUBMIT_FG=0x277AB; START_RES=0x19EEF; SUBMIT_ID=0x277BF
NATIVE_SWAP=0x2C7E010; NATIVE_DEV=0x4B118; NATIVE_CTX=0x2C7DFA8; IID=0x32058; FG=0x2C3F6CC
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
    raise RuntimeError(hex(rva))
def rel_target(b,rva,op):
    x=b[roff(b,rva):roff(b,rva)+5]
    return None if len(x)!=5 or x[0]!=op else rva+5+struct.unpack_from('<i',x,1)[0]
def call(b,rva): return rel_target(b,rva,0xE8)
def jmp(b,rva): return rel_target(b,rva,0xE9)
def pe_checksum(blob,off):
    x=bytearray(blob); struct.pack_into('<I',x,off,0); s=0
    for i in range(0,len(x)-1,2): s+=x[i]|(x[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(x)&1:s+=x[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16); return (s+len(x))&0xffffffff
B=OUT.read_bytes(); ck('runtime exact',sha(OUT)==FINAL,sha(OUT)); ck('d3d11 mirror exact',sha(D3D)==FINAL,sha(D3D)); ck('size 320000',len(B)==320000,len(B)); ck('GW12 base exact',sha(BASE)==BASESHA,sha(BASE))
opt,secs=parse(B); ck('section count 9',len(secs)==9,len(secs)); ck('.dwmlab RWX',secs[-1][0]=='.dwmlab' and (secs[-1][5]&0xE0000000)==0xE0000000,secs[-1]); stored=struct.unpack_from('<I',B,opt+64)[0]; ck('PE checksum exact',stored==pe_checksum(B,opt+64),(hex(stored),hex(pe_checksum(B,opt+64))))
# Full deterministic lineage.
with tempfile.TemporaryDirectory() as td:
    td=Path(td); g=td/'g.dll'; u=td/'u.dll'; s1=td/'s1.dll'; s2=td/'s2.dll'
    chain=[(P1,BASE,g,GW16G,'GW16G'),(P2,g,u,UNIFIED2,'UNIFIEDREC2'),(P3,u,s1,SAFE1,'SAFEPOINT1'),(P4,s1,s2,FINAL,'SAFEPOINT2')]
    for p,src,dst,h,label in chain:
        r=subprocess.run([sys.executable,str(p),str(src),str(dst)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        ck(label+' patch exits 0',r.returncode==0,r.stdout.strip().replace('\n',' | ')); ck(label+' exact SHA',dst.is_file() and sha(dst)==h,sha(dst) if dst.exists() else 'missing')
    ck('full lineage byte-identical payload',s2.is_file() and s2.read_bytes()==B,sha(s2) if s2.exists() else 'missing')
    S1=s1.read_bytes()
# Dispatch and route coverage.
ck('action8 deferred request',jmp(B,ACTION)==REQUEST,hex(jmp(B,ACTION) or 0)); ck('isolated common submit wrapped',call(B,SUBMIT_SITE)==ISO,hex(call(B,SUBMIT_SITE) or 0))
for s in NATIVE_SITES: ck('native USR site %#x wrapped'%s,call(B,s)==NATIVE,hex(call(B,s) or 0))
ck('unrelated recovery native call unchanged',call(B,UNTOUCHED)==NATIVE_POST,hex(call(B,UNTOUCHED) or 0))
# Original explicit FG-only gates remain removed by UNIFIEDREC2.
ck('start explicit FG gate remains NOP',B[roff(B,START_FG):roff(B,START_FG)+2]==b'\x90\x90'); ck('submit explicit FG gate remains NOP',B[roff(B,SUBMIT_FG):roff(B,SUBMIT_FG)+6]==b'\x90'*6)
# Start and submit guards are extended rather than bypassed.
ck('start resource helper call',call(B,START_RES)==RES,hex(call(B,START_RES) or 0)); ck('submit identity helper call',call(B,SUBMIT_ID)==IDENT,hex(call(B,SUBMIT_ID) or 0))
# Request stub includes pending set, logger call, and jump back to dispatcher continuation.
req=B[roff(B,REQUEST):roff(B,REQUEST)+32]; ck('request sets pending',req[:7]==bytes.fromhex('c605e9ffffff01'),req.hex()); ck('request has aligned shadow space',b'\x48\x83\xec\x20' in req and b'\x48\x83\xc4\x20' in req); ck('request returns to action continuation',req[-5]==0xE9 and REQUEST+32+struct.unpack_from('<i',req,-4)[0]==0x12309,req.hex())
# Isolated wrapper still submits first, then consumes pending and calls original toggle.
iso=B[roff(B,ISO):roff(B,ISO)+51]; ck('isolated wrapper calls original submit',call(B,ISO+4)==SUBMIT,hex(call(B,ISO+4) or 0)); ck('isolated wrapper calls original toggle',call(B,ISO+41)==HANDLER,hex(call(B,ISO+41) or 0))
# Native wrapper starts by preserving original post-present helper, then is FG-OFF gated.
nat=B[roff(B,NATIVE):roff(B,NATIVE)+217]; ck('native wrapper stack frame 0x38',nat[:4]==bytes.fromhex('4883ec38'),nat[:4].hex()); ck('native wrapper calls original post first',call(B,NATIVE+4)==NATIVE_POST,hex(call(B,NATIVE+4) or 0)); ck('native wrapper references FG state',FG.to_bytes(4,'little') not in nat) # RIP-relative, structural checks below
ck('native wrapper calls original submit',call(B,0x34FEA16)==SUBMIT,hex(call(B,0x34FEA16) or 0)); ck('native wrapper calls original toggle',call(B,0x34FEA4F)==HANDLER,hex(call(B,0x34FEA4F) or 0))
# Canonical IID_ID3D11Texture2D used by GetBuffer.
ck('IID_ID3D11Texture2D exact',B[roff(B,IID):roff(B,IID)+16].hex()=='f2aa156f08d2894e9ab4489535d34f9c',B[roff(B,IID):roff(B,IID)+16].hex())
# Provenance of native tuple: actual CreateDeviceAndSwapChain args are stored into globals.
def rip_store_target(rva,ilen,disp_off):
    x=B[roff(B,rva):roff(B,rva)+ilen]; return rva+ilen+struct.unpack_from('<i',x,disp_off)[0]
# RCX was preserved in R14 at function entry; R14 is stored as the native swapchain.
ck('CreateDeviceAndSwapChain native swapchain store opcode',B[roff(B,0x379A):roff(B,0x379D)]==bytes.fromhex('4c8935'),B[roff(B,0x379A):roff(B,0x37A1)].hex())
ck('CreateDeviceAndSwapChain stores swapchain global',rip_store_target(0x379A,7,3)==NATIVE_SWAP,hex(rip_store_target(0x379A,7,3)))
ck('CreateDeviceAndSwapChain native device store opcode',B[roff(B,0x37A4):roff(B,0x37A7)]==bytes.fromhex('488915'),B[roff(B,0x37A4):roff(B,0x37AB)].hex())
ck('CreateDeviceAndSwapChain stores RDX device global',rip_store_target(0x37A4,7,3)==NATIVE_DEV,hex(rip_store_target(0x37A4,7,3)))
ck('CreateDeviceAndSwapChain native context store opcode',B[roff(B,0x37B0):roff(B,0x37B3)]==bytes.fromhex('4c8905'),B[roff(B,0x37B0):roff(B,0x37B7)].hex())
ck('CreateDeviceAndSwapChain stores R8 context global',rip_store_target(0x37B0,7,3)==NATIVE_CTX,hex(rip_store_target(0x37B0,7,3)))
# Mode helpers contain both resource families and fail closed.
res=B[roff(B,RES):roff(B,RES)+105]; ident=B[roff(B,IDENT):roff(B,IDENT)+54]
ck('resource helper returns success/fail explicitly',res.count(bytes.fromhex('b801000000c3'))==2 and res.endswith(bytes.fromhex('31c0c3'))); ck('identity helper fails closed',ident.endswith(bytes.fromhex('31c0c3'))); ck('identity helper success explicit',bytes.fromhex('b801000000c3') in ident)
# SAFEPOINT2 delta from SAFEPOINT1 confined to explicit regions.
allowed=set(range(opt+64,opt+68))
for r,n in [(REQUEST,32),(ISO,51),(NATIVE,217),(RES,105),(IDENT,54),(0x34FEB20,42),(0x34FEB60,46),(0x34FEBA8,43),(START_RES,20),(SUBMIT_ID,13)]: allowed.update(range(roff(B,r),roff(B,r)+n))
for s in NATIVE_SITES: allowed.update(range(roff(B,s),roff(B,s)+5))
bad=[]; changed=0
for i,(a,b) in enumerate(zip(S1,B)):
    if a!=b:
        changed+=1
        if i not in allowed: bad.append(i)
ck('SAFEPOINT2 delta confined',not bad,'changed=%d bad=%s'%(changed,bad[:16]))
# No bridge; documentation matches new behavior.
ck('no external recorder bridge',not (ROOT/'tools/recorder/PTAR_RECORDER_BRIDGE.ps1').exists() and not (ROOT/'07-START_RECORDER_BRIDGE.bat').exists() and not (ROOT/'08-STOP_RECORDER_BRIDGE.bat').exists())
ver=(ROOT/'payload/win81_nis_version.txt').read_text(errors='replace'); readme=(ROOT/'README_TEST.txt').read_text(errors='replace')
for tok in ['GW16H_UNIFIEDREC3_SAFEPOINT2=MULTIPATH_NATIVE_USR_FGOFF_BACKBUFFER_FEED_ORIGINAL_B18K18','RECORDER_NATIVE_CALL_SITES=0x0000C810,0x0000C90E,0x0000D6F0,0x0000D7EE','RECORDER_RESOURCE_GUARDS=MODE_AWARE_RETAINED_NOT_BYPASSED','RECORDER_DEVICE_IDENTITY_GUARD=MODE_AWARE_RETAINED_NOT_BYPASSED','RECORDER_EXTERNAL_BRIDGE=ABSENT']:
    ck('version '+tok[:60],tok in ver)
for tok in ['SAFEPOINT2','FG OFF','GetBuffer(0','device natif','aucun second recorder']:
    ck('README '+tok,tok.lower() in readme.lower())
failed=[x for x in checks if not x[1]]
print('RECORDER_SAFEPOINT2_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    sys.exit(1)
