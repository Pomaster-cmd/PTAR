#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,sys,json
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
D=ROOT/'diag'; PAY=ROOT/'payload'; U=ROOT/'_PTAR_UNINSTALL'
RT='e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429'
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def txt(rel): return (ROOT/rel).read_text(errors='replace')
ck('runtime exact HUDREC1 retained',sha(PAY/'d3d11.dll')==RT,sha(PAY/'d3d11.dll'))
ck('runtime mirror exact',sha(PAY/'win81_nis_dx11_x64.dll')==RT)
b=(PAY/'d3d11.dll').read_bytes()
for s in [b'Warhammer.exe',b'Warhammer',b'Inquisitor',b'NeoCore']:
    ck('runtime excludes '+s.decode(),s not in b)
ini=txt('payload/win81_nis.ini').splitlines()
ck('INI has one dynamic target placeholder',sum(1 for x in ini if x.startswith('TargetExe='))==1 and 'TargetExe=__PTAR_TARGET_EXE__' in ini)
ver=txt('payload/win81_nis_version.txt')
ck('version marks dynamic target','TARGET_EXE=DYNAMIC_INSTALL_SELECTION' in ver)
ck('historical validation exe is metadata only','FIELD_VALIDATION_EXE=Warhammer.exe' in ver)
ins=txt('diag/install.ps1'); vr=txt('diag/verify.ps1'); col=txt('diag/collect.ps1'); rb=txt('diag/rollback_test.ps1'); un=txt('_PTAR_UNINSTALL/PTAR_SAFE_UNINSTALL.ps1'); marker=txt('diag/set_vblank_diagnostics.ps1'); arm=txt('03-ARM_VISIBLE_FRAME_VERIFIER.bat'); runner=txt('diag/visible_pacing/run_single_engine_verifier.ps1')
functional={'install':ins,'verify':vr,'collect':col,'rollback':rb,'uninstall':un,'marker':marker,'arm':arm,'runner':runner}
for name,t in functional.items():
    ck(name+' no Warhammer executable lock','Warhammer.exe' not in t)
    ck(name+' no NeoCore registry lock','NeoCore Games' not in t)
ck('installer explicit path parameter',"param([string]$GameExe='')" in ins)
ck('installer env path override','$env:PTAR_GAME_EXE' in ins)
ck('installer interactive candidate selection','Executables x64 detectes' in ins and 'Read-Host' in ins)
ck('installer PE64 guard','0x8664' in ins and 'Test-Pe64' in ins)
ck('installer schema 4',"schema=4" in ins)
ck('installer exact path process guard','Is-TargetRunning $TargetExePath' in ins)
ck('installer stores target exe/name','target_exe=$TargetExePath' in ins and 'target_exe_name=$TargetExeName' in ins)
ck('installer writes root handoff','win81_nis_install_target.txt' in ins)
ck('installer writes exe handoff','win81_nis_install_exe.txt' in ins)
ck('installer builds dynamic INI',"'TargetExe='+$TargetExeName" in ins and 'generated_win81_nis.ini' in ins)
ck('installer does not mutate registry','Set-ItemProperty' not in ins and 'HKCU:' not in ins)
ck('verify requires schema 4','[int]$m.schema -lt 4' in vr)
ck('verify dynamic target contract',"'TargetExe='+$targetName" in vr)
ck('verify permits only marker toggle in addition','VBlankDiagnostics=<SUPPORTED_TOGGLE>' in vr)
ck('collector resolves installation state',"LATEST_STATE.txt" in col and '$m.game_root' in col)
ck('rollback exact target process guard','$m.target_exe' in rb and 'Is-Running $exe' in rb)
ck('uninstall exact target process guard','$m.target_exe' in un and 'MainModule.FileName' in un)
ck('uninstall has no WindowStyle registry restore','WindowStyle' not in un and 'Set-ItemProperty' not in un)
ck('marker resolves state',"LATEST_STATE.txt" in marker and '$m.game_root' in marker)
ck('visible verifier uses target handoff','win81_nis_install_exe.txt' in arm and 'PTAR_TARGET_EXE' in arm)
ck('runner validates target exe','$env:PTAR_TARGET_EXE' in runner and 'Executable cible absent' in runner)
for rel in ['03-INSTALL_QSV_HELPER_WIN81.bat','04-TEST_QSV_5S_MP4.bat']:
    t=txt(rel); ck(rel+' uses generic target root handoff','win81_nis_install_target.txt' in t)
    ck(rel+' has no title-specific fallback','SatGat' not in t and 'Warhammer' not in t and 'Inquisitor' not in t)
qsv=txt('tools/installer/INSTALL_QSV_HELPER_WIN81.ps1')
ck('QSV installer requires recorded target handoff','win81_nis_install_target.txt' in qsv and "return $null" in qsv)
ck('QSV installer has no renderer-name heuristic','*-Win64-Shipping.exe' not in qsv)
readme=txt('README.md')
ck('README declares game agnostic','game-agnostic' in readme.lower())
ck('README documents explicit exe path','01-INSTALL_GW16.bat "C:\\full\\path\\Game.exe"' in readme)
ck('README distinguishes validation title','only one historical hardware-validation title' in readme)
failed=[x for x in checks if not x[1]]
print('UNIVERSAL_TARGETING_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for n,_,d in failed: print('FAILED',n,d)
    raise SystemExit(1)
