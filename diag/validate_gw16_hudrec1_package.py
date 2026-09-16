#!/usr/bin/env python3
"""Final package gate for PTAR SAFEPOINT11/FUSEDDETAIL1 HUDREC1."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve();D=ROOT/'diag';PAY=ROOT/'payload';U=ROOT/'_PTAR_UNINSTALL'
RT='e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429';BASE='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c';INI='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';VER='7ea6a7a47b463d780aeb752c7a8b9c1ea74db61336f8a44f6e1799fd8439d2fc';PKG='PTAR_GW16H_UNIFIEDREC3_SAFEPOINT11_FUSEDDETAIL1_HUDREC1'
checks=[]
def ck(n,c,d=''):checks.append((n,bool(c),str(d)));print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def sums(p):
 out={}
 for line in p.read_text(errors='replace').splitlines():
  if not line.strip():continue
  m=re.match(r'^([0-9a-f]{64})  (.+)$',line)
  if not m:raise ValueError('bad sums '+line)
  out[m.group(2)]=m.group(1)
 return out
def owners(p):
 lines=p.read_text(errors='replace').splitlines();assert lines and lines[0]=='PTAR_STATIC_OWNERSHIP_SCHEMA\t3';out={};ids=[]
 for line in lines[1:]:
  if not line.strip():continue
  a=line.split('|');assert len(a)==3 and re.fullmatch(r'\d{8}',a[0]) and re.fullmatch(r'[0-9a-f]{64}',a[2]);out[a[1]]=a[2];ids.append(int(a[0]))
 return out,ids
required=['01-INSTALL_GW16.bat','02-VERIFY_INSTALL.bat','04-COLLECT_RESULTS.bat','README.md','README_TEST.txt','LICENSE','payload/d3d11.dll','payload/win81_nis_dx11_x64.dll','payload/win81_nis.ini','payload/win81_nis_version.txt','diag/base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll','diag/patch_gw16h_recorder_hudrec1.py','diag/validate_recorder_hudrec1.py','diag/GW16H_HUDREC1_FINDING.txt','diag/FG_MARKER_VISIBILITY.bat','diag/set_vblank_diagnostics.ps1','diag/validate_fg_marker_visibility.py','diag/FG_MARKER_VISIBILITY_VALIDATION.txt','diag/BUILD_MANIFEST.json','diag/SHA256SUMS.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv','_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1']
for r in required:ck('required '+r,(ROOT/r).is_file())
if any(not (ROOT/r).is_file() for r in required):raise SystemExit(1)
ck('runtime exact HUDREC1',sha(PAY/'d3d11.dll')==RT,sha(PAY/'d3d11.dll'));ck('runtime mirror exact HUDREC1',sha(PAY/'win81_nis_dx11_x64.dll')==RT,sha(PAY/'win81_nis_dx11_x64.dll'));ck('runtime mirrors exact',(PAY/'d3d11.dll').read_bytes()==(PAY/'win81_nis_dx11_x64.dll').read_bytes());ck('INI exact canonical',sha(PAY/'win81_nis.ini')==INI,sha(PAY/'win81_nis.ini'));ck('version exact HUDREC1',sha(PAY/'win81_nis_version.txt')==VER,sha(PAY/'win81_nis_version.txt'));ck('bundled canonical SAFEPOINT11 exact',sha(D/'base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll')==BASE,sha(D/'base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll'))
b=(D/'base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll').read_bytes();n=(PAY/'d3d11.dll').read_bytes();diff=[i for i,(x,y) in enumerate(zip(b,n)) if x!=y]
ck('whole binary same size',len(b)==len(n),(len(b),len(n)));ck('exact 3-byte delta',diff==[0xD0,0xD1,0x4CBBC],','.join(hex(x) for x in diff));ck('single functional byte outside checksum',[x for x in diff if x not in (0xD0,0xD1)]==[0x4CBBC])
install=(D/'install.ps1').read_text(errors='replace');verify=(D/'verify.ps1').read_text(errors='replace');collect=(D/'collect.ps1').read_text(errors='replace');marker=(D/'set_vblank_diagnostics.ps1').read_text(errors='replace')
for label,text in [('installer',install),('verifier',verify)]:
 for h in [RT,INI,VER]:ck(label+' hash lock '+h[:8],h in text)
ck('installer accepts canonical SAFEPOINT11 predecessor',BASE in install);ck('installer package id',"package='GW16H_UNIFIEDREC3_SAFEPOINT11_FUSEDDETAIL1_HUDREC1'" in install)
ck('collector HUDREC finding','GW16H_HUDREC1_FINDING.txt' in collect);ck('collector HUDREC validation','RECORDER_HUDREC1_VALIDATION.txt' in collect);ck('collector unique ZIP no Remove-Item','Remove-Item' not in collect)
ck('marker helper only VBlankDiagnostics key','VBlankDiagnostics' in marker and 'Overlay=' not in marker);ck('marker helper backup before write','Copy-Item' in marker and 'backups' in marker);ck('marker helper no Remove-Item','Remove-Item' not in marker)
ver=(PAY/'win81_nis_version.txt').read_text(errors='replace');readme=(ROOT/'README.md').read_text(errors='replace');test=(ROOT/'README_TEST.txt').read_text(errors='replace')
for tok in ['GW16H_HUDREC1=RECORDER_CAPTURES_VISIBLE_PRESENTER_HUD','RECORDER_HUD_FPS=INCLUDED_WHEN_OVERLAY_ENABLED','RECORDER_FG_ON_FEED=UNCHANGED_ISOLATED_FINAL_BACKBUFFER','FG_VISIBLE_MARKER_CONTROL=VBlankDiagnostics_DIAG_MENU']:ck('version '+tok,tok in ver)
for tok in ['video recording with PTAR HUD / FPS','FG cadence marker (blinking squares)','VBlankDiagnostics=0/1']:ck('README '+tok,tok.lower() in readme.lower())
ck('README_TEST targeted two-clip HUDREC hardware gate','GATE MATERIEL HUDREC1' in test and 'CLIP A - FG OFF' in test and 'CLIP B - FG ON' in test and 'SAFEPOINT11/FUSEDDETAIL1 is already hardware validated' in test)
try:
 m=json.loads((D/'BUILD_MANIFEST.json').read_text());hr=m['runtime']['recorder_hudrec1'];ins=m['installer_fix'];hw=m['hardware_state'];val=m['validation']
 ck('manifest package',m['package']==PKG,m['package']);ck('manifest filename',m['package_filename']==PKG+'.zip');ck('manifest runtime',m['runtime']['sha256']==RT,m['runtime']['sha256']);ck('manifest HUDREC base',hr['base_sha256']==BASE);ck('manifest functional bytes',hr['functional_byte_changes']==1);ck('manifest whole delta',hr['whole_binary_delta_count']==3);ck('manifest FG algorithm unchanged',hr['fg_algorithm_changed'] is False and hr['fuseddetail1_changed'] is False);ck('manifest QSV unchanged',hr['qsv_conversion_changed'] is False);ck('manifest hardware gate targeted',all(tok in hw['required_targeted_gates'][0] for tok in ['Clip A','FG OFF','Clip B','FG ON','no broad regression suite']));ck('manifest payload locks',ins['expected_runtime_sha256']==RT and ins['expected_ini_sha256']==INI and ins['expected_version_sha256']==VER);ck('manifest marker validation',val.get('fg_marker_visibility')=='17/17 PASS')
except Exception as e:ck('manifest parse',False,e)
execs=[('validate_recorder_hudrec1.py','RECORDER_HUDREC1_VALIDATION='),('validate_win81_path_handoff.py','WIN81_PATH_HANDOFF_VALIDATION=29/29 PASS'),('validate_ps4_compat.py','PS4_COMPAT_VALIDATION=31/31 PASS'),('validate_auto_wrapper_build.py','AUTO_WRAPPER_BUILD_VALIDATION=26/26 PASS'),('validate_fg_marker_visibility.py','FG_MARKER_VISIBILITY_VALIDATION=17/17 PASS')]
for script,markerout in execs:
 cp=subprocess.run([sys.executable,str(D/script),str(ROOT)],capture_output=True,text=True);ok=cp.returncode==0 and markerout in cp.stdout and 'FAIL' not in cp.stdout.splitlines()[-1];ck('execute '+script,ok,(cp.stdout+cp.stderr).splitlines()[-2:])
reports=[('FG_FUSEDDETAIL1_VALIDATION.txt','FG_FUSEDDETAIL1_VALIDATION=70/70 PASS'),('UNIFIED_RECORDER_VALIDATION.txt','UNIFIED_RECORDER_VALIDATION=57/57 PASS'),('RECORDER_QSV_PRIORITY_VALIDATION.txt','RECORDER_QSV_PRIORITY_VALIDATION=45/45 PASS'),('RECORDER_NATIVE_STATE_GUARD_VALIDATION.txt','RECORDER_NATIVE_STATE_GUARD_VALIDATION=54/54 PASS'),('GW16_RUNTIME_VALIDATION.txt','GW16_RUNTIME_VALIDATION=74/74 PASS')]
for fn,mk in reports:ck('inherited canonical report '+fn,(D/fn).is_file() and mk in (D/fn).read_text(errors='replace'))
try:
 ss=sums(D/'SHA256SUMS.txt');oo,ids=owners(U/'PTAR_STATIC_OWNERSHIP.tsv');allfiles=[]
 for q in ROOT.rglob('*'):
  if q.is_file():
   rel=q.relative_to(ROOT).as_posix()
   if rel.startswith('_PTAR_UNINSTALL/state/') or rel.startswith('diag/backups/'):continue
   allfiles.append(rel)
 aset=set(allfiles);sexp=aset-{'diag/SHA256SUMS.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'};oexp=aset-{'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'}
 ck('SHA registry coverage',set(ss)==sexp,(len(ss),len(sexp)));ck('ownership coverage',set(oo)==oexp,(len(oo),len(oexp)));ck('ownership IDs contiguous',ids==list(range(82600001,82600001+len(ids))),len(ids));bad=[r for r,h in ss.items() if sha(ROOT/r)!=h];ck('SHA registry hashes exact',not bad,','.join(bad[:4]));bad=[r for r,h in oo.items() if sha(ROOT/r)!=h];ck('ownership hashes exact',not bad,','.join(bad[:4]));ck('ownership hashes SHA registry',oo.get('diag/SHA256SUMS.txt')==sha(D/'SHA256SUMS.txt'))
except Exception as e:ck('registry parse',False,e)
for pat in ['__pycache__','*.pyc','*.pyo','*disasm*','*.bak','hudrec1_candidate*.dll']:
 found=[q for q in ROOT.rglob(pat) if q.is_file() or q.is_dir()];ck('no work product '+pat,not found,','.join(str(x.relative_to(ROOT)) for x in found[:3]))
failed=[x for x in checks if not x[1]];print('GW16_HUDREC1_PACKAGE_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
 for n,_,d in failed:print('FAILED',n,d)
 raise SystemExit(1)
