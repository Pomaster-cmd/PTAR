$ErrorActionPreference='Stop'

& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 lab\tsr1\d3d9_tsr1_perf.cpp /link d3d9.lib user32.lib /OUT:artifact\d3d9_tsr1_perf.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'TSR1 GPU perf compile failed'}

& artifact\d3d9_tsr1_perf.exe 2>&1 | Tee-Object artifact\R8_TSR1_GPU_PERF.txt
if($LASTEXITCODE){throw 'TSR1 GPU perf execution failed'}
$perf=Get-Content artifact\R8_TSR1_GPU_PERF.txt -Raw
foreach($marker in @(
 'TSR1_PERF_GPU_TIMESTAMP=PASS',
 'TSR1_PERF_INITIAL_ENVELOPE=PASS',
 'TSR1_GPU_PERF=PASS'))
{
    if($perf -notmatch [regex]::Escape($marker)){throw "TSR1 perf marker missing: $marker"}
}

$ratioMatch=[regex]::Match($perf,'TSR1_PERF_R8_TO_R7_RATIO=([0-9.]+)')
if(-not $ratioMatch.Success){throw 'TSR1 perf ratio missing'}
$ratio=[double]::Parse($ratioMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
if($ratio -lt 1.0 -or $ratio -gt 4.0){throw "TSR1 initial performance envelope failed ratio=$ratio"}

if(Test-Path artifact\PTAR_R8_VALIDATION.txt)
{
    'TSR1_GPU_TIMESTAMP_PERF=PASS' | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_R8_TO_R7_GPU_RATIO='+$ratio.ToString('F4',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
}

Write-Host "R8_TSR1_GPU_PERF_RATIO=$($ratio.ToString('F4',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host 'R8_TSR1_GPU_PERF=PASS'
