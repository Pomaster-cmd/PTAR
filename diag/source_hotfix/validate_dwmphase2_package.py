#!/usr/bin/env python3
import hashlib, re, subprocess, sys
from pathlib import Path
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
RUNTIME='3d4d777c943ced0f475df1371d3a2f9eeb5eeb80c66e9fb217c4d91057f32453'
BASE='2b6fafabeedc89857dbb6d5a318ca143ce8e30ec912050116f70a4a58ad55ad3'
VISIBLE='67b163bf3203366562f066c10ac971992b5846991f19c68f61cbd975f9ef8305'
BUILD_ID='2026083102'; NAMESPACE='8104'; checks=[]
def ck(name,cond,detail=''):
    ok=bool(cond); checks.append(ok); print(('[PASS] ' if ok else '[FAIL] ')+name+((' :: '+detail) if detail else ''))
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def txt(rel): return (ROOT/rel).read_text(encoding='utf-8',errors='replace')
def rel(path): return path.relative_to(ROOT).as_posix()
required=[
 '01-INSTALL_FULLSTACK1.bat','VERIFY_FULLSTACK1_INSTALL.bat','05-DESINSTALLER_PTAR.bat','win81_nis.ini','win81_nis_dx11_x64.dll',
 'diag/PTAR_PACKAGE_ID.txt','diag/PTAR_VERIFY_FULLSTACK1.ps1','diag/DWMPHASE2_AUTOCOLLECT1_HARDWARE_PROTOCOL.txt','diag/DWMPHASE1_COUNTER_MAP.txt',
 'diag/RC18_DWMPHASE2_AUTOCOLLECT1_FIELD_BASIS.txt','diag/source_hotfix/dwmphase2_rc18.s','diag/source_hotfix/dwmphase2_rc18.obj',
 'diag/source_hotfix/patch_dwmphase2_rc18.py','diag/source_hotfix/validate_dwmphase2_rc18lab.py','diag/source_hotfix/validate_dwmphase2_package.py',
 'diag/r/r071.dll','diag/visible_verifier/win81_vblank2_visible_marker_x64.exe','_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1',
 '_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv','_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv',
 '_PTAR_UNINSTALL/reference/win81_nis.CADENCEFIX1_BASELINE.ini','tools/installer/PTAR_CORE_INSTALL.bat']
