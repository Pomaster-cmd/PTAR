#!/usr/bin/env python3
from pathlib import Path
import sys
ROOT=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1]).resolve()
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),d)); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
arm=(ROOT/'03-ARM_VISIBLE_FRAME_VERIFIER.bat').read_text(errors='replace')
run=(ROOT/'diag/visible_pacing/run_single_engine_verifier.ps1').read_text(errors='replace')
ver=(ROOT/'diag/verify.ps1').read_text(errors='replace')
ins=(ROOT/'diag/install.ps1').read_text(errors='replace')
# Generic fixture deliberately differs from the historical Warhammer path.
field=r'C:\Games\StarGazer\Binaries\Win64\StarGazer-Win64-Shipping.exe'
ck('fixture has spaces or nested path', '\\' in field and field.lower().endswith('.exe'))
ck('installer accepts explicit GameExe', "param([string]$GameExe='')" in ins)
ck('installer supports PTAR_GAME_EXE env', '$env:PTAR_GAME_EXE' in ins)
ck('installer supports optional target hint', 'PTAR_TARGET_EXE.txt' in ins)
ck('installer validates PE64', 'Test-Pe64' in ins and '0x8664' in ins)
ck('installer stores exact target exe', 'target_exe=$TargetExePath' in ins and 'target_exe_name=$TargetExeName' in ins)
ck('installer stores target directory handoff', 'win81_nis_install_target.txt' in ins)
ck('installer stores target executable handoff', 'win81_nis_install_exe.txt' in ins)
ck('installer dynamic TargetExe substitution', "'TargetExe='+$TargetExeName" in ins)
ck('installer has no game registry mutation', 'HKCU:' not in ins and 'Set-ItemProperty' not in ins)
ck('launcher reads target root handoff', 'win81_nis_install_target.txt' in arm)
ck('launcher reads target exe handoff', 'win81_nis_install_exe.txt' in arm)
ck('launcher exports PTAR_GAME_ROOT', 'set "PTAR_GAME_ROOT=%GAMEROOT%"' in arm)
ck('launcher exports PTAR_TARGET_EXE', 'set "PTAR_TARGET_EXE=%GAMEEXE%"' in arm)
ck('runner reads PTAR_GAME_ROOT', '$env:PTAR_GAME_ROOT' in run)
ck('runner reads PTAR_TARGET_EXE', '$env:PTAR_TARGET_EXE' in run)
ck('runner does not use Resolve-Path', 'Resolve-Path' not in run)
ck('runner validates root with LiteralPath', 'Test-Path -LiteralPath $rawRoot -PathType Container' in run)
ck('runner canonicalizes root', '[IO.Path]::GetFullPath($rawRoot)' in run)
ck('runner validates selected exe', 'Executable cible absent' in run)
ck('runner checks exe/root pairing', 'PTAR_TARGET_EXE ne correspond pas a PTAR_GAME_ROOT' in run)
ck('runner supports preflight', '[switch]$PreflightOnly' in run and 'VBLANK3 PRE-FLIGHT Windows/PS4' in run)
ck('verify exports same root', '$env:PTAR_GAME_ROOT=[IO.Path]::GetFullPath($g)' in ver)
ck('verify exports same target exe', '$env:PTAR_TARGET_EXE=[IO.Path]::GetFullPath($targetExe)' in ver)
for bad in ['??','?.','ForEach-Object -Parallel','ConvertFrom-Json -AsHashtable','Get-FileHash -InputStream']:
    ck('PS4 excludes '+bad, bad not in run and bad not in ins and bad not in ver)
ck('direct Framework4 CSC','Framework64\\v4.0.30319\\csc.exe' in run and 'Framework\\v4.0.30319\\csc.exe' in run)
ck('dedicated executable target',"'/target:exe'" in run and "'/platform:x64'" in run)
ck('preflight executes compiled EXE',"& $tmpExe '--selftest'" in run)
ck('no invalid New-Item -LiteralPath','New-Item -ItemType Directory -LiteralPath' not in run)
ck('temp directory uses System.IO','[IO.Directory]::CreateDirectory($tmpDir)' in run)
ck('actual run executes dedicated EXE','& $tmpExe $DurationSeconds' in run and '[PTARVisiblePacingVerifier]::Run' not in run)
for token in ['Warhammer.exe','NeoCore Games\\Warhammer','Get-Process Warhammer']:
    ck('functional path excludes '+token, token not in arm and token not in run and token not in ver and token not in ins)
fail=[x for x in checks if not x[1]]
print('WIN81_PATH_HANDOFF_UNIVERSAL_VALIDATION=%d/%d %s'%(len(checks)-len(fail),len(checks),'PASS' if not fail else 'FAIL'))
if fail:
    for n,_,d in fail: print('FAILED',n,d)
    raise SystemExit(1)
