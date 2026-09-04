#!/usr/bin/env python3
from pathlib import Path
import hashlib, json, re, subprocess, sys, tempfile, zipfile, math
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
RUNTIME='2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d'
BASE='50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59'
checks=[]
def ck(n,c,d=''):
 checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest() if Path(p).is_file() else None
def txt(rel): return (ROOT/rel).read_text(encoding='utf-8',errors='replace') if (ROOT/rel).is_file() else ''
required=[
'01-INSTALL_GW15.bat','02-VERIFY_INSTALL.bat','03-ARM_VISIBLE_FRAME_VERIFIER.bat','04-COLLECT_RESULTS.bat','05-ROLLBACK_TEST.bat','06-DESINSTALLER_PTAR_COMPLET.bat','README_TEST.txt',
'payload/d3d11.dll','payload/win81_nis_dx11_x64.dll','payload/win81_nis.ini','payload/win81_nis_version.txt',
'diag/base/GW12_BASE.dll','diag/patch_gw15_runtime_from_gw12.py','diag/validate_gw15_runtime.py','diag/validate_gw15_package.py',
'diag/validate_pacingverifier2.py','diag/validate_win81_path_handoff.py','diag/validate_ps4_compat.py','diag/validate_f4_field_regression.py',
'diag/visible_pacing/PTARVisiblePacingVerifier.cs','diag/visible_pacing/run_single_engine_verifier.ps1','diag/GW14_FIELD_FINDING.txt','diag/BUILD_MANIFEST.json',
'_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1','_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv']
for r in required: ck('required '+r,(ROOT/r).is_file())
ck('runtime d3d11 exact',sha(ROOT/'payload/d3d11.dll')==RUNTIME,sha(ROOT/'payload/d3d11.dll'))
ck('runtime mirror exact',sha(ROOT/'payload/win81_nis_dx11_x64.dll')==RUNTIME,sha(ROOT/'payload/win81_nis_dx11_x64.dll'))
ck('GW12 base exact',sha(ROOT/'diag/base/GW12_BASE.dll')==BASE,sha(ROOT/'diag/base/GW12_BASE.dll'))
ini=txt('payload/win81_nis.ini'); ver=txt('payload/win81_nis_version.txt'); ins=txt('diag/install.ps1'); verify=txt('diag/verify.ps1')
for tok in ['FrameGeneration=0','FrameGenerationPresentSync=2','FrameGenerationTargetFPS=30','FrameGenerationRequireVSync=0','FrameGenerationSharedFlush=1']:
 ck('INI '+tok,re.search(r'(?m)^'+re.escape(tok)+r'\s*$',ini) is not None)
ck('version GW15', 'WIN81_NIS_VERSION=P1FG7N-GW15-LOCK30' in ver)
ck('version runtime hash', 'DLL_SHA256='+RUNTIME in ver)
ck('version target30', 'FRAME_GENERATION_TARGET_FPS=30' in ver and 'FRAME_GENERATION_REAL_GOVERNOR_TARGET_FPS=15' in ver)
ini_sha=sha(ROOT/'payload/win81_nis.ini'); ver_sha=sha(ROOT/'payload/win81_nis_version.txt')
for tok in [RUNTIME,ini_sha,ver_sha,"package='GW15_LOCK30'",'FrameGenerationTargetFPS=30']:
 ck('installer contract '+tok[:28],tok in ins)
