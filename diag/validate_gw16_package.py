#!/usr/bin/env python3
"""Package gate for PTAR GW16H SAFEPOINT11/FUSEDDETAIL1."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve();D=ROOT/'diag';PAY=ROOT/'payload';U=ROOT/'_PTAR_UNINSTALL'
RT='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c';INI='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';VER='8c15a4ab74222f1efc7305315bf25dd06903f45bd568abdd3a04d152501e0c51';SP8='613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70';PKG='PTAR_GW16H_UNIFIEDREC3_SAFEPOINT11_FUSEDDETAIL1'
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
required=['01-INSTALL_GW16.bat','02-VERIFY_INSTALL.bat','04-COLLECT_RESULTS.bat','README_TEST.txt','payload/d3d11.dll','payload/win81_nis_dx11_x64.dll','payload/win81_nis.ini','payload/win81_nis_version.txt','diag/base/FUSEDDETAIL1_SAFEPOINT8_BASE.dll','diag/FUSEDDETAIL1_SOURCE.hlsl','diag/patch_gw16h_fg_fuseddetail1.py','diag/validate_fg_fuseddetail1.py','diag/FG_FUSEDDETAIL1_VALIDATION.txt','diag/GW16H_SAFEPOINT11_FUSEDDETAIL1_LAB_FINDING.txt','diag/BUILD_MANIFEST.json','diag/SHA256SUMS.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv','_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1']
for r in required:ck('required '+r,(ROOT/r).is_file())
if any(not (ROOT/r).is_file() for r in required):raise SystemExit(1)
ck('runtime SHA',sha(PAY/'win81_nis_dx11_x64.dll')==RT,sha(PAY/'win81_nis_dx11_x64.dll'));ck('d3d11 SHA',sha(PAY/'d3d11.dll')==RT,sha(PAY/'d3d11.dll'));ck('runtime mirrors exact',(PAY/'d3d11.dll').read_bytes()==(PAY/'win81_nis_dx11_x64.dll').read_bytes());ck('INI SHA',sha(PAY/'win81_nis.ini')==INI,sha(PAY/'win81_nis.ini'));ck('version SHA',sha(PAY/'win81_nis_version.txt')==VER,sha(PAY/'win81_nis_version.txt'));ck('SP8 branch base SHA',sha(D/'base/FUSEDDETAIL1_SAFEPOINT8_BASE.dll')==SP8,sha(D/'base/FUSEDDETAIL1_SAFEPOINT8_BASE.dll'))
install=(D/'install.ps1').read_text(errors='replace');verify=(D/'verify.ps1').read_text(errors='replace');collect=(D/'collect.ps1').read_text(errors='replace')
for label,text in [('installer',install),('verifier',verify)]:
 for h in [RT,INI,VER]:ck(label+' hash lock '+h[:8],h in text)
ck('installer package id',"package='GW16H_UNIFIEDREC3_SAFEPOINT11_FUSEDDETAIL1'" in install);ck('installer knows FUSED runtime',RT in install);ck('collector includes FUSED validation','FG_FUSEDDETAIL1_VALIDATION.txt' in collect);ck('collector includes FUSED finding','GW16H_SAFEPOINT11_FUSEDDETAIL1_LAB_FINDING.txt' in collect)
ver=(PAY/'win81_nis_version.txt').read_text(errors='replace');ini=(PAY/'win81_nis.ini').read_text(errors='replace');readme=(ROOT/'README_TEST.txt').read_text(errors='replace')
for tok in ['SAFEPOINT11=FUSEDDETAIL1','FG_FUSEDDETAIL1_NEW_TEXTURE_FETCH=ZERO_BY_SOURCE_STRUCTURE','FG_FUSEDDETAIL1_NEW_RESOURCE_BINDINGS=ZERO','FG_FUSEDDETAIL1_NEW_DISPATCH=ZERO','SAFEPOINT11_LINEAGE=BRANCH_FROM_SAFEPOINT8']:ck('version '+tok,tok in ver)
for tok in ['CONSERVATIVE/FUSEDDETAIL1','No added texture-load token','Profile 2 QUALITY is mathematically bypassed']:ck('INI '+tok,tok.lower() in ini.lower())
for tok in ['no new texture resource','no new Dispatch','one real GTX 960M test','QUALITY remains mathematically unchanged']:ck('README '+tok,tok.lower() in readme.lower())
try:
 m=json.loads((D/'BUILD_MANIFEST.json').read_text());fd=m['runtime']['fg_fuseddetail1'];ins=m['installer_fix'];hw=m['hardware_state'];val=m['validation']
 ck('manifest package',m['package']==PKG,m['package']);ck('manifest filename',m['package_filename']==PKG+'.zip');ck('manifest runtime',m['runtime']['sha256']==RT,m['runtime']['sha256']);ck('manifest branch base',fd['base_sha256']==SP8);ck('manifest load 7->7',fd['existing_load_token_count']==fd['new_load_token_count']==7);ck('manifest added load zero',fd['additional_load_token_sites']==0);ck('manifest bindings zero',fd['new_resource_bindings']==0);ck('manifest dispatch zero',fd['new_dispatches']==0);ck('manifest exact timing pending','GPU time' in hw['required_targeted_gates'][0] or 'GPU time' in str(hw));ck('manifest payload locks',ins['expected_runtime_sha256']==RT and ins['expected_ini_sha256']==INI and ins['expected_version_sha256']==VER);ck('manifest fused validation',val['fg_fuseddetail1']=='70/70 PASS')
except Exception as e:ck('manifest parse',False,e)
# Current validators re-executed against this payload.
execs=[('validate_fg_fuseddetail1.py','FG_FUSEDDETAIL1_VALIDATION=70/70 PASS'),('validate_quality_hotkey_safe.py','QUALITY_HOTKEY_VALIDATION=45/45 PASS'),('validate_pacingverifier3.py','PACINGVERIFIER3_VALIDATION=54/54 PASS'),('validate_f4_field_regression.py','F4_FIELD_REGRESSION_VALIDATION=21/21 PASS'),('validate_prod_recorder_restore.py','TOTAL=29 PASS=29 FAIL=0'),('validate_win81_path_handoff.py','WIN81_PATH_HANDOFF_VALIDATION=29/29 PASS'),('validate_ps4_compat.py','PS4_COMPAT_VALIDATION=30/30 PASS'),('validate_auto_wrapper_build.py','AUTO_WRAPPER_BUILD_VALIDATION=26/26 PASS')]
for script,marker in execs:
 cp=subprocess.run([sys.executable,str(D/script),str(ROOT)],capture_output=True,text=True);ck('execute '+script,cp.returncode==0 and marker in cp.stdout,(cp.stdout+cp.stderr).splitlines()[-1:] if cp.returncode else '')
# Inherited SAFEPOINT8 gates plus whole-binary delta isolation from FUSED validator.
for fn,marker in [('FG_TDETAIL4_VALIDATION.txt','FG_TDETAIL4_VALIDATION=65/65 PASS'),('UNIFIED_RECORDER_VALIDATION.txt','UNIFIED_RECORDER_VALIDATION=57/57 PASS'),('RECORDER_QSV_PRIORITY_VALIDATION.txt','RECORDER_QSV_PRIORITY_VALIDATION=45/45 PASS'),('GW16_RUNTIME_VALIDATION.txt','GW16_RUNTIME_VALIDATION=74/74 PASS'),('RECORDER_NATIVE_STATE_GUARD_VALIDATION.txt','RECORDER_NATIVE_STATE_GUARD_VALIDATION=54/54 PASS')]:ck('inherited report '+fn,(D/fn).is_file() and marker in (D/fn).read_text(errors='replace'))
# Registries.
try:
 ss=sums(D/'SHA256SUMS.txt');oo,ids=owners(U/'PTAR_STATIC_OWNERSHIP.tsv');allfiles=[]
 for q in ROOT.rglob('*'):
  if q.is_file():
   rel=q.relative_to(ROOT).as_posix()
   if rel.startswith('_PTAR_UNINSTALL/state/'):continue
   allfiles.append(rel)
 aset=set(allfiles);sexp=aset-{'diag/SHA256SUMS.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'};oexp=aset-{'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'}
 ck('SHA registry coverage',set(ss)==sexp,(len(ss),len(sexp)));ck('ownership coverage',set(oo)==oexp,(len(oo),len(oexp)));ck('ownership IDs contiguous',ids==list(range(82600001,82600001+len(ids))),len(ids));bad=[r for r,h in ss.items() if sha(ROOT/r)!=h];ck('SHA registry hashes exact',not bad,','.join(bad[:4]));bad=[r for r,h in oo.items() if sha(ROOT/r)!=h];ck('ownership hashes exact',not bad,','.join(bad[:4]));ck('ownership hashes SHA registry',oo.get('diag/SHA256SUMS.txt')==sha(D/'SHA256SUMS.txt'))
except Exception as e:ck('registry parse',False,e)
for pat in ['__pycache__','*.pyc','*.pyo','*disasm*','*.bak','fuseddetail1_candidate*.dll']:
 found=[q for q in ROOT.rglob(pat) if q.is_file() or q.is_dir()];ck('no work product '+pat,not found,','.join(str(x.relative_to(ROOT)) for x in found[:3]))
failed=[x for x in checks if not x[1]];print('GW16_PACKAGE_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
 for n,_,d in failed:print('FAILED',n,d)
 raise SystemExit(1)
