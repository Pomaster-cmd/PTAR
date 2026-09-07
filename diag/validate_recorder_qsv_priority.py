#!/usr/bin/env python3
"""Static/reproducibility validator for SAFEPOINT5/QSVPRIORITY1 retained through SAFEPOINT7/TDETAIL3."""
from pathlib import Path
import hashlib, struct, subprocess, sys, tempfile, shutil

ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
D=ROOT/'diag'; PAY=ROOT/'payload'
SIZE=320000
CHAIN=[
 ('GW12','50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'),
 ('GW16G','8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d'),
 ('UNIFIEDREC2','4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f'),
 ('SAFEPOINT1','6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7'),
 ('SAFEPOINT2','7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630'),
 ('SAFEPOINT3','961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2'),
 ('SAFEPOINT4','1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b'),
 ('SAFEPOINT5','50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92'),
 ('SAFEPOINT6','e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53'),
 ('SAFEPOINT7','c573b4c4ca102867ca67dbd16e1e6d36e3057f43d85b343fca94803000de0040'),
 ('SAFEPOINT8','613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70'),
]
FINAL=CHAIN[-1][1]
READBACK_PRI_RVA=0x1F5C9
ENCODE_PRI_RVA=0x1F679
FFMPEG_FLAGS_RVA=0x20687
STATE_GUARD_REGIONS=[(0x34FE804,109),(0x34FECB3,282),(0x34FEEC3,259),(0x34FF67D,50)]
checks=[]

def ck(n,c,d=''):
    ok=bool(c); checks.append((n,ok,str(d)))
    print(('[PASS] ' if ok else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))

def sha(x):
    if isinstance(x,(str,Path)): return hashlib.sha256(Path(x).read_bytes()).hexdigest()
    return hashlib.sha256(bytes(x)).hexdigest()

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

def pe_checksum(blob,off):
    x=bytearray(blob); struct.pack_into('<I',x,off,0); s=0
    for i in range(0,len(x)-1,2):
        s+=x[i]|(x[i+1]<<8); s=(s&0xffff)+(s>>16)
    if len(x)&1:s+=x[-1]
    s=(s&0xffff)+(s>>16); s=(s&0xffff)+(s>>16)
    return (s+len(x))&0xffffffff