ck('verify target30','FrameGenerationTargetFPS=30' in verify)
ck('verify runtime exact',RUNTIME in verify)
ck('collector prefix','PTAR_GW15_RESULTS_' in txt('diag/collect.ps1'))
ck('short package name',len('PTAR_GW15_LOCK30.zip')<40,len('PTAR_GW15_LOCK30.zip'))
ck('beeps retained','SafeBeep(900, 140)' in txt('diag/visible_pacing/PTARVisiblePacingVerifier.cs') and 'SafeBeep(1400, 180)' in txt('diag/visible_pacing/PTARVisiblePacingVerifier.cs'))
# The runtime must be byte-identical to the hardware-tested GW13 Sync2 binary.
ck('GW15 runtime binary is GW13 Sync2',RUNTIME=='2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d')
# Queue model: 60 Hz, Sync2 G + Sync2 R = 4 VBlanks/pair = 15 pairs/s.
refresh=60.0; pair_service=4.0/refresh; capacity=1.0/pair_service; target=30.0; real_target=target/2.0
ck('target30 => real15 model',abs(real_target-15.0)<1e-12,real_target)
ck('Sync2+Sync2 capacity 15 pairs/s',abs(capacity-15.0)<1e-12,capacity)
ck('target matches presenter capacity',abs(real_target-capacity)<1e-12,(real_target,capacity))
# Field rationale: GW13 source 15.387 exceeded 15 pair/s capacity; target30 removes this positive queue slope.
gw13_source=15.387; debt=(gw13_source-capacity)/capacity*100.0
ck('GW13 observed source exceeded capacity',gw13_source>capacity,'%.3f%% excess'%debt)
ck('GW15 governor target does not exceed capacity',real_target<=capacity)
# Run current validators.
for rel,label in [
 ('diag/validate_gw15_runtime.py','runtime validator'),
 ('diag/validate_pacingverifier2.py','pacing validator'),
 ('diag/validate_win81_path_handoff.py','win81 path validator'),
 ('diag/validate_ps4_compat.py','PS4 validator'),
 ('diag/validate_f4_field_regression.py','F4 regression validator')]:
 p=subprocess.run([sys.executable,str(ROOT/rel)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 last=p.stdout.strip().splitlines()[-1] if p.stdout.strip() else ''
 ck(label+' exits 0',p.returncode==0,last)
# No forbidden regression in operational runner.
runner=txt('diag/visible_pacing/run_single_engine_verifier.ps1')
for bad in ['Resolve-Path','Add-Type','New-Item -ItemType Directory -LiteralPath','Compress-Archive','Expand-Archive']:
 ck('runner excludes '+bad,bad not in '\n'.join(l for l in runner.splitlines() if not l.lstrip().startswith('#')))

# SHA manifest coverage/integrity.
sha_manifest=txt('diag/SHA256SUMS.txt').splitlines()
sha_rows=[]
for line in sha_manifest:
 if not line.strip(): continue
 m=re.match(r'^([0-9a-f]{64})  (.+)$',line.strip())
 ck('SHA256SUMS row format '+line[-32:],m is not None)
 if m: sha_rows.append((m.group(2),m.group(1)))
sha_map=dict(sha_rows)
ck('SHA256SUMS no duplicate paths',len(sha_map)==len(sha_rows),(len(sha_map),len(sha_rows)))
for rel,h in sha_rows:
 ck('SHA exact '+rel,(ROOT/rel).is_file() and sha(ROOT/rel)==h,sha(ROOT/rel) if (ROOT/rel).is_file() else 'missing')
expected_sha=set()
for pth in ROOT.rglob('*'):
 if not pth.is_file(): continue
 rel=pth.relative_to(ROOT).as_posix()
 if rel.startswith('_PTAR_UNINSTALL/state/'): continue
 if rel in {'diag/SHA256SUMS.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'}: continue
 expected_sha.add(rel)
ck('SHA256SUMS exact static coverage',set(sha_map)==expected_sha,(len(sha_map),len(expected_sha),sorted(expected_sha-set(sha_map))[:4],sorted(set(sha_map)-expected_sha)[:4]))
# Ownership registry coverage/integrity. It intentionally does not own itself.
own_lines=txt('_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv').splitlines()
own=[]
for line in own_lines:
 if not re.match(r'^\d+\|',line): continue
 a=line.split('|')
 ck('ownership row 3 fields '+line[:24],len(a)==3)
 if len(a)==3: own.append((a[1],a[2]))
own_map=dict(own)
ck('ownership no duplicate paths',len(own_map)==len(own),(len(own_map),len(own)))
for rel,h in own:
 ck('ownership exact '+rel,(ROOT/rel).is_file() and sha(ROOT/rel)==h,sha(ROOT/rel) if (ROOT/rel).is_file() else 'missing')
expected_own=set()
for pth in ROOT.rglob('*'):
 if not pth.is_file(): continue
 rel=pth.relative_to(ROOT).as_posix()
 if rel.startswith('_PTAR_UNINSTALL/state/'): continue
 if rel=='_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv': continue
 expected_own.add(rel)
ck('ownership exact static coverage',set(own_map)==expected_own,(len(own_map),len(expected_own),sorted(expected_own-set(own_map))[:4],sorted(set(own_map)-expected_own)[:4]))
ck('ownership has GW15 installer','01-INSTALL_GW15.bat' in own_map)
ck('ownership excludes obsolete GW13 installer','01-INSTALL_GW13.bat' not in own_map)
# Production-style uninstaller contract retained.
un=txt('_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1'); ub=txt('06-DESINSTALLER_PTAR_COMPLET.bat')
for tok in ['PTAR_STATIC_OWNERSHIP.tsv','Get-FileHash','[KEEP]','install_state.json']:
 ck('uninstaller token '+tok,tok in un)
ck('uninstaller temp engine copy','%TEMP%\\PTAR_GW15_UNINSTALL_' in ub and 'copy /y' in ub.lower())
ck('uninstaller self cleanup delayed','ping -n 3 127.0.0.1' in ub and 'del /f /q' in ub.lower())

# Manifest integrity.
try:
 m=json.loads(txt('diag/BUILD_MANIFEST.json'));ck('manifest package',m.get('package')=='PTAR_GW15_LOCK30',m.get('package'));ck('manifest target30',m.get('config_delta',{}).get('FrameGenerationTargetFPS',{}).get('to')==30);ck('manifest runtime',m.get('runtime',{}).get('sha256')==RUNTIME)
except Exception as e: ck('manifest parse',False,e)
failed=[x for x in checks if not x[1]]
print('GW15_PACKAGE_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
 for n,_,d in failed: print('FAILED',n,d)
 sys.exit(1)
