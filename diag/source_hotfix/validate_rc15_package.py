#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,subprocess,sys
ROOT=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]
RC15='2c978a59cc7053edc6669de18e0dcf2ae5755b9b96da74c452c9b19405c1d785';RC14='3808fcb1681f00342b7ce07e39aebc2b1cabbed8aeb2fd244788744136d1868f';OBJ='ebb37d71a949c9c20208e362bb8ccf37555c25341bc6d37af1180494b2ecf523';BUILD='2026082926'
checks=[]
def ck(n,c,d=''):
 if not c:raise AssertionError(n+(' :: '+d if d else ''))
 checks.append(n)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def txt(p):return Path(p).read_text(encoding='utf-8',errors='strict').replace('\r\n','\n').replace('\r','\n')
def rel(p):return p.relative_to(ROOT).as_posix()
cur=ROOT/'win81_nis_dx11_x64.dll';base=ROOT/'diag/r/r067.dll'
ck('runtime hash RC15',sha(cur)==RC15,sha(cur));ck('embedded RC14 reference',sha(base)==RC14,sha(base));ck('HUD object exact',sha(ROOT/'diag/source_hotfix/fgquality_hud1.obj')==OBJ)
# Run the binary contract validator using the package's exact RC14 reference.
val=ROOT/'diag/source_hotfix/validate_fgqualityhud1_rc15.py';pr=subprocess.run([sys.executable,str(val),str(base),str(cur)],text=True,capture_output=True);ck('binary validator returns PASS',pr.returncode==0 and 'BINARY VALIDATION: PASS' in pr.stdout,pr.stdout+pr.stderr)
ini=txt(ROOT/'win81_nis.ini');baseini=txt(ROOT/'_PTAR_UNINSTALL/reference/win81_nis.CADENCEFIX1_BASELINE.ini');ck('root INI equals uninstall baseline',ini==baseini)
for n,p in [('quality profile exists',r'(?m)^FrameGenerationQuality=[0-3]$'),('FG starts OFF',r'(?m)^FrameGeneration=0$'),('recorder profile3',r'(?m)^VideoRecordProfile=3$'),('status remains F8',r'(?m)^Status=F8$')]:ck(n,re.search(p,ini) is not None)
ck('INI documents RC15 HUD','RC15 HUD: CTRL+F8 cycles FG quality and displays ACTIVE/SELECTED/PENDING' in ini)
pid=txt(ROOT/'diag/PTAR_PACKAGE_ID.txt')
for n,s in [('identity','RC15_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1_FGQUALITYHUD1'),('runtime metadata','RUNTIME_DLL_SHA256='+RC15),('base metadata','BASE_RUNTIME_DLL_SHA256='+RC14),('HUD marker','FG_QUALITY_HUD=FGQUALITYHUD1_ACTIVE_SELECTED_PENDING_NOTICE'),('notice id','FG_QUALITY_NOTICE_ID=8'),('notice line1','FG_QUALITY_NOTICE_LINE1=CURR_ACTIVE_NOW'),('notice line2','FG_QUALITY_NOTICE_LINE2=SEL_SELECTED_P_PENDING'),('active tracking','FG_QUALITY_ACTIVE_TRACKING=LAST_FULLY_APPLIED_PROFILE'),('GATE1 retained','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING'),('AutoVSync deferred','FG_AUTO_VSYNC_IMPLEMENTED=NO'),('recorder unchanged','RECORDER_CHANGED=NO')]:ck(n,s in pid)
core=txt(ROOT/'tools/installer/PTAR_CORE_INSTALL.bat')
for n,s in [('installer hash', 'echo DLL_SHA256='+RC15),('HUD marker write','FG_QUALITY_HUD=FGQUALITYHUD1_ACTIVE_SELECTED_PENDING_NOTICE'),('notice id write','FG_QUALITY_NOTICE_ID=8'),('profile preservation','KEEP_FG_QUALITY=2'),('quality input parse','FrameGenerationQuality=[0-3]'),('no auto rebuild','FG_QUALITY_CROSS_TIER_POLICY=NO_AUTOMATIC_FG_REBUILD_CTRL_F6_OFF_ON_REQUIRED_WHILE_ACTIVE'),('GATE1 marker','FG_REAL_SOURCE_GATE=RUNTIME_ENABLED_AND_SCHEDULER_RUNNING'),('AutoVSync deferred core','FG_AUTO_VSYNC=NOT_IMPLEMENTED_RC15_FUTURE_CONTRACT_FG_ONLY')]:ck(n,s in core)
ver=txt(ROOT/'VERIFY_FULLSTACK1_INSTALL.bat');ck('verifier hash','set "EXPECTED_DLL='+RC15+'"' in ver);ck('verifier HUD marker','FG_QUALITY_HUD=FGQUALITYHUD1_ACTIVE_SELECTED_PENDING_NOTICE' in ver);ck('verifier notice id','FG_QUALITY_NOTICE_ID=8' in ver)
col=txt(ROOT/'diag/05-COLLECT_RESULTS.bat');ck('collector RC15 identity','RC15_INPUTMAP2_FGREAL30_GATE1_SHARELEGACY3_RESIZEGUARD1_FGQUALITYHUD1_COLLECT=PASS' in col);ck('collector GTX960M','FIELD_MACHINE_EXPECTED=GTX_960M_WINDOWS_8_1_FOR_RC15_FGQUALITYHUD1_TEST' in col)
proto=txt(ROOT/'diag/FGQUALITYHUD1_HARDWARE_PROTOCOL.txt');ck('protocol same-tier notice','CURR 3 NOW' in proto and 'SEL  3 P0' in proto);ck('protocol cross-tier notice','SEL  0 P1' in proto);ck('protocol no recorder','Do not test recorder' in proto)
# New source assets and reproducibility metadata exist.
for p in ['diag/source_hotfix/fgquality_hud1.s','diag/source_hotfix/fgquality_hud1.obj','diag/source_hotfix/patch_fgqualityhud1_rc14.py','diag/source_hotfix/validate_fgqualityhud1_rc15.py','diag/source_hotfix/FGQUALITYHUD1_BUILD.txt','diag/FGQUALITYHUD1_STATIC_VALIDATION.txt','diag/RC14_FIELD_FINDING_FGQUALITY_HUD_GAP.txt']:
 ck('asset '+p,(ROOT/p).is_file())
