#!/usr/bin/env python3
"""Static validator for GW16H UNIFIEDREC3 SAFEPOINT3 / LOCALGPU1.

Validates the exact surgical delta from SAFEPOINT2/NATIVEUSR1, reconstructs the
candidate from the frozen lineage, verifies PE topology/checksum and ensures that
B18K18 conversion no longer references the isolated-presenter VS/RS objects.
"""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile

ROOT = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]).resolve()
D = ROOT / 'diag'
PAY = ROOT / 'payload'
BASE = D / 'base' / 'GW12_BASE.dll'
OUT = PAY / 'win81_nis_dx11_x64.dll'
MIRROR = PAY / 'd3d11.dll'
PATCHES = [
    D / 'patch_gw16_auto_from_gw12.py',
    D / 'patch_gw16g_unified_recorder.py',
    D / 'patch_gw16h_recorder_safepoint.py',
    D / 'patch_gw16h_recorder_safepoint2.py',
    D / 'patch_gw16h_recorder_local_gpu_state.py',
]
BASE_SHA = '50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'
GW16G_SHA = '8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d'
UNIFIED2_SHA = '4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f'
SAFE1_SHA = '6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7'
SAFE2_SHA = '7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630'
FINAL_SHA = '961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2'
SIZE = 320000

INIT_CALL_SITE = 0x27AD5
ORIG_INIT = 0x2A400
PRIVATE_OWNER = 0x34FF4C0
PRIVATE_VS = 0x34FF4C8
PRIVATE_RS = 0x34FF4D0
WRAP = 0x34FF4E0
WRAP_SIZE = 413
CAVE_START = 0x34FF4BF
CAVE_END = 0x34FF700
SHARED_VS = 0x2C93A38
SHARED_RS = 0x2C93A80
VS_PRE = (0x2B747, 8, 4)
VS_BIND = (0x2BA2D, 7, 3)
RS_PRE = (0x2B7B1, 8, 3)
RS_BIND = (0x2BA7C, 7, 3)
COMPILE_SHADER = 0x21AB0
ORIG_RELEASE = 0x9FB0
MEMZERO = 0x5B80
VS_SOURCE = 0x320E0
VS_TARGET = 0x42C3A

checks = []
def ck(name, cond, detail=''):
    ok = bool(cond); checks.append((name, ok, str(detail)))
    print(('[PASS] ' if ok else '[FAIL] ') + name + ((' :: ' + str(detail)) if detail else ''))

def sha_bytes(b): return hashlib.sha256(bytes(b)).hexdigest()
def sha_file(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def parse_pe(b):
    e = struct.unpack_from('<I', b, 0x3c)[0]
    ck0 = b[e:e+4] == b'PE\0\0'
    if not ck0: raise ValueError('bad PE signature')
    coff = e + 4
    n = struct.unpack_from('<H', b, coff + 2)[0]
    optsz = struct.unpack_from('<H', b, coff + 16)[0]
    opt = coff + 20
    sh = opt + optsz
    secs=[]
    for i in range(n):
        o=sh+i*40
        name=b[o:o+8].rstrip(b'\0').decode('ascii', 'replace')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8)
        ch=struct.unpack_from('<I',b,o+36)[0]
        secs.append((name,vs,va,rs,rp,ch))
    return opt,secs

def roff(b,rva):
    for _n,vs,va,rs,rp,_ch in parse_pe(b)[1]:
        if va <= rva < va+max(vs,rs): return rp+(rva-va)
    raise ValueError('RVA not mapped %#x'%rva)

def call_target(b,site):
    o=roff(b,site)
    if b[o] != 0xE8: return None
    return site+5+struct.unpack_from('<i',b,o+1)[0]

def rip_target(b,site,ilen,disp_off):
    o=roff(b,site)
    return site+ilen+struct.unpack_from('<i',b,o+disp_off)[0]

