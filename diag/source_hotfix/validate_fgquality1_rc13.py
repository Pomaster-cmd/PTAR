#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, struct, subprocess, tempfile, sys

ROOT = Path(__file__).resolve().parents[2]
RUNTIME='2c8b77c540988c5671f604abeaf244c660e2ede11f5b74fb716c7e9a184affd2'
RC12='b253f0457f61a7c268ea3747864eb53e14ab113464f3c62f2a137da56bc18148'
BUILD_ID='2026082924'
PE_CHECKSUM=0x5263A
WIDTH_RVA=0x220BD
HEIGHT_RVA=0x220CC
WIDTH_OLD=bytes.fromhex('8d43020fb7c069c0abaa0000c1e811')
WIDTH_NEW=bytes.fromhex('8d4301d1e890909090909090909090')
HEIGHT_OLD=bytes.fromhex('418d4e020fb7c969c9abaa0000c1e911')
HEIGHT_NEW=bytes.fromhex('418d4e01d1e990909090909090909090')
EXPECTED_EXPORTS={
 'D3D11CoreCreateDevice','D3D11CoreCreateLayeredDevice','D3D11CoreGetLayeredDeviceSize',
 'D3D11CoreRegisterLayers','D3D11CreateDevice','D3D11CreateDeviceAndSwapChain','D3D11On12CreateDevice'
}

def sha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for c in iter(lambda:f.read(1<<20),b''): h.update(c)
    return h.hexdigest()

def txt(p):
    return p.read_text(encoding='utf-8',errors='strict').replace('\r\n','\n').replace('\r','\n')

def rel(p): return p.relative_to(ROOT).as_posix()

def pe_info(b):
    e=struct.unpack_from('<I',b,0x3c)[0]
    machine,n=struct.unpack_from('<HH',b,e+4)
    opt=e+24
    optsz=struct.unpack_from('<H',b,e+20)[0]
    sec=opt+optsz
    sections=[]
    for i in range(n):
        o=sec+i*40
        name=b[o:o+8].split(b'\0')[0].decode('ascii')
        vs,va,rs,rp=struct.unpack_from('<IIII',b,o+8)
        sections.append((name,vs,va,rs,rp))
    return {
        'machine':machine,'n':n,'magic':struct.unpack_from('<H',b,opt)[0],
        'subver':struct.unpack_from('<HH',b,opt+48),'subsystem':struct.unpack_from('<H',b,opt+68)[0],
        'checksum':struct.unpack_from('<I',b,opt+64)[0], 'sections':sections
    }

def rva_to_off(b,rva):
    info=pe_info(b)
    for name,vs,va,rs,rp in info['sections']:
        if va <= rva < va+max(vs,rs): return rp+(rva-va)
    if rva < 0x400: return rva
    raise ValueError(hex(rva))

def me_dim(v):
    q=(v+1)//2
    if q < 64: q=64
    q=(q+15)&0x7ff0
    cap=v & 0x3ffe
    return min(cap,q)

