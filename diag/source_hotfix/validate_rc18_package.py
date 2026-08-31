#!/usr/bin/env python3
import hashlib, re, subprocess, sys
from pathlib import Path

ROOT=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
RUNTIME='2b6fafabeedc89857dbb6d5a318ca143ce8e30ec912050116f70a4a58ad55ad3'
BASE='5bdff6f5ecce928cb62f5dbbfe0cd3562eff455054620223cc4ba268c12c92fc'
VISIBLE='67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305'
BUILD_ID='2026083062'
NAMESPACE='8103'
checks=[]
def ck(n,c,d=''):
    ok=bool(c); checks.append(ok); print(('[PASS] ' if ok else '[FAIL] ')+n+((' :: '+d) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def txt(rel): return (ROOT/rel).read_text(encoding='utf-8',errors='replace')
def rel(p): return p.relative_to(ROOT).as_posix()

required=[
'01-INSTALL_FULLSTACK1.bat','VERIFY_FULLSTACK1_INSTALL.bat','05-DESINSTALLER_PTAR.bat',
'win81_nis.ini','win81_nis_dx11_x64.dll','diag/PTAR_PACKAGE_ID.txt','diag/PTAR_README.txt',
'diag/PRODUCTION_VALIDATION.txt','diag/RC18_FIELD_FINDING_VISIBLE_DELIVERY.txt',
'diag/PRESENTDELIVERY1_BUILD.txt','diag/PRESENTDELIVERY1_DETERMINISM.txt','diag/PRESENTDELIVERY1_HARDWARE_PROTOCOL.txt',
'diag/source_hotfix/patch_presentdelivery1_rc17.py','diag/source_hotfix/validate_presentdelivery1_rc18.py',
'diag/r/r070.dll','diag/visible_verifier/win81_vblank2_visible_marker_x64.exe',
'_PTAR_UNINSTALL/reference/win81_nis.CADENCEFIX1_BASELINE.ini','tools/installer/PTAR_CORE_INSTALL.bat'
]
for r in required: ck('asset '+r,(ROOT/r).is_file())
ck('runtime hash',sha(ROOT/'win81_nis_dx11_x64.dll')==RUNTIME)
ck('RC17 base ref hash',sha(ROOT/'diag/r/r070.dll')==BASE)
ck('visible verifier hash unchanged',sha(ROOT/'diag/visible_verifier/win81_vblank2_visible_marker_x64.exe')==VISIBLE)

# Current deterministic binary validation must pass on the actual package tree.
p=subprocess.run([sys.executable,str(ROOT/'diag/source_hotfix/validate_presentdelivery1_rc18.py'),str(ROOT)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
ck('PRESENTDELIVERY1 validator exits 0',p.returncode==0,p.stdout.strip().splitlines()[-1] if p.stdout.strip() else '')

ini=txt('win81_nis.ini'); refini=txt('_PTAR_UNINSTALL/reference/win81_nis.CADENCEFIX1_BASELINE.ini')
for label,s in [('root INI',ini),('reference INI',refini)]:
    for token in ['FrameGeneration=0','FrameGenerationRequireVSync=0','FrameGenerationPresentSync=1','FrameGenerationTargetFPS=60','FrameGenerationSharedFlush=1','VideoRecordProfile=3']:
        ck(label+' '+token,token in s)

core=txt('tools/installer/PTAR_CORE_INSTALL.bat'); ver=txt('VERIFY_FULLSTACK1_INSTALL.bat'); wrapper=txt('01-INSTALL_FULLSTACK1.bat'); pid=txt('diag/PTAR_PACKAGE_ID.txt')
for token in [RUNTIME,'KEEP_FG_REQUIRE_VSYNC=0','KEEP_FG_PRESENT_SYNC=1','FrameGenerationPresentSync=1','FG_VISIBLE_DELIVERY=PRESENTDELIVERY1_PREWAIT_REMOVED_SYNC1','FRAME_GENERATION_PRESENT=SYNC1_FLAGS0_ISOLATED_DISPLAY_THREAD','FG_PRE_PRESENT_WAITFORVBLANK=DISABLED_BY_RC18_PATCH']:
    ck('core contract '+token,token in core)
for token in [RUNTIME,'FrameGenerationRequireVSync=0','FrameGenerationPresentSync=1','FG_VISIBLE_DELIVERY=PRESENTDELIVERY1_PREWAIT_REMOVED_SYNC1','FRAME_GENERATION_PRESENT=SYNC1_FLAGS0_ISOLATED_DISPLAY_THREAD','FG_PRE_PRESENT_WAITFORVBLANK=DISABLED_BY_RC18_PATCH']:
    ck('verify contract '+token,token in ver)
ck('wrapper names RC18 PRESENTDELIVERY1','RC18 PRESENTDELIVERY1' in wrapper)
for token in ['RC18_PRESENTATION_POLICY=PRESENTDELIVERY1_SYNC1_NO_PREWAIT','VISIBLE_VERIFIER_SHA256='+VISIBLE,'RECORDER_INSTALL_POLICY=RECORDERFIX1_FORCE_PRODUCTION_PROFILE_3_ON_EVERY_INSTALL']:
    ck('package ID '+token,token in pid)

# Current scripts must remain Windows 8.1 batch-safe: ASCII, CRLF, no BOM.
for pth in sorted(ROOT.rglob('*.bat')):
    b=pth.read_bytes()
    ck('batch ASCII '+rel(pth),not any(x>=128 for x in b))
    ck('batch CRLF '+rel(pth),all(i>0 and b[i-1]==13 for i,x in enumerate(b) if x==10))
    ck('batch no BOM '+rel(pth),not b.startswith(b'\xef\xbb\xbf'))

# No new destructive broad-delete command in public install/verify/collect paths.
for r in ['01-INSTALL_FULLSTACK1.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat','tools/installer/PTAR_CORE_INSTALL.bat']:
    s=txt(r)
    bad=bool(re.search(r'(?im)^\s*(del|erase|rd\s+/s|rmdir\s+/s)\b',s))
    ck(r+' no broad destructive command',not bad)

# Manifest/ownership validation is enabled after regeneration.
mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt'; own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'; dirs=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv'
if mf.is_file() and own.is_file() and dirs.is_file():
    entries={}
    for line in mf.read_text(encoding='utf-8').splitlines():
        if not line.strip(): continue
        h,r=line.split('  ',1); entries[r]=h
    files=[p for p in ROOT.rglob('*') if p.is_file()]
    expected=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
    ck('manifest path set exact',sorted(entries)==expected,f'{len(entries)}/{len(expected)}')
    ck('manifest hashes exact',all(sha(ROOT/r)==h for r,h in entries.items()))
    lines=own.read_text(encoding='utf-8').splitlines()
    ck('ownership namespace',lines and lines[0]=='PTAR_OWNER_NAMESPACE\t'+NAMESPACE)
    ck('ownership build id',len(lines)>1 and lines[1]=='PTAR_BUILD_ID\t'+BUILD_ID)
    rows=[]
    for line in lines[2:]:
        if re.match(r'^\d+\|',line):
            a=line.split('|'); rows.append((a[0],a[1],a[2]))
    expected_own=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
    ck('ownership path set exact',sorted(r for _,r,_ in rows)==expected_own,f'{len(rows)}/{len(expected_own)}')
    ck('ownership ids unique',len({i for i,_,_ in rows})==len(rows))
    ck('ownership hashes exact',all(sha(ROOT/r)==h for _,r,h in rows))
    dlines=dirs.read_text(encoding='utf-8').splitlines()
    ck('dirs namespace',dlines and dlines[0]=='PTAR_OWNER_NAMESPACE\t'+NAMESPACE)
    ck('dirs build id',len(dlines)>1 and dlines[1]=='PTAR_BUILD_ID\t'+BUILD_ID)

passed=sum(checks); total=len(checks)
print(f'RC18 PACKAGE VALIDATION: {"PASS" if passed==total else "FAIL"} {passed}/{total}')
if passed!=total: sys.exit(1)
