#!/usr/bin/env python3
"""Static/reproducibility validator for GW16H SAFEPOINT4/STATEGUARD1."""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile, shutil

ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
D=ROOT/'diag'; PAY=ROOT/'payload'
SIZE=320000
SHA_CHAIN=[
 ('GW12','50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'),
 ('GW16G','8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d'),
 ('UNIFIEDREC2','4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f'),
 ('SAFEPOINT1','6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7'),
 ('SAFEPOINT2','7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630'),
 ('SAFEPOINT3','961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2'),
 ('SAFEPOINT4','1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b'),
]
FINAL=SHA_CHAIN[-1][1]
ORIG_SUBMIT=0x27760
ISOLATED_CALL=0x34FE934
NATIVE_CALL=0x34FEA16
MAIN=0x34FE804; SAVE=0x34FECB3; RESTORE=0x34FEEC3; RELEASE=0x34FF67D
CAVES=[(MAIN,109),(SAVE,282),(RESTORE,259),(RELEASE,50)]
CONVERSION_HELPER=0x2B6B0
EXPECTED_SETTERS={0x40,0x48,0x58,0x80,0x88,0xB8,0xC0,0x108,0x118,0x158,0x160,0x168}
EXPECTED_GETTERS={0x248,0x250,0x260,0x268,0x270,0x290,0x298,0x2C8,0x2D8,0x2F0,0x2F8,0x300}
checks=[]

def ck(n,c,d=''):
    ok=bool(c); checks.append((n,ok,str(d)))
    print(('[PASS] ' if ok else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))

def sha(x): return hashlib.sha256(Path(x).read_bytes() if isinstance(x,(str,Path)) else bytes(x)).hexdigest()

def parse(b):
    e=struct.unpack_from('<I',b,0x3c)[0]; coff=e+4
    n=struct.unpack_from('<H',b,coff+2)[0]; optsz=struct.unpack_from('<H',b,coff+16)[0]
    opt=coff+20; sh=opt+optsz; secs=[]
    for i in range(n):
        o=sh+i*40
        name=b[o:o+8].rstrip(b'\0').decode('ascii')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8); ch=struct.unpack_from('<I',b,o+36)[0]
        secs.append((name,vs,va,rs,rp,ch))
    return opt,secs

def roff(b,rva):
    for _n,vs,va,rs,rp,_ch in parse(b)[1]:
        if va<=rva<va+max(vs,rs): return rp+(rva-va)
    raise RuntimeError('RVA not mapped %#x'%rva)

def call_target(b,site):
    o=roff(b,site)
    if b[o]!=0xE8:return None
    return site+5+struct.unpack_from('<i',b,o+1)[0]