for item in required: ck('asset '+item,(ROOT/item).is_file())
if (ROOT/'win81_nis_dx11_x64.dll').is_file(): ck('runtime hash',sha(ROOT/'win81_nis_dx11_x64.dll')==RUNTIME)
if (ROOT/'diag/r/r071.dll').is_file(): ck('RC18 base reference hash',sha(ROOT/'diag/r/r071.dll')==BASE)
if (ROOT/'diag/visible_verifier/win81_vblank2_visible_marker_x64.exe').is_file(): ck('visible verifier unchanged',sha(ROOT/'diag/visible_verifier/win81_vblank2_visible_marker_x64.exe')==VISIBLE)
validator=ROOT/'diag/source_hotfix/validate_dwmphase2_rc18lab.py'
if validator.is_file():
    p=subprocess.run([sys.executable,str(validator),str(ROOT)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    last=p.stdout.strip().splitlines()[-1] if p.stdout.strip() else ''
    ck('DWMPHASE2 runtime validator exits 0',p.returncode==0,last)
ini=txt('win81_nis.ini') if (ROOT/'win81_nis.ini').is_file() else ''
for token in ['FrameGeneration=0','FrameGenerationRequireVSync=0','FrameGenerationPresentSync=1','FrameGenerationTargetFPS=60','FrameGenerationSharedFlush=1','VideoRecordProfile=3']: ck('root INI '+token,token in ini)
core=txt('tools/installer/PTAR_CORE_INSTALL.bat'); verifyps=txt('diag/PTAR_VERIFY_FULLSTACK1.ps1'); verifybat=txt('VERIFY_FULLSTACK1_INSTALL.bat'); pid=txt('diag/PTAR_PACKAGE_ID.txt'); protocol=txt('diag/DWMPHASE2_AUTOCOLLECT1_HARDWARE_PROTOCOL.txt'); maptxt=txt('diag/DWMPHASE1_COUNTER_MAP.txt'); arm=txt('diag/04-ARM_VISIBLE_FRAME_VERIFIER.bat'); collect=txt('diag/05-COLLECT_RESULTS.bat')
markers=[RUNTIME,'FG_VISIBLE_DELIVERY=PRESENTDELIVERY1_PREWAIT_REMOVED_SYNC1','FG_DWM_PHASE_DIAG=DWMPHASE2_SAMPLE_EVERY_8_G_AND_R_AUTOCOLLECT1','FG_DWM_PHASE_HOTKEY=CTRL_F4_SNAPSHOT_HUD_DWM_LAB','FG_DWM_PHASE_AUTODUMP=F8_STATUS_EDGE_TO_LOG','FG_VISIBLE_RESULT_CAPTURE=PTAR_VISIBLE_VERIFIER_LAST_OUTPUT_TXT','FG_VISIBLE_TEST_HUD_HOTKEY=CTRL_F5_HUD_VISIB_TEST','DWM_TIMING_INFO_SIZE=292_PACK4','DWM_TIMING_HWND=NULL_WINDOWS_8_1','FG_PRESENT_CALL=IDXGISWAPCHAIN_PRESENT_SYNCINTERVAL_1_FLAGS_0']
for token in markers: ck('core contract '+token,token in core)
for token in markers: ck('verify contract '+token,token in verifyps)
ck('VERIFYFIX5 uses PSScriptRoot','$PSScriptRoot' in verifyps); ck('VERIFYFIX5 has no GetFullPath','GetFullPath' not in verifyps); ck('VERIFY launcher does not pass package root','-PackageRoot' not in verifybat and '-Root' not in verifybat)
for token in ['PACKAGE=PTAR_DISPLAY_DELIVERY_LAB_DWMPHASE2_AUTOCOLLECT1_RC18_BASE_VERIFYFIX5','BASE_RUNTIME=RC18_PRESENTDELIVERY1_FIELD_BASE','DWM_PHASE_AUTODUMP=F8_STATUS_EDGE_TO_LOG','VISIBLE_RESULT_CAPTURE=PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt','INSTALL_VERIFY=VERIFYFIX5_PSSCRIPTROOT_NO_GETFULLPATH']: ck('package ID '+token,token in pid)
for token in ['F8','CTRL+F4','F5','DWM LAB','VISIB TEST','PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt','RC18']: ck('hardware protocol mentions '+token,token in protocol)
for token in ['GS0','GE0','RS0','RE0','CP0','2.08']: ck('counter map mentions '+token,token in maptxt)
ck('visible verifier stdout redirected to result file','"%VERIFIER%" >"%OUTPUT%" 2>&1' in arm)
ck('visible verifier output status saved','VERIFIER_OUTPUT_SAVED=YES' in arm)
ck('collector copies visible verifier output','PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt' in collect)
for pth in sorted(ROOT.rglob('*.bat')):
    b=pth.read_bytes(); ck('batch ASCII '+rel(pth),not any(x>=128 for x in b)); ck('batch CRLF '+rel(pth),all(i>0 and b[i-1]==13 for i,x in enumerate(b) if x==10)); ck('batch no BOM '+rel(pth),not b.startswith(b'\xef\xbb\xbf'))
for item in ['01-INSTALL_FULLSTACK1.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat','tools/installer/PTAR_CORE_INSTALL.bat']:
    s=txt(item); bad=bool(re.search(r'(?im)^\s*(del|erase|rd\s+/s|rmdir\s+/s)\b',s)); ck(item+' no broad destructive command',not bad)
mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt'; own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'; dirs=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv'
if mf.is_file() and own.is_file() and dirs.is_file():
    entries={}
    for line in mf.read_text(encoding='utf-8').splitlines():
        if not line.strip(): continue
        try: h,r=line.split('  ',1)
        except ValueError: continue
        entries[r]=h
    files=[p for p in ROOT.rglob('*') if p.is_file()]
    expected=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
    ck('manifest path set exact',sorted(entries)==expected,f'{len(entries)}/{len(expected)}'); ck('manifest hashes exact',all((ROOT/r).is_file() and sha(ROOT/r)==h for r,h in entries.items()))
    lines=own.read_text(encoding='utf-8').splitlines(); ck('ownership namespace',len(lines)>0 and lines[0]=='PTAR_OWNER_NAMESPACE\t'+NAMESPACE); ck('ownership build id',len(lines)>1 and lines[1]=='PTAR_BUILD_ID\t'+BUILD_ID)
    rows=[]
    for line in lines[2:]:
        if re.match(r'^\d+\|',line):
            a=line.split('|');
            if len(a)==3: rows.append((a[0],a[1],a[2]))
    expected_own=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
    ck('ownership path set exact',sorted(r for _,r,_ in rows)==expected_own,f'{len(rows)}/{len(expected_own)}'); ck('ownership ids unique',len({i for i,_,_ in rows})==len(rows)); ck('ownership hashes exact',all((ROOT/r).is_file() and sha(ROOT/r)==h for _,r,h in rows))
    dlines=dirs.read_text(encoding='utf-8').splitlines(); ck('dirs namespace',len(dlines)>0 and dlines[0]=='PTAR_OWNER_NAMESPACE\t'+NAMESPACE); ck('dirs build id',len(dlines)>1 and dlines[1]=='PTAR_BUILD_ID\t'+BUILD_ID)
passed=sum(checks); total=len(checks); print(f'DWMPHASE2 PACKAGE VALIDATION: {"PASS" if passed==total else "FAIL"} {passed}/{total}'); sys.exit(0 if passed==total else 1)