def main():
    checks=[]
    def ck(name,cond,detail=''):
        if not cond: raise AssertionError(name + (f' :: {detail}' if detail else ''))
        checks.append(name)

    cur=ROOT/'win81_nis_dx11_x64.dll'; base=ROOT/'diag/r/r065.dll'
    ck('runtime exists',cur.is_file())
    ck('RC12 field-working reference r065 exists',base.is_file())
    ck('runtime SHA exact',sha(cur)==RUNTIME,sha(cur))
    ck('RC12 r065 SHA exact',sha(base)==RC12,sha(base))
    C=cur.read_bytes(); B=base.read_bytes()
    ck('runtime size unchanged',len(C)==len(B)==308224,f'{len(B)}->{len(C)}')

    # PE/ABI invariants.
    pi=pe_info(C); pb=pe_info(B)
    ck('PE AMD64',pi['machine']==0x8664,hex(pi['machine']))
    ck('PE32+',pi['magic']==0x20b,hex(pi['magic']))
    ck('Windows GUI subsystem',pi['subsystem']==2,str(pi['subsystem']))
    ck('Windows subsystem version 6.0',pi['subver']==(6,0),repr(pi['subver']))
    ck('PE checksum exact',pi['checksum']==PE_CHECKSUM,hex(pi['checksum']))
    ck('section geometry identical to RC12',pi['sections']==pb['sections'],repr(pi['sections']))
    ck('eight sections retained',pi['n']==8,str(pi['n']))

    objdump='/usr/local/swift/usr/bin/llvm-objdump'
    if not Path(objdump).exists(): objdump='llvm-objdump'
    out=subprocess.check_output([objdump,'-p',str(cur)],text=True,errors='replace')
    dlls=re.findall(r'DLL Name:\s*(\S+)',out)
    ck('imports only KERNEL32 + USER32',dlls==['KERNEL32.dll','USER32.dll'],repr(dlls))
    ex=set()
    in_exports=False
    for line in out.splitlines():
        if line.startswith('Export Table:'): in_exports=True; continue
        if in_exports:
            m=re.search(r'\b(D3D11[A-Za-z0-9_]+)\s*$',line)
            if m: ex.add(m.group(1))
    ck('seven D3D11 exports unchanged',ex==EXPECTED_EXPORTS,repr(sorted(ex)))

    # Exact binary delta against field-working RC12.
    wo=rva_to_off(B,WIDTH_RVA); ho=rva_to_off(B,HEIGHT_RVA)
    ck('RC12 width sequence exact',B[wo:wo+len(WIDTH_OLD)]==WIDTH_OLD,B[wo:wo+len(WIDTH_OLD)].hex())
    ck('RC13 width /2 sequence exact',C[wo:wo+len(WIDTH_NEW)]==WIDTH_NEW,C[wo:wo+len(WIDTH_NEW)].hex())
    ck('RC12 height sequence exact',B[ho:ho+len(HEIGHT_OLD)]==HEIGHT_OLD,B[ho:ho+len(HEIGHT_OLD)].hex())
    ck('RC13 height /2 sequence exact',C[ho:ho+len(HEIGHT_NEW)]==HEIGHT_NEW,C[ho:ho+len(HEIGHT_NEW)].hex())
    diff={i for i,(a,b) in enumerate(zip(B,C)) if a!=b}
    checksum_range=set(range(0xd0,0xd4))
    width_range=set(range(wo,wo+len(WIDTH_NEW)))
    height_range=set(range(ho,ho+len(HEIGHT_NEW)))
    phrase_old=b'1/3 source dimensions'; phrase_new=b'1/2 source dimensions'
    po=B.find(phrase_old); pn=C.find(phrase_new)
    ck('RC12 1/3 log phrase exists once',po>=0 and B.count(phrase_old)==1,str(po))
    ck('RC13 1/2 log phrase exists once',pn>=0 and C.count(phrase_new)==1,str(pn))
    ck('runtime old 1/3 phrase absent',phrase_old not in C)
    phrase_range=set(range(po,po+len(phrase_old)))
    allowed=checksum_range|width_range|height_range|phrase_range
    ck('RC12->RC13 binary diff bounded to checksum + ME profile + log digit',diff<=allowed,repr(sorted(diff-allowed)[:20]))
    ck('binary delta is small',len(diff)==30,str(len(diff)))

    # Dimension model matches retained clamp/alignment code.
    ck('ME model 1280x720 = 640x368',(me_dim(1280),me_dim(720))==(640,368),repr((me_dim(1280),me_dim(720))))
    ck('ME model 1920x1080 = 960x544',(me_dim(1920),me_dim(1080))==(960,544),repr((me_dim(1920),me_dim(1080))))
    ck('ME model retains min64',me_dim(80)==64,str(me_dim(80)))

    # Existing confidence/fallback shader retained; config changes only its threshold.
    ck('existing luminance confidence shader retained',b'float trust=saturate(1.0-diff/max(Guard,.02));' in C)
    ck('BlendGuard config key retained in runtime','FrameGenerationBlendGuard'.encode('utf-16le') in C)

    ini=txt(ROOT/'win81_nis.ini')
    baseini=txt(ROOT/'_PTAR_UNINSTALL/reference/win81_nis.CADENCEFIX1_BASELINE.ini')
    ck('root INI equals installer baseline',ini==baseini)
    for name,pat in [
      ('FG starts OFF',r'(?m)^FrameGeneration=0$'),('BlendGuard 35',r'(?m)^FrameGenerationBlendGuard=35$'),
      ('ME profile identity 50',r'(?m)^FrameGenerationMEScalePercent=50$'),('ME budget 6000',r'(?m)^FrameGenerationMEBudgetUs=6000$'),
      ('adaptive deadline retained',r'(?m)^FrameGenerationAdaptiveDeadline=1$'),('FG target 60',r'(?m)^FrameGenerationTargetFPS=60$'),
      ('SharedFlush retained',r'(?m)^FrameGenerationSharedFlush=1$'),('recorder profile 3 retained',r'(?m)^VideoRecordProfile=3$')]:
        ck(name,re.search(pat,ini) is not None)
    ck('old Guard65 not shipped in active INI','FrameGenerationBlendGuard=65' not in ini)
    ck('old ME identity33 not shipped in active INI','FrameGenerationMEScalePercent=33' not in ini)

    # Package metadata and installer/verifier contracts.
    pid=txt(ROOT/'diag/PTAR_PACKAGE_ID.txt')
    for name,token in [
      ('RC13 package identity','RC13_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1_FGQUALITY1'),
      ('package runtime hash','RUNTIME_DLL_SHA256='+RUNTIME),('package RC12 base','BASE_RUNTIME_DLL_SHA256='+RC12),
      ('field machine GTX960M','FIELD_MACHINE=WINDOWS_8_1_X64_GTX_960M'),
      ('field quality finding','RC12_FG_FUNCTIONAL_30_TO_59_RUNTIME_FAILURES_ZERO_BUT_MOVING_CHARACTER_DEFORMATION'),
      ('quality ME profile','FG_QUALITY_ME_PROFILE=FIXED_DIV2_CLAMP64_ALIGN16'),('quality expected size','FG_QUALITY_ME_EXPECTED_1280X720=640X368'),
      ('quality guard','FG_QUALITY_BLEND_GUARD=35'),('quality SatGat scope note','FG_QUALITY_SATGAT=TOPOLOGY_AND_KEYED_PRIMARY_PRESERVED_ME_PROFILE_NOT_FIELD_VALIDATED_ON_SATGAT'),
      ('AutoVSync still deferred','FG_AUTO_VSYNC_IMPLEMENTED=NO')]: ck(name,token in pid)

    core=txt(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat')
    for name,token in [
      ('core runtime hash','echo DLL_SHA256='+RUNTIME),('core default guard35','set "KEEP_FG_BLEND_GUARD=35"'),('core default ME50','set "KEEP_FG_ME_SCALE=50"'),
      ('core forces Guard35 after merge','  set "KEEP_FG_BLEND_GUARD=35"'),('core forces ME50 after merge','  set "KEEP_FG_ME_SCALE=50"'),('core budget6000','set "KEEP_FG_ME_BUDGET=6000"'),
      ('core ME version marker','FRAME_GENERATION_ME_PROFILE=DIV2_FGQUALITY1_1280X720_640X368'),('core quality profile marker','FG_QUALITY_PROFILE=INQUISITOR_FGQUALITY1_ME_DIV2_GUARD35'),
      ('core expected ME marker','FG_QUALITY_ME_EXPECTED_1280X720=640X368'),('core Guard35 marker','FG_QUALITY_BLEND_GUARD=35_PERCENT_LUMA_DISAGREEMENT'),
      ('core GATE1 retained','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING'),('core SHARELEGACY3 retained','FG_SHARED_TRANSPORT=KEYED_PRIMARY_LEGACY_SHARED_END_TO_END_FALLBACK'),
      ('core RESIZEGUARD1 retained','FG_RESIZE_GUARD=RESIZEGUARD1_LEGACY_ACTIVE_AND_SCHEDULER_ONLY'),('core keyed policy retained','FG_RESIZE_GUARD_KEYED_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH'),
      ('core FG-OFF resize policy retained','FG_RESIZE_GUARD_OFF_POLICY=ORIGINAL_RESIZEBUFFERS_PASS_THROUGH'),('core AutoVSync deferred','FG_AUTO_VSYNC=NOT_IMPLEMENTED_RC13_FUTURE_CONTRACT_FG_ONLY')]: ck(name,token in core)

    ver=txt(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat')
    ck('verifier runtime hash',f'set "EXPECTED_DLL={RUNTIME}"' in ver)
    for name,token in [('verifier RC13 identity','RC13 INPUTMAP2 FGREAL30 GATE1 SHARELEGACY3 RESIZEGUARD1 FGQUALITY1'),
                       ('verifier ME marker','FRAME_GENERATION_ME_PROFILE=DIV2_FGQUALITY1_1280X720_640X368'),
                       ('verifier quality marker','FG_QUALITY_PROFILE=INQUISITOR_FGQUALITY1_ME_DIV2_GUARD35'),
                       ('verifier Guard35 INI','FrameGenerationBlendGuard=35'),('verifier ME50 INI','FrameGenerationMEScalePercent=50'),
                       ('verifier MEBudget6000 INI','FrameGenerationMEBudgetUs=6000'),('verifier Adaptive1 INI','FrameGenerationAdaptiveDeadline=1')]: ck(name,token in ver)
    ck('verifier contains no control chars',not any(ord(ch)<32 and ch not in '\n\t' for ch in ver))

    col=txt(ROOT/'diag/05-COLLECT_RESULTS.bat')
    ck('collector RC13 identity','RC13_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1_FGQUALITY1_COLLECT=PASS' in col)
    ck('collector GTX960M marker','FIELD_MACHINE_EXPECTED=GTX_960M_WINDOWS_8_1_FOR_RC13_FGQUALITY1_TEST' in col)
    ck('collector asks ME640 quality evidence','ME SIZE (attendu 640x368)' in col and 'deformation personnage' in col)

    idx=txt(ROOT/'diag/r/INDEX.txt')
    ck('RC12 r065 indexed',f'r065.dll' in idx and RC12 in idx and 'field-working Inquisitor FG' in idx)
    proto=txt(ROOT/'diag/FGQUALITY1_HARDWARE_PROTOCOL.txt')
    ck('hardware protocol GTX960M','GTX 960M' in proto and 'Windows 8.1' in proto)
    ck('hardware protocol requires ME640','ME SIZE must be 640x368' in proto)
    ck('hardware protocol keeps recorder off','Do NOT test CTRL+F9 recorder/QSV' in proto)
    ck('hardware protocol does not claim fix','Primary success criterion' in proto and 'FAIL / STOP CONDITIONS' in proto)
    finding=txt(ROOT/'diag/RC12_FIELD_FINDING_FG_WORKS_QUALITY_ARTIFACT.txt')
    ck('field finding records 30/59/29/29','SOURCE FPS 30, DISPLAY FPS 59, GENERATED FPS 29, REAL DISPLAY FPS 29' in finding)
    ck('field finding records RC12 ME432','ME size 432x240' in finding)
    ck('field finding records runtime failures zero','Runtime failures 0' in finding)

    # Patch script must deterministically reconstruct packaged DLL from exact RC12 reference.
    patch=ROOT/'diag/source_hotfix/patch_fgquality1_rc12.py'
    ck('patch script exists',patch.is_file())
    with tempfile.TemporaryDirectory() as td:
        a=Path(td)/'a.dll'; b=Path(td)/'b.dll'
        subprocess.check_call([sys.executable,str(patch),str(base),str(a)],stdout=subprocess.DEVNULL)
        subprocess.check_call([sys.executable,str(patch),str(base),str(b)],stdout=subprocess.DEVNULL)
        ck('fresh patch run A exact',sha(a)==RUNTIME,sha(a))
        ck('fresh patch run B exact',sha(b)==RUNTIME,sha(b))
        ck('two fresh patch runs bit-identical',a.read_bytes()==b.read_bytes())
        ck('fresh patch equals packaged runtime',a.read_bytes()==C)

    build=txt(ROOT/'diag/FGQUALITY1_BUILD.txt')
    ck('build record base hash','BASE_SHA256='+RC12 in build)
    ck('build record output hash','OUTPUT_SHA256='+RUNTIME in build)
    ck('build record ME profile','ME_PROFILE=CEIL_SOURCE_DIV2_CLAMP64_ALIGN16' in build and 'EXPECTED_ME_1280X720=640X368' in build)

    # Non-destructive delivery paths preserved.
    destructive=re.compile(r'(?im)(^|[^A-Za-z])(del|erase|rmdir|rd)\s|Remove-Item')
    for rr in ['01-INSTALL_FULLSTACK1.bat','tools/installer/PTAR_CORE_INSTALL.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat']:
        ck('no destructive command '+rr,destructive.search(txt(ROOT/rr)) is None)
    ck('uninstaller unchanged from locked contract',sha(ROOT/'05-DESINSTALLER_PTAR.bat')=='4138f33c3edea77d9ce0c78fb931aa79ed147b2f07efbee45ab2d3584b7e050f')
    ck('safe uninstall unchanged from locked contract',sha(ROOT/'_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1')=='4145d093d824f05edd315fe091053a8dfe7ee30a250db923964c17b7a84f7527')

    # Integrity metadata.
    files=[p for p in ROOT.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc']
    mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt'; entries={}
    for line in txt(mf).splitlines():
        if line.strip(): h,p=line.split('  ',1); entries[p]=h
    expected_paths=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
    ck('manifest paths exact',sorted(entries)==expected_paths,f'{len(entries)}/{len(expected_paths)}')
    ck('manifest hashes exact',not [p for p,h in entries.items() if sha(ROOT/p)!=h])

    own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'; lines=txt(own).splitlines()
    ck('ownership namespace',lines[0]=='PTAR_OWNER_NAMESPACE\t8101',lines[0])
    ck('ownership build id',lines[1]=='PTAR_BUILD_ID\t'+BUILD_ID,lines[1])
    rows=[]
    for line in lines[2:]:
        if line.strip(): ident,p,h=line.split('|',2); rows.append((int(ident),p,h))
    expected_own=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
    ck('ownership paths exact',sorted(p for _,p,_ in rows)==expected_own,f'{len(rows)}/{len(expected_own)}')
    ck('ownership IDs unique',len({i for i,_,_ in rows})==len(rows))
    ck('ownership hashes exact',not [p for _,p,h in rows if sha(ROOT/p)!=h])
    dirs=txt(ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv').splitlines()
    ck('static dirs build id',dirs[1]=='PTAR_BUILD_ID\t'+BUILD_ID,dirs[1])

    print(f'RC13 FGQUALITY1 PACKAGE STATIC VALIDATION: PASS {len(checks)}/{len(checks)}')
    for n in checks: print('[PASS]',n)

if __name__=='__main__': main()