# Full deterministic lineage.
tmp=Path(tempfile.mkdtemp(prefix='ptar_sp5_validate_'))
try:
    scripts=[
      'patch_gw16_auto_from_gw12.py',
      'patch_gw16g_unified_recorder.py',
      'patch_gw16h_recorder_safepoint.py',
      'patch_gw16h_recorder_safepoint2.py',
      'patch_gw16h_recorder_local_gpu_state.py',
      'patch_gw16h_recorder_native_state_guard.py',
      'patch_gw16h_recorder_qsv_priority.py',
      'patch_gw16h_fg_tdetail2.py',
      'patch_gw16h_fg_tdetail3.py',
      'patch_gw16h_fg_tdetail4.py',
    ]
    cur=D/'base/GW12_BASE.dll'; ck('GW12 frozen base exists',cur.is_file()); ck('GW12 SHA',cur.is_file() and sha(cur)==CHAIN[0][1],sha(cur) if cur.is_file() else 'missing')
    stages=[]
    for i,s in enumerate(scripts,1):
        out=tmp/('stage%d.dll'%i)
        cp=subprocess.run([sys.executable,str(D/s),str(cur),str(out)],capture_output=True,text=True)
        ck('execute '+s,cp.returncode==0,(cp.stderr or cp.stdout).splitlines()[-1:] if cp.returncode else '')
        if cp.returncode!=0: raise RuntimeError('lineage failed '+s)
        ck(CHAIN[i][0]+' SHA',sha(out)==CHAIN[i][1],sha(out)); stages.append(out); cur=out

    sp4=stages[-5].read_bytes(); sp5=stages[-4].read_bytes(); sp6=stages[-3].read_bytes(); sp7=stages[-2].read_bytes(); final=stages[-1].read_bytes(); payload=(PAY/'win81_nis_dx11_x64.dll').read_bytes(); mirror=(PAY/'d3d11.dll').read_bytes()
    ck('SAFEPOINT8 payload exact final',sha(payload)==FINAL,sha(payload)); ck('SAFEPOINT5 intermediate exact',hashlib.sha256(sp5).hexdigest()==CHAIN[-4][1],hashlib.sha256(sp5).hexdigest())
    ck('d3d11 mirror exact final',sha(mirror)==FINAL,sha(mirror))
    ck('runtime mirrors byte-identical',payload==mirror)
    ck('full lineage reproduces payload',final==payload)
    ck('PE size unchanged',len(sp4)==len(final)==SIZE,len(final))
    ck('section topology unchanged',parse(sp4)[1]==parse(final)[1])

    # Exact scheduling changes.
    ck('GPU readback worker remains BELOW_NORMAL',sp4[roff(sp4,READBACK_PRI_RVA):roff(sp4,READBACK_PRI_RVA)+5]==bytes.fromhex('BAFFFFFFFF') and final[roff(final,READBACK_PRI_RVA):roff(final,READBACK_PRI_RVA)+5]==bytes.fromhex('BAFFFFFFFF'))
    ck('encode thread base was BELOW_NORMAL',sp4[roff(sp4,ENCODE_PRI_RVA):roff(sp4,ENCODE_PRI_RVA)+5]==bytes.fromhex('BAFFFFFFFF'))
    ck('encode thread is NORMAL',final[roff(final,ENCODE_PRI_RVA):roff(final,ENCODE_PRI_RVA)+5]==bytes.fromhex('BA00000000'))
    ck('FFmpeg base flags BELOW_NORMAL+NO_WINDOW',sp4[roff(sp4,FFMPEG_FLAGS_RVA):roff(sp4,FFMPEG_FLAGS_RVA)+8]==bytes.fromhex('C744242800400008'))
    ck('FFmpeg final flags NORMAL+NO_WINDOW',final[roff(final,FFMPEG_FLAGS_RVA):roff(final,FFMPEG_FLAGS_RVA)+8]==bytes.fromhex('C744242800000008'))

    # Delta: only five functional bytes plus PE checksum are permitted.
    opt,_=parse(sp4); csoff=opt+64
    allowed=set(range(csoff,csoff+4))
    allowed.update(range(roff(sp4,ENCODE_PRI_RVA)+1,roff(sp4,ENCODE_PRI_RVA)+5))
    allowed.add(roff(sp4,FFMPEG_FLAGS_RVA)+5)
    changed=[i for i,(a,b) in enumerate(zip(sp4,sp5)) if a!=b]
    bad=[i for i in changed if i not in allowed]
    functional=[i for i in changed if i not in range(csoff,csoff+4)]
    ck('delta confined to consumer priority + PE checksum',not bad,'changed=%d bad=%s'%(len(changed),[hex(x) for x in bad]))
    ck('exactly five functional bytes changed',len(functional)==5,[hex(x) for x in functional])
    ck('state guard machine code byte-identical',all(sp4[roff(sp4,r):roff(sp4,r)+n]==sp5[roff(sp5,r):roff(sp5,r)+n] for r,n in STATE_GUARD_REGIONS))

    # PE checksum valid.

    ck('SAFEPOINT8 retains encode thread NORMAL',final[roff(final,ENCODE_PRI_RVA):roff(final,ENCODE_PRI_RVA)+5]==sp5[roff(sp5,ENCODE_PRI_RVA):roff(sp5,ENCODE_PRI_RVA)+5])
    ck('SAFEPOINT8 retains FFmpeg flags',final[roff(final,FFMPEG_FLAGS_RVA):roff(final,FFMPEG_FLAGS_RVA)+8]==sp5[roff(sp5,FFMPEG_FLAGS_RVA):roff(sp5,FFMPEG_FLAGS_RVA)+8])
    stored=struct.unpack_from('<I',final,csoff)[0]
    ck('PE checksum recomputed',stored==pe_checksum(final,csoff),hex(stored))

    # Imports/exports and command strings untouched.
    try:
        p4=subprocess.check_output(['objdump','-p',str(stages[-2])],text=True,stderr=subprocess.STDOUT)
        p5=subprocess.check_output(['objdump','-p',str(stages[-1])],text=True,stderr=subprocess.STDOUT)
        dll4=[x.split('DLL Name:',1)[1].strip().lower() for x in p4.splitlines() if 'DLL Name:' in x]
        dll5=[x.split('DLL Name:',1)[1].strip().lower() for x in p5.splitlines() if 'DLL Name:' in x]
        ck('static import DLL set unchanged',dll4==dll5,dll5)
    except Exception as e: ck('import audit',False,e)
    for text in ['h264_qsv','fixed-QPC','QSV target bitrate kbps','B18K18 bounded QSV queue frames']:
        a=text.encode('ascii'); w=text.encode('utf-16le')
        ck('recorder policy string retained '+text,(a in final) or (w in final))
finally:
    shutil.rmtree(tmp,ignore_errors=True)

failed=[x for x in checks if not x[1]]
print('RECORDER_QSV_PRIORITY_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