def pe_checksum(blob, checksum_off):
    x=bytearray(blob); struct.pack_into('<I',x,checksum_off,0)
    s=0
    for i in range(0,len(x)-1,2):
        s += x[i] | (x[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(x)&1: s+=x[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(x))&0xffffffff

def run_patch(script, src, dst):
    cp=subprocess.run([sys.executable,str(script),str(src),str(dst)],capture_output=True,text=True)
    ck('patch '+script.name+' exit 0',cp.returncode==0,(cp.stdout+cp.stderr).strip().splitlines()[-1:] if cp.returncode else '')
    if cp.returncode: raise RuntimeError(cp.stdout+cp.stderr)

# Files and hashes.
for p in [BASE, OUT, MIRROR] + PATCHES:
    ck('exists '+str(p.relative_to(ROOT)), p.is_file())
if not all(p.is_file() for p in [BASE, OUT, MIRROR] + PATCHES):
    raise SystemExit(1)
ck('GW12 base hash', sha_file(BASE)==BASE_SHA, sha_file(BASE))
final=OUT.read_bytes(); mirror=MIRROR.read_bytes()
ck('runtime size', len(final)==SIZE, len(final))
ck('runtime final hash', sha_bytes(final)==FINAL_SHA, sha_bytes(final))
ck('d3d11 mirror byte-identical', mirror==final, sha_bytes(mirror))

# Rebuild exact lineage into temp files.
with tempfile.TemporaryDirectory(prefix='ptar_sp3_val_') as td:
    td=Path(td)
    s0=BASE
    built=[]
    cur=s0
    expected=[GW16G_SHA,UNIFIED2_SHA,SAFE1_SHA,SAFE2_SHA,FINAL_SHA]
    for i,(patch,exp) in enumerate(zip(PATCHES,expected),1):
        nxt=td/f'stage{i}.dll'; run_patch(patch,cur,nxt)
        h=sha_file(nxt); ck(f'lineage stage {i} exact hash',h==exp,h)
        built.append(nxt.read_bytes()); cur=nxt
    sp2=built[3]; rebuilt=built[4]
    ck('rebuild final byte-identical payload', rebuilt==final, sha_bytes(rebuilt))

# PE structural locks.
sp2_opt, sp2_secs=parse_pe(sp2); final_opt, final_secs=parse_pe(final)
ck('PE section topology unchanged',sp2_secs==final_secs)
ck('PE optional-header location unchanged',sp2_opt==final_opt,sp2_opt)
cs_off=final_opt+64
stored=struct.unpack_from('<I',final,cs_off)[0]
calc=pe_checksum(final,cs_off)
ck('PE checksum valid',stored==calc,'stored=%08x calc=%08x'%(stored,calc))
ck('SAFEPOINT2 cave was pristine CC',sp2[roff(sp2,CAVE_START):roff(sp2,CAVE_END)]==b'\xcc'*(CAVE_END-CAVE_START))
ck('private owner/VS/RS initialize NULL',final[roff(final,PRIVATE_OWNER):roff(final,PRIVATE_RS)+8]==b'\0'*24)

# Exact control-flow redirection.
ck('recorder init call redirected to local-state wrapper',call_target(final,INIT_CALL_SITE)==WRAP,hex(call_target(final,INIT_CALL_SITE) or 0))
ck('SP2 init call originally targeted recorder GPU init',call_target(sp2,INIT_CALL_SITE)==ORIG_INIT,hex(call_target(sp2,INIT_CALL_SITE) or 0))
for label,(site,ilen,disp),old,new in [
    ('VS precondition',VS_PRE,SHARED_VS,PRIVATE_VS),
    ('VS bind',VS_BIND,SHARED_VS,PRIVATE_VS),
    ('RS precondition',RS_PRE,SHARED_RS,PRIVATE_RS),
    ('RS bind',RS_BIND,SHARED_RS,PRIVATE_RS),
]:
    ck(label+' SP2 shared target',rip_target(sp2,site,ilen,disp)==old,hex(rip_target(sp2,site,ilen,disp)))
    ck(label+' SAFEPOINT3 private target',rip_target(final,site,ilen,disp)==new,hex(rip_target(final,site,ilen,disp)))

# Wrapper lock: exact prefix/epilogue, key calls, COM methods and compile constants.
wo=roff(final,WRAP); wb=final[wo:wo+WRAP_SIZE]
ck('wrapper exact size occupied',len(wb)==WRAP_SIZE)
ck('wrapper ABI prologue',wb.startswith(bytes.fromhex('53 56 57 48 81 ec 80 00 00 00')))
ck('wrapper ABI epilogue',wb.endswith(bytes.fromhex('48 81 c4 80 00 00 00 5f 5e 5b c3')))
# Known direct CALL RVAs emitted by the frozen patcher.
expected_calls={
    WRAP+0x0D:ORIG_INIT,
    WRAP+0x57:ORIG_RELEASE,
    WRAP+0x63:ORIG_RELEASE,
    WRAP+0x8F:COMPILE_SHADER,
    WRAP+0xE3:ORIG_RELEASE,
    WRAP+0x108:MEMZERO,
    WRAP+0x168:ORIG_RELEASE,
    WRAP+0x174:ORIG_RELEASE,
    WRAP+0x180:ORIG_RELEASE,
}
for site,target in expected_calls.items():
    ck('wrapper CALL %#x -> %#x'%(site,target),call_target(final,site)==target,hex(call_target(final,site) or 0))
ck('CreateVertexShader vtable +0x60 present',bytes.fromhex('ff 50 60') in wb)
ck('CreateRasterizerState vtable +0xB0 present',bytes.fromhex('ff 90 b0 00 00 00') in wb)
ck('rasterizer descriptor exact 0x28 memset',bytes.fromhex('ba 28 00 00 00') in wb)
ck('rasterizer SOLID/NONE values present',bytes.fromhex('48 b8 03 00 00 00 01 00 00 00') in wb)
ck('DepthClipEnable=1 present',bytes.fromhex('c7 44 24 48 01 00 00 00') in wb)
# Ensure wrapper embeds RIP-relative refs to existing VS source and vs_5_0 target.
found_vs_source=False; found_vs_target=False
for i in range(len(wb)-7):
    rva=WRAP+i
    if wb[i:i+3] in (b'\x48\x8d\x0d',b'\x48\x8d\x15'):
        t=rva+7+struct.unpack_from('<i',wb,i+3)[0]
        if t==VS_SOURCE: found_vs_source=True
        if t==VS_TARGET: found_vs_target=True
ck('existing fullscreen VS source reused',found_vs_source)
ck('existing vs_5_0 target reused',found_vs_target)

# Exact surgical delta: no bytes outside checksum, call displacement, four RIP displacements and private cave.
allowed=set(range(cs_off,cs_off+4))
o=roff(sp2,INIT_CALL_SITE); allowed.update(range(o+1,o+5))
for site,ilen,disp in [VS_PRE,VS_BIND,RS_PRE,RS_BIND]:
    o=roff(sp2,site); allowed.update(range(o+disp,o+disp+4))
allowed.update(range(roff(sp2,PRIVATE_OWNER), roff(sp2,WRAP+WRAP_SIZE)))
diff=[i for i,(a,b) in enumerate(zip(sp2,final)) if a!=b]
bad=[i for i in diff if i not in allowed]
ck('surgical delta has no out-of-scope bytes',not bad,('bad='+','.join(hex(x) for x in bad[:8])) if bad else '')
ck('surgical delta exact changed-byte count',len(diff)==459,len(diff))
# Opcodes at redirected instructions must be byte-identical outside displacements.
for label,(site,ilen,disp) in [('VS pre',VS_PRE),('VS bind',VS_BIND),('RS pre',RS_PRE),('RS bind',RS_BIND)]:
    a=sp2[roff(sp2,site):roff(sp2,site)+ilen]; b=final[roff(final,site):roff(final,site)+ilen]
    aa=a[:disp]+a[disp+4:]; bb=b[:disp]+b[disp+4:]
    ck(label+' opcode/operands unchanged',aa==bb)

# Conversion helper must no longer reference either shared isolated-presenter object at the four locked sites.
ck('conversion four shared FG-state references removed',all(rip_target(final,*x)!=SHARED_VS and rip_target(final,*x)!=SHARED_RS for x in [VS_PRE,VS_BIND,RS_PRE,RS_BIND]))

# Documentation/field finding locks.
version=(PAY/'win81_nis_version.txt').read_text(errors='replace')
readme=(ROOT/'README_TEST.txt').read_text(errors='replace')
finding=(D/'GW16H_SAFEPOINT2_ZEROFRAME_FINDING.txt').read_text(errors='replace')
for token in ['SAFEPOINT3','LOCALGPU1',FINAL_SHA,'RECORDER_LOCAL_VS_RVA=0x034FF4C8','RECORDER_LOCAL_RS_RVA=0x034FF4D0']:
    ck('version token '+token,token in version)
for token in ['155','262','recorder-local','FG OFF']:
    ck('README/finding documents '+token,(token in readme) or (token in finding))

failed=[x for x in checks if not x[1]]
print('RECORDER_LOCAL_GPU_STATE_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
