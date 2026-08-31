#!/usr/bin/env python3
import hashlib, struct, sys, subprocess, tempfile
from pathlib import Path

ROOT=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
BASE_SHA='5bdff6f5ecce928cb62f5dbbfe0cd3562eff455054620223cc4ba268c12c92fc'
OUT_SHA='2b6fafabeedc89857dbb6d5a318ca143ce8e30ec912050116f70a4a58ad55ad3'
VISIBLE_SHA='67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305'
checks=[]
def ck(name, cond, detail=''):
    ok=bool(cond); checks.append(ok)
    print(('[PASS] ' if ok else '[FAIL] ')+name+((' :: '+detail) if detail else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def parse_pe(b):
    pe=struct.unpack_from('<I',b,0x3c)[0]; assert b[pe:pe+4]==b'PE\0\0'
    coff=pe+4; machine,nsec,_ts,_ps,_ns,optsz,_ch=struct.unpack_from('<HHIIIHH',b,coff)
    opt=coff+20; image=struct.unpack_from('<Q',b,opt+24)[0]; size_image=struct.unpack_from('<I',b,opt+56)[0]
    checksum=struct.unpack_from('<I',b,opt+64)[0]; shoff=opt+optsz
    secs=[]
    for i in range(nsec):
        o=shoff+i*40; name=b[o:o+8].rstrip(b'\0').decode('ascii','replace'); vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8)
        secs.append((name,va,vs,rp,rs,b[o:o+40]))
    return (image,size_image,checksum,secs,opt+64)
def rva_off(secs,rva):
    for n,va,vs,rp,rs,_ in secs:
        if va<=rva<va+max(vs,rs): return rp+rva-va
    raise ValueError(hex(rva))

def call_targets(b, secs, target_rva):
    text=next(s for s in secs if s[0]=='.text')
    _,va,vs,rp,rs,_=text; out=[]
    raw=b[rp:rp+rs]
    for i in range(len(raw)-4):
        if raw[i]==0xe8:
            disp=struct.unpack_from('<i',raw,i+1)[0]
            rva=va+i
            if rva+5+disp==target_rva: out.append(rva)
    return out

base=ROOT/'diag/r/r070.dll'; out=ROOT/'win81_nis_dx11_x64.dll'; verifier=ROOT/'diag/visible_verifier/win81_vblank2_visible_marker_x64.exe'
ck('RC17 exact base reference present',base.is_file() and sha(base)==BASE_SHA)
ck('RC18 runtime hash exact',out.is_file() and sha(out)==OUT_SHA)
ck('ESCSAFE1 visible verifier unchanged',verifier.is_file() and sha(verifier)==VISIBLE_SHA)

A=base.read_bytes(); B=out.read_bytes(); pa=parse_pe(A); pb=parse_pe(B)
ck('runtime file size unchanged',len(A)==len(B),f'{len(A)} bytes')
ck('image base unchanged',pa[0]==pb[0]==0x180000000)
ck('SizeOfImage unchanged',pa[1]==pb[1])
ck('section count/geometry unchanged',[(x[0],x[1],x[2],x[3],x[4]) for x in pa[3]]==[(x[0],x[1],x[2],x[3],x[4]) for x in pb[3]])

# Exact patch sites.
for rva in (0x259c9,0x25b58):
    oa=rva_off(pa[3],rva); ob=rva_off(pb[3],rva)
    ck(f'pre-Present WaitForVBlank call NOP at RVA {rva:#x}',B[ob:ob+5]==b'\x90'*5)
ck('Present args are SyncInterval=1 Flags=0',B[rva_off(pb[3],0x265cf):rva_off(pb[3],0x265cf)+8]==bytes.fromhex('ba010000004531c0'))
ck('IDXGISwapChain::Present call opcode untouched',A[rva_off(pa[3],0x265d7):rva_off(pa[3],0x265d7)+2]==B[rva_off(pb[3],0x265d7):rva_off(pb[3],0x265d7)+2]==b'\xff\xd0')
ck('no display-loop call to FGVblankWaitDisplaySlot remains',call_targets(B,pb[3],0x26400)==[],str(call_targets(B,pb[3],0x26400)))
ck('RC17 had exactly two FGVblankWaitDisplaySlot calls',call_targets(A,pa[3],0x26400)==[0x259c9,0x25b58],str(call_targets(A,pa[3],0x26400)))
wa=rva_off(pa[3],0x26400); wb=rva_off(pa[3],0x2653c)
ck('FGVblankWaitDisplaySlot implementation byte-identical',A[wa:wb]==B[wa:wb])

# Critical injected/governor/quality sections untouched.
for name in ['.fgov','.fgdat','.fgdia']:
    sa=next(s for s in pa[3] if s[0]==name); sb=next(s for s in pb[3] if s[0]==name)
    ck(name+' raw bytes byte-identical',A[sa[3]:sa[3]+sa[4]]==B[sb[3]:sb[3]+sb[4]])

old=b'P1FG7N-VBLANK2 PRESENT MODE DISCARD + PRESENT(0,DO_NOT_WAIT) - EXACT N SWAPCHAIN RETAINED'
new=b'P1FG7N-PRESENTDELIVERY1 PRESENT(1,0) VSYNC - PREWAIT REMOVED - EXACT N SWAPCHAIN RETAINED'
ck('old Present policy status removed',old not in B)
ck('PRESENTDELIVERY1 status present exactly once',B.count(new)==1)

# Rebuild determinism from exact RC17 base.
patcher=ROOT/'diag/source_hotfix/patch_presentdelivery1_rc17.py'
with tempfile.TemporaryDirectory() as td:
    rebuilt=Path(td)/'rebuilt.dll'
    cp=subprocess.run([sys.executable,str(patcher),str(base),str(rebuilt)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    ck('deterministic patcher exits 0',cp.returncode==0,cp.stdout.strip().splitlines()[-1] if cp.stdout.strip() else '')
    ck('deterministic rebuild byte-identical',rebuilt.is_file() and rebuilt.read_bytes()==B)

# Package policy surface.
ini=(ROOT/'win81_nis.ini').read_text(errors='replace')
ref=(ROOT/'_PTAR_UNINSTALL/reference/win81_nis.CADENCEFIX1_BASELINE.ini').read_text(errors='replace')
for label,t in [('root INI',ini),('reference INI',ref)]:
    ck(label+' RequireVSync=0','FrameGenerationRequireVSync=0' in t)
    ck(label+' PresentSync=1','FrameGenerationPresentSync=1' in t)
    ck(label+' FG starts OFF','FrameGeneration=0' in t)
    ck(label+' CONSERVATIVE available','FrameGenerationQuality=2' in t or 'FrameGenerationQuality=3' in t)

core=(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat').read_text(errors='replace')
verify=(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat').read_text(errors='replace')
for token in [OUT_SHA,'KEEP_FG_REQUIRE_VSYNC=0','KEEP_FG_PRESENT_SYNC=1','FG_VISIBLE_DELIVERY=PRESENTDELIVERY1_PREWAIT_REMOVED_SYNC1','FRAME_GENERATION_PRESENT=SYNC1_FLAGS0_ISOLATED_DISPLAY_THREAD']:
    ck('core installer token '+token,token in core)
for token in [OUT_SHA,'FrameGenerationRequireVSync=0','FrameGenerationPresentSync=1','FG_VISIBLE_DELIVERY=PRESENTDELIVERY1_PREWAIT_REMOVED_SYNC1','FRAME_GENERATION_PRESENT=SYNC1_FLAGS0_ISOLATED_DISPLAY_THREAD']:
    ck('fullstack verifier token '+token,token in verify)

passed=sum(checks); total=len(checks)
print(f'PRESENTDELIVERY1 RC18 STATIC VALIDATION: {"PASS" if passed==total else "FAIL"} {passed}/{total}')
if passed!=total: sys.exit(1)
