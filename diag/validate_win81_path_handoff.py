#!/usr/bin/env python3
from pathlib import Path
import sys,re
ROOT=Path(__file__).resolve().parents[1]
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),d));print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
arm=(ROOT/'03-ARM_VISIBLE_FRAME_VERIFIER.bat').read_text(errors='replace')
run=(ROOT/'diag/visible_pacing/run_single_engine_verifier.ps1').read_text(errors='replace')
ver=(ROOT/'diag/verify.ps1').read_text(errors='replace')
# Exact field path from the failed Windows 8.1 host.
field='C:\\Program Files (x86)\\Steam\\steamapps\\common\\Warhammer 40,000 Inquisitor - Martyr\\'
ck('fixture has spaces', ' ' in field)
ck('fixture has comma', ',' in field)
ck('fixture has hyphen', ' - ' in field)
ck('fixture has trailing backslash', field.endswith('\\'))
# Regression: game root must never be transported as a quoted CLI argument.
ck('no -GameRoot CLI argument in launcher','-GameRoot' not in arm)
ck('launcher exports PTAR_GAME_ROOT','set "PTAR_GAME_ROOT=%%~fI"' in arm)
ck('launcher canonicalizes GAMEROOT dot path','for %%I in ("%GAMEROOT%.")' in arm)
ck('runner reads PTAR_GAME_ROOT','$env:PTAR_GAME_ROOT' in run)
ck('runner does not use Resolve-Path','Resolve-Path' not in run)
ck('root resolution occurs inside try',run.find('try{',run.find('$ErrorActionPreference')) < run.index('$rawRoot=[string]$env:PTAR_GAME_ROOT') < run.rfind('catch{'))
ck('runner validates root with LiteralPath','Test-Path -LiteralPath $rawRoot -PathType Container' in run)
ck('runner canonicalizes with GetFullPath','[IO.Path]::GetFullPath($rawRoot)' in run)
ck('runner validates Warhammer.exe',"Join-Path $GameRoot 'Warhammer.exe'" in run)
ck('runner supports preflight','[switch]$PreflightOnly' in run and 'VBLANK3 PRE-FLIGHT Windows/PS4' in run)
ck('verify runs same preflight','-PreflightOnly' in ver and '$env:PTAR_GAME_ROOT=[IO.Path]::GetFullPath($g)' in ver)
# PowerShell 4 compatibility guardrails used by the field host.
for bad in ['??','?.','ForEach-Object -Parallel','ConvertFrom-Json -AsHashtable','Get-FileHash -InputStream']:
    ck('PS4 excludes '+bad,bad not in run)
# Direct CSC compile, no Add-Type path normalization.
ck('no Add-Type in runner','Add-Type' not in run.replace('Do not use Add-Type','').replace('rejected Add-Type -Path',''))
ck('direct Framework4 CSC','Framework64\\v4.0.30319\\csc.exe' in run and 'Framework\\v4.0.30319\\csc.exe' in run)
ck('compile output path object-safe',"('/out:{0}' -f $tmpExe)" in run)
ck('dedicated executable target',"'/target:exe'" in run and "'/platform:x64'" in run)
ck('preflight executes compiled EXE',"& $tmpExe '--selftest'" in run)
ck('no dynamic assembly loading','[Reflection.Assembly]' not in run and '$runMethod' not in run)

# Exact second hardware regression: New-Item does not expose -LiteralPath in Windows PowerShell 4.
ck('no invalid New-Item -LiteralPath','New-Item -ItemType Directory -LiteralPath' not in run)
ck('temp directory uses System.IO','[IO.Directory]::CreateDirectory($tmpDir)' in run)
ck('actual run executes dedicated EXE','& $tmpExe $DurationSeconds' in run and '[PTARVisiblePacingVerifier]::Run' not in run)
fail=[x for x in checks if not x[1]]
print('WIN81_PATH_HANDOFF_VALIDATION=%d/%d %s'%(len(checks)-len(fail),len(checks),'PASS' if not fail else 'FAIL'))
if fail:
    for n,_,d in fail:print('FAILED',n,d)
    sys.exit(1)