# Exact RC14 reference index.
idx=txt(ROOT/'diag/r/INDEX.txt');ck('r067 index', 'r067.dll' in idx and RC14 in idx)
# Non-destructive batch / installer rule.
destructive=re.compile(r'(?im)(^|[^A-Za-z])(del|erase|rmdir|rd)\s|Remove-Item')
for p in ['01-INSTALL_FULLSTACK1.bat','tools/installer/PTAR_CORE_INSTALL.bat','VERIFY_FULLSTACK1_INSTALL.bat','diag/05-COLLECT_RESULTS.bat']:
 ck('no destructive command '+p,destructive.search(txt(ROOT/p)) is None)
# Integrity manifests when regenerated for RC15.
dirs=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv';own=ROOT/'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv';mf=ROOT/'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt'
if dirs.exists() and ('PTAR_BUILD_ID\t'+BUILD) in txt(dirs):
 entries={}
 for line in txt(mf).splitlines():
  if line.strip():h,p=line.split('  ',1);entries[p]=h
 files=[p for p in ROOT.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc']
 exp=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_MANIFEST_SHA256.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'})
 ck('manifest paths exact',sorted(entries)==exp,f'{len(entries)}/{len(exp)}');ck('manifest hashes exact',not [p for p,h in entries.items() if sha(ROOT/p)!=h])
 ol=txt(own).splitlines();ck('ownership namespace',ol[0]=='PTAR_OWNER_NAMESPACE\t8101');ck('ownership build',ol[1]=='PTAR_BUILD_ID\t'+BUILD)
 rows=[]
 for line in ol[2:]:
  if line.strip():ident,p,h=line.split('|',2);rows.append((int(ident),p,h))
 exo=sorted(rel(p) for p in files if rel(p) not in {'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','05-DESINSTALLER_PTAR.bat'})
 ck('ownership paths exact',sorted(p for _,p,_ in rows)==exo,f'{len(rows)}/{len(exo)}');ck('ownership ids unique',len({i for i,_,_ in rows})==len(rows));ck('ownership hashes exact',not [p for _,p,h in rows if sha(ROOT/p)!=h])
else: ck('integrity build ID present',False,'run RC15 integrity regeneration first')
print('RC15 FGQUALITYHUD1 PACKAGE VALIDATION: PASS %d/%d'%(len(checks),len(checks)))
for n in checks:print('[PASS]',n)
