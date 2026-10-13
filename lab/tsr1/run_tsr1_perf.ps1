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

$totalMatch=[regex]::Match($perf,'TSR1_PERF_R8_TOTAL_TO_R7_RATIO=([0-9.]+)')
$extraMatch=[regex]::Match($perf,'TSR1_PERF_TSR_EXTRA_TO_R7_RATIO=([0-9.]+)')
if(-not $totalMatch.Success -or -not $extraMatch.Success){throw 'TSR1 perf ratios missing'}
$total=[double]::Parse($totalMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
$extra=[double]::Parse($extraMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
if($total -lt 1.0 -or $total -gt 3.0){throw "TSR1 initial total performance envelope failed ratio=$total"}
if($extra -le 0.0 -or $extra -ge $total){throw "TSR1 incremental performance accounting failed extra=$extra total=$total"}

if(Test-Path artifact\PTAR_R8_VALIDATION.txt)
{
    'TSR1_GPU_TIMESTAMP_PERF=PASS' | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_R8_TOTAL_TO_R7_GPU_RATIO='+$total.ToString('F4',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_EXTRA_TO_R7_GPU_RATIO='+$extra.ToString('F4',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
}

Write-Host "R8_TSR1_GPU_TOTAL_RATIO=$($total.ToString('F4',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host "R8_TSR1_GPU_EXTRA_RATIO=$($extra.ToString('F4',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host 'R8_TSR1_GPU_PERF=PASS'
