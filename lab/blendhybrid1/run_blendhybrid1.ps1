$ErrorActionPreference='Stop'
.\lab\blendhybrid1\compile_hybrid.ps1
if($LASTEXITCODE){throw 'hybrid compile failed'}
python .\lab\blendhybrid1\prepare_hybrid_harnesses.py
if($LASTEXITCODE){throw 'hybrid harness prep failed'}

& cl.exe /nologo /O2 /MT /W4 /WX /wd4244 /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\blendhybrid1\hybrid_quality.cpp /link d3d9.lib user32.lib /OUT:artifact\hybrid_quality.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'hybrid quality compile failed'}
Push-Location artifact
& .\hybrid_quality.exe 2>&1 | Tee-Object HYBRID_QUALITY_LOG.txt
$rc=$LASTEXITCODE
Pop-Location
if($rc){throw "hybrid quality failed rc=$rc"}

& cl.exe /nologo /O2 /MT /W4 /WX /wd4244 /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\blendhybrid1\hybrid_temporal.cpp /link d3d9.lib user32.lib /OUT:artifact\hybrid_temporal.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'hybrid temporal compile failed'}
Push-Location artifact
& .\hybrid_temporal.exe 2>&1 | Tee-Object HYBRID_TEMPORAL_LOG.txt
$rc=$LASTEXITCODE
Pop-Location
if($rc){throw "hybrid temporal failed rc=$rc"}

& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\blendhybrid1\hybrid_perf.cpp /link d3d9.lib user32.lib /OUT:artifact\hybrid_perf.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'hybrid perf compile failed'}
Push-Location artifact
& .\hybrid_perf.exe 2>&1 | Tee-Object HYBRID_PERF_LOG.txt
$rc=$LASTEXITCODE
Pop-Location
if($rc){throw "hybrid perf failed rc=$rc"}

foreach($file in @('HYBRID_QUALITY_LOG.txt','HYBRID_TEMPORAL_LOG.txt','HYBRID_PERF_LOG.txt')){
  if(-not (Test-Path (Join-Path artifact $file))){throw "missing $file"}
}
@(
 'BLENDHYBRID1=PASS',
 'EXTRA_TEXTURE_TAPS=0',
 'PRODUCTION_RUNTIME_CHANGED=NO') | Set-Content artifact\BLENDHYBRID1_STATUS.txt -Encoding ascii
Write-Host 'BLENDHYBRID1_MATRIX=PASS'
