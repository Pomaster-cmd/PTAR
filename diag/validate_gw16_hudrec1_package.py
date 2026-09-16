#!/usr/bin/env python3
"""Final static package gate for HUDREC1 UNIVERSAL1."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
D=ROOT/'diag'; PAY=ROOT/'payload'; U=ROOT/'_PTAR_UNINSTALL'
RT='e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429'; BASE='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'; INI='dea8a93e97d3ad1b438973822d67ca9ac12477b5774933eea135ab71776c5648'; VER='bf31c0593b5fec78c533ba6f994866bc8d4921139bff4d602fde292c6a376f15'
PKG='PTAR_GW16H_UNIFIEDREC3_SAFEPOINT11_FUSEDDETAIL1_HUDREC1_UNIVERSAL1'
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def sums(p):
    out={}
    for line in p.read_text(errors='replace').splitlines():
        if not line.strip(): continue
        m=re.match(r'^([0-9a-f]{64})  (.+)$',line)
        if not m: raise ValueError('bad sums '+line)
        out[m.group(2)]=m.group(1)
    return out
def owners(p):
    lines=p.read_text(errors='replace').splitlines(); assert lines and lines[0]=='PTAR_STATIC_OWNERSHIP_SCHEMA\t3'
    out={}; ids=[]
    for line in lines[1:]:
        if not line.strip(): continue
        a=line.split('|'); assert len(a)==3 and re.fullmatch(r'\d{8}',a[0]) and re.fullmatch(r'[0-9a-f]{64}',a[2])
        out[a[1]]=a[2]; ids.append(int(a[0]))
    return out,ids

required=[
'01-INSTALL_GW16.bat','02-VERIFY_INSTALL.bat','03-ARM_VISIBLE_FRAME_VERIFIER.bat','03-INSTALL_QSV_HELPER_WIN81.bat',
'04-COLLECT_RESULTS.bat','04-TEST_QSV_5S_MP4.bat','05-ROLLBACK_TEST.bat','06-DESINSTALLER_PTAR_COMPLET.bat',
'README.md','README_TEST.txt','LICENSE','payload/d3d11.dll','payload/win81_nis_dx11_x64.dll','payload/win81_nis.ini',
'payload/win81_nis_version.txt','diag/base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll','diag/install.ps1','diag/verify.ps1',
'diag/collect.ps1','diag/rollback_test.ps1','diag/set_vblank_diagnostics.ps1','diag/FG_MARKER_VISIBILITY.bat',
'diag/visible_pacing/run_single_engine_verifier.ps1','diag/validate_recorder_hudrec1.py','diag/validate_fg_marker_visibility.py',
'diag/validate_universal_targeting.py','diag/validate_win81_path_handoff.py','diag/BUILD_MANIFEST.json','diag/SHA256SUMS.txt',
'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv','_PTAR_UNINSTALL/PTAR_STATIC_DIRS.tsv','_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1'
]
for r in required: ck('required '+r,(ROOT/r).is_file())
if any(not (ROOT/r).is_file() for r in required): raise SystemExit(1)

ck('runtime exact HUDREC1',sha(PAY/'d3d11.dll')==RT,sha(PAY/'d3d11.dll'))
ck('runtime mirror exact HUDREC1',sha(PAY/'win81_nis_dx11_x64.dll')==RT,sha(PAY/'win81_nis_dx11_x64.dll'))
ck('runtime mirrors exact',(PAY/'d3d11.dll').read_bytes()==(PAY/'win81_nis_dx11_x64.dll').read_bytes())
ck('INI template exact',sha(PAY/'win81_nis.ini')==INI,sha(PAY/'win81_nis.ini'))
ck('version exact',sha(PAY/'win81_nis_version.txt')==VER,sha(PAY/'win81_nis_version.txt'))
ck('bundled SAFEPOINT11 exact',sha(D/'base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll')==BASE,sha(D/'base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll'))

b=(D/'base/SAFEPOINT11_FUSEDDETAIL1_BASE.dll').read_bytes(); n=(PAY/'d3d11.dll').read_bytes()
diff=[i for i,(x,y) in enumerate(zip(b,n)) if x!=y]
ck('HUDREC1 whole binary size same',len(b)==len(n),(len(b),len(n)))
ck('HUDREC1 exact 3-byte delta',diff==[0xD0,0xD1,0x4CBBC],','.join(hex(x) for x in diff))
ck('HUDREC1 single functional byte',[x for x in diff if x not in (0xD0,0xD1)]==[0x4CBBC])

install=(D/'install.ps1').read_text(errors='replace'); verify=(D/'verify.ps1').read_text(errors='replace')
for label,text in [('installer',install),('verifier',verify)]:
    for h in [RT,INI,VER]: ck(label+' hash lock '+h[:8],h in text)
ck('installer package universal id',"HUDREC1_UNIVERSAL1" in install)
ck('installer schema 4','schema=4' in install)
ck('installer dynamic target','target_exe=$TargetExePath' in install and "'TargetExe='+$TargetExeName" in install)
ck('installer no registry mutation','HKCU:' not in install and 'Set-ItemProperty' not in install)
ck('verifier dynamic target',"'TargetExe='+$targetName" in verify and '[int]$m.schema -lt 4' in verify)

functional=[
'diag/install.ps1','diag/verify.ps1','diag/collect.ps1','diag/rollback_test.ps1',
'_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1','diag/set_vblank_diagnostics.ps1',
'03-ARM_VISIBLE_FRAME_VERIFIER.bat','diag/visible_pacing/run_single_engine_verifier.ps1',
'03-INSTALL_QSV_HELPER_WIN81.bat','04-TEST_QSV_5S_MP4.bat','tools/installer/INSTALL_QSV_HELPER_WIN81.ps1'
]
for rel in functional:
    t=(ROOT/rel).read_text(errors='replace')
    ck(rel+' no Warhammer lock','Warhammer.exe' not in t and 'Get-Process Warhammer' not in t)
    ck(rel+' no SatGat fallback','SatGat' not in t)
    ck(rel+' no NeoCore registry','NeoCore Games' not in t)

ini_lines=(PAY/'win81_nis.ini').read_text(errors='replace').splitlines()
ck('one TargetExe placeholder',sum(1 for x in ini_lines if x.startswith('TargetExe='))==1 and 'TargetExe=__PTAR_TARGET_EXE__' in ini_lines)
v=(PAY/'win81_nis_version.txt').read_text(errors='replace')
for tok in ['TARGET_EXE=DYNAMIC_INSTALL_SELECTION','HUDREC1_HARDWARE_VALIDATED=YES','UNIVERSAL_TARGETING=INSTALL_TIME_DYNAMIC_EXE','GAME_SPECIFIC_REGISTRY_MUTATION=NONE']:
    ck('version '+tok,tok in v)

try:
    m=json.loads((D/'BUILD_MANIFEST.json').read_text())
    ck('manifest package',m['package']==PKG,m.get('package'))
    ck('manifest filename',m['package_filename']==PKG+'.zip',m.get('package_filename'))
    ck('manifest runtime',m['runtime']['sha256']==RT,m['runtime'].get('sha256'))
    u=m['universal_targeting']
    ck('manifest universal version',u['version']=='UNIVERSAL1')
    ck('manifest runtime unchanged',u['runtime_binary_changed'] is False and u['runtime_sha256']==RT)
    ck('manifest no game registry mutation',u['game_specific_registry_mutation'] is False)
    ck('manifest schema 4',u['state_schema']==4)
    ins=m['installer_fix']
    ck('manifest payload locks',ins['expected_runtime_sha256']==RT and ins['expected_ini_sha256']==INI and ins['expected_version_sha256']==VER)
except Exception as e:
    ck('manifest parse',False,e)

execs=[
('validate_recorder_hudrec1.py','RECORDER_HUDREC1_VALIDATION=38/38 PASS'),
('validate_fg_marker_visibility.py','FG_MARKER_VISIBILITY_VALIDATION=17/17 PASS'),
('validate_universal_targeting.py','UNIVERSAL_TARGETING_VALIDATION='),
('validate_win81_path_handoff.py','WIN81_PATH_HANDOFF_UNIVERSAL_VALIDATION=38/38 PASS'),
('validate_ps4_compat.py','PS4_COMPAT_VALIDATION=31/31 PASS')
]
for script,marker in execs:
    cp=subprocess.run([sys.executable,str(D/script),str(ROOT)],capture_output=True,text=True,timeout=20)
    ck('execute '+script,cp.returncode==0 and marker in cp.stdout,(cp.stdout+cp.stderr).splitlines()[-2:])

readme=(ROOT/'README.md').read_text(errors='replace')
ck('README game agnostic','game-agnostic' in readme.lower())
ck('README explicit nested exe support','01-INSTALL_GW16.bat "C:\\full\\path\\Game.exe"' in readme)
ck('README scope x64 D3D11','x64 Direct3D 11' in readme)
ck('README says validation title only','only one historical hardware-validation title' in readme)

# Registries
try:
    ss=sums(D/'SHA256SUMS.txt'); oo,ids=owners(U/'PTAR_STATIC_OWNERSHIP.tsv'); allfiles=[]
    for q in ROOT.rglob('*'):
        if q.is_file():
            rel=q.relative_to(ROOT).as_posix()
            if rel.startswith('_PTAR_UNINSTALL/state/') or rel.startswith('diag/backups/') or rel.startswith('.github/'): continue
            allfiles.append(rel)
    aset=set(allfiles); sexp=aset-{'diag/SHA256SUMS.txt','_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'}; oexp=aset-{'_PTAR_UNINSTALL/PTAR_STATIC_OWNERSHIP.tsv'}
    ck('SHA registry coverage',set(ss)==sexp,(len(ss),len(sexp)))
    ck('ownership coverage',set(oo)==oexp,(len(oo),len(oexp)))
    ck('ownership IDs contiguous',ids==list(range(82600001,82600001+len(ids))),len(ids))
    bad=[r for r,h in ss.items() if sha(ROOT/r)!=h]; ck('SHA registry hashes exact',not bad,','.join(bad[:6]))
    bad=[r for r,h in oo.items() if sha(ROOT/r)!=h]; ck('ownership hashes exact',not bad,','.join(bad[:6]))
    ck('ownership hashes SHA registry',oo.get('diag/SHA256SUMS.txt')==sha(D/'SHA256SUMS.txt'))
except Exception as e: ck('registry parse',False,e)

for pat in ['__pycache__','*.pyc','*.pyo','*disasm*','*.bak','hudrec1_candidate*.dll']:
    found=[q for q in ROOT.rglob(pat) if q.is_file() or q.is_dir()]
    ck('no work product '+pat,not found,','.join(str(x.relative_to(ROOT)) for x in found[:3]))

failed=[x for x in checks if not x[1]]
print('HUDREC1_UNIVERSAL1_PACKAGE_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
