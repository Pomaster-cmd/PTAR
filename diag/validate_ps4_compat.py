#!/usr/bin/env python3
from pathlib import Path
import re,sys
ROOT=Path(__file__).resolve().parents[1]
checks=[]
def ck(n,c,d=''):
    checks.append((n,bool(c),str(d))); print(('[PASS] ' if c else '[FAIL] ')+n+((' :: '+str(d)) if d else ''))
ps_files=sorted(list(ROOT.rglob('*.ps1')))
bat_files=sorted(list(ROOT.rglob('*.bat')))
all_ps='\n'.join(p.read_text(errors='replace') for p in ps_files)
runner=(ROOT/'diag/visible_pacing/run_single_engine_verifier.ps1').read_text(errors='replace')
cs=(ROOT/'diag/visible_pacing/PTARVisiblePacingVerifier.cs').read_text(errors='replace')
# Known Windows PowerShell 4 incompatibilities / field regressions.
forbidden={
 'New-Item -LiteralPath':r'(?i)\bNew-Item\b[^\r\n]*\-LiteralPath\b',
 'Resolve-Path in pacing runner':r'(?i)\bResolve-Path\b',
 'Add-Type in pacing runner':r'(?i)\bAdd-Type\b',
 'Compress-Archive':r'(?i)\bCompress-Archive\b',
 'Expand-Archive':r'(?i)\bExpand-Archive\b',
 'ForEach-Object -Parallel':r'(?i)ForEach-Object\s+-Parallel\b',
 'ConvertFrom-Json -AsHashtable':r'(?i)ConvertFrom-Json\b[^\r\n]*-AsHashtable\b',
 'Get-FileHash -InputStream':r'(?i)Get-FileHash\b[^\r\n]*-InputStream\b',
}
for name,pat in forbidden.items():
    hay=runner if name in {'Resolve-Path in pacing runner','Add-Type in pacing runner'} else all_ps
    # comments are not executable; strip comment-only lines for command scans.
    exec_hay='\n'.join(l for l in hay.splitlines() if not l.lstrip().startswith('#'))
    ck('PS4 excludes '+name,re.search(pat,exec_hay) is None)
ck('runner temp dir via .NET','[IO.Directory]::CreateDirectory($tmpDir)' in runner)
ck('runner no dynamic assembly invocation','[PTARVisiblePacingVerifier]::Run' not in runner and '$runMethod' not in runner and '[Reflection.Assembly]' not in runner)
ck('runner executes dedicated EXE',"'/target:exe'" in runner and '$tmpExe' in runner and "'--selftest'" in runner)
ck('runner direct Framework4 CSC','Framework64\\v4.0.30319\\csc.exe' in runner and 'Framework\\v4.0.30319\\csc.exe' in runner)
ck('runner environment root transport','$env:PTAR_GAME_ROOT' in runner)
# Guard against C# syntax newer than the .NET 4 csc shipped on Windows 8.1.
modern=[r'\$"',r'\bnameof\s*\(',r'\?\.',r'\busing\s+var\b',r'\brecord\s+',r'\breadonly\s+struct\b',r'=>']
for pat in modern: ck('C#4/5 excludes '+pat,re.search(pat,cs) is None)
# Balanced structural delimiters in diagnostic C# source.
for a,b,n in [('(',')','paren'),('{','}','brace'),('[',']','bracket')]: ck('C# balanced '+n,cs.count(a)==cs.count(b),(cs.count(a),cs.count(b)))
# Ensure all batch launchers explicitly use Windows PowerShell v1.0 host path (works for PS4).
for p in bat_files:
    txt=p.read_text(errors='replace')
    if 'powershell' in txt.lower(): ck('BAT explicit WindowsPowerShell host '+p.name,'WindowsPowerShell\\v1.0\\powershell.exe' in txt)
failed=[x for x in checks if not x[1]]
print('PS4_COMPAT_VALIDATION=%d/%d %s'%(len(checks)-len(failed),len(checks),'PASS' if not failed else 'FAIL'))
if failed:
    for x in failed: print('FAILED',x[0],x[2])
    sys.exit(1)