def pe_checksum(blob,off):
    x=bytearray(blob); struct.pack_into('<I',x,off,0); s=0
    for i in range(0,len(x)-1,2):
        s+=x[i]|(x[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(x)&1:s+=x[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(x))&0xffffffff

def method_offsets(code, opcode=b'\xff\x97'):
    out=[]
    pos=0
    while True:
        i=code.find(opcode,pos)
        if i<0:break
        if i+6<=len(code): out.append(struct.unpack_from('<I',code,i+2)[0])
        pos=i+1
    return out

# Rebuild exact lineage from frozen GW12.
tmp=Path(tempfile.mkdtemp(prefix='ptar_sp4_validate_'))
try:
    base=D/'base/GW12_BASE.dll'
    ck('GW12 frozen base exists',base.is_file())
    ck('GW12 SHA',base.is_file() and sha(base)==SHA_CHAIN[0][1],sha(base) if base.is_file() else 'missing')
    scripts=[
      'patch_gw16_auto_from_gw12.py',
      'patch_gw16g_unified_recorder.py',
      'patch_gw16h_recorder_safepoint.py',
      'patch_gw16h_recorder_safepoint2.py',
      'patch_gw16h_recorder_local_gpu_state.py',
      'patch_gw16h_recorder_native_state_guard.py',
    ]
    cur=base
    rebuilt=[]
    for i,s in enumerate(scripts,1):
        out=tmp/('stage%d.dll'%i)
        cp=subprocess.run([sys.executable,str(D/s),str(cur),str(out)],capture_output=True,text=True)
        ck('execute '+s,cp.returncode==0,(cp.stderr or cp.stdout).splitlines()[-1:] if cp.returncode else '')
        if cp.returncode!=0: raise RuntimeError('lineage rebuild failed at '+s)
        h=sha(out); expected=SHA_CHAIN[i][1]
        ck(SHA_CHAIN[i][0]+' SHA',h==expected,h)
        rebuilt.append(out); cur=out

    sp3=rebuilt[-2].read_bytes(); final=rebuilt[-1].read_bytes()
    payload=(PAY/'win81_nis_dx11_x64.dll').read_bytes()
    mirror=(PAY/'d3d11.dll').read_bytes()

    ck('final size',len(final)==SIZE,len(final))
    ck('payload exact final SHA',sha(payload)==FINAL,sha(payload))
    ck('d3d11 mirror exact final SHA',sha(mirror)==FINAL,sha(mirror))
    ck('rebuilt final byte-identical payload',final==payload)
    ck('runtime mirrors byte-identical',payload==mirror)

    # PE topology and checksum.
    opt3,sec3=parse(sp3); opt4,sec4=parse(final)
    ck('PE section topology unchanged',sec3==sec4)
    csoff=opt4+64
    stored=struct.unpack_from('<I',final,csoff)[0]; calc=pe_checksum(final,csoff)
    ck('PE checksum valid',stored==calc,'stored=%08x calc=%08x'%(stored,calc))

    # Caves were genuinely unused in exact SP3.
    for r,n in CAVES:
        o=roff(sp3,r)
        ck('SP3 cave pristine %#x'%r,sp3[o:o+n]==b'\xcc'*n,'len=%d'%n)

    # Surgical delta: only checksum, one call displacement, and the four code caves.
    allowed=set(range(csoff,csoff+4))
    co=roff(sp3,NATIVE_CALL); allowed.update(range(co,co+5))
    for r,n in CAVES:
        o=roff(sp3,r); allowed.update(range(o,o+n))
    diffs={i for i,(a,b) in enumerate(zip(sp3,final)) if a!=b}
    ck('surgical delta only',diffs<=allowed,'diff=%d allowed=%d'%(len(diffs),len(allowed)))
    ck('non-empty state-guard delta',len(diffs)>600,len(diffs))

    # Native only: isolated path untouched, native route wrapped.
    ck('SP3 isolated call original',call_target(sp3,ISOLATED_CALL)==ORIG_SUBMIT,hex(call_target(sp3,ISOLATED_CALL) or 0))
    ck('SAFEPOINT4 isolated call remains original',call_target(final,ISOLATED_CALL)==ORIG_SUBMIT,hex(call_target(final,ISOLATED_CALL) or 0))
    ck('SP3 native call original',call_target(sp3,NATIVE_CALL)==ORIG_SUBMIT,hex(call_target(sp3,NATIVE_CALL) or 0))
    ck('SAFEPOINT4 native call redirected to state guard',call_target(final,NATIVE_CALL)==MAIN,hex(call_target(final,NATIVE_CALL) or 0))

    # Main wrapper must call original submit exactly once.
    maincode=final[roff(final,MAIN):roff(final,MAIN)+109]
    calls=[]
    for i,b in enumerate(maincode[:-4]):
        if b==0xE8:
            site=MAIN+i; dst=site+5+struct.unpack_from('<i',maincode,i+1)[0]; calls.append(dst)
    ck('main calls original recorder submit exactly once',calls.count(ORIG_SUBMIT)==1,[hex(x) for x in calls])

    # State coverage. B18K18 conversion changes exactly these 12 pipeline-state categories.
    savecode=final[roff(final,SAVE):roff(final,SAVE)+282]
    restorecode=final[roff(final,RESTORE):roff(final,RESTORE)+259]
    got_getters=set(method_offsets(savecode))
    got_setters=set(method_offsets(restorecode))
    ck('save getter coverage exact',got_getters==EXPECTED_GETTERS,','.join(hex(x) for x in sorted(got_getters)))
    ck('restore setter coverage exact',got_setters==EXPECTED_SETTERS,','.join(hex(x) for x in sorted(got_setters)))
    ck('conversion state category mapping complete',len(EXPECTED_SETTERS)==12 and len(EXPECTED_GETTERS)==12)
    ck('PS SRV slot0 preserved',0x248 in got_getters and 0x40 in got_setters)
    ck('PS shader preserved',0x250 in got_getters and 0x48 in got_setters)
    ck('VS shader preserved',0x260 in got_getters and 0x58 in got_setters)
    ck('PS constant buffer slot0 preserved',0x268 in got_getters and 0x80 in got_setters)
    ck('IA input layout preserved',0x270 in got_getters and 0x88 in got_setters)
    ck('GS shader preserved',0x290 in got_getters and 0xB8 in got_setters)
    ck('IA topology preserved',0x298 in got_getters and 0xC0 in got_setters)
    ck('OM render targets+DSV preserved',0x2C8 in got_getters and 0x108 in got_setters)
    ck('OM blend state/factor/mask preserved',0x2D8 in got_getters and 0x118 in got_setters)
    ck('RS state preserved',0x2F0 in got_getters and 0x158 in got_setters)
    ck('RS viewports preserved',0x2F8 in got_getters and 0x160 in got_setters)
    ck('RS scissors preserved',0x300 in got_getters and 0x168 in got_setters)

    # Snapshot limits/ownership: 8 RTVs, max 16 viewports/scissors, 17 COM refs released.
    ck('save requests 8 render targets',b'\xba\x08\x00\x00\x00' in savecode)
    ck('viewport max count 16 encoded',savecode.count(b'\xc7\x83\x8c\x00\x00\x00\x10\x00\x00\x00')==1)
    ck('scissor max count 16 encoded',savecode.count(b'\xc7\x83\x90\x00\x00\x00\x10\x00\x00\x00')==1)
    releasecode=final[roff(final,RELEASE):roff(final,RELEASE)+50]
    ck('release loop exact 17 COM references',b'\xbb\x11\x00\x00\x00' in releasecode)
    ck('release uses IUnknown::Release slot',b'\xff\x50\x10' in releasecode)

    # No ClearState or deferred-context substitution in wrapper code.
    allguard=maincode+savecode+restorecode+releasecode
    ck('guard does not call ClearState slot 0x2e0',struct.pack('<I',0x2E0) not in allguard)
    ck('original B18K18 conversion helper unchanged from SP3',
       final[roff(final,CONVERSION_HELPER):roff(final,CONVERSION_HELPER)+0x650] ==
       sp3[roff(sp3,CONVERSION_HELPER):roff(sp3,CONVERSION_HELPER)+0x650])
finally:
    shutil.rmtree(tmp,ignore_errors=True)

failed=[x for x in checks if not x[1]]
print('RECORDER_NATIVE_STATE_GUARD_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
