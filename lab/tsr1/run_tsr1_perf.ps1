$ErrorActionPreference='Stop'

# Keep the qualified R8 gate as the default. R13 MAX1 intentionally spends
# more resolve ALU / current-source support for maximum perceived 1080p quality,
# so its workflow receives a separate explicit lab envelope without weakening
# the R8 baseline gate on other branches/revisions.
$resolveSource=''
if(Test-Path 'lab\tsr1\ptar_tsr1_resolve_ps.hlsl')
{
    $resolveSource=Get-Content 'lab\tsr1\ptar_tsr1_resolve_ps.hlsl' -Raw
}
$isR13Max1=$resolveSource -match [regex]::Escape('PTAR R13 PLSR MAX1')
$totalMax=if($isR13Max1){3.20}else{2.35}
$extraMax=if($isR13Max1){1.90}else{1.35}
$perfProfile=if($isR13Max1){'R13_MAX1'}else{'R8_QUALIFIED'}

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

# Hosted HAL absolute milliseconds are not portable performance claims. Use
# within-run ratios only. R8 stays at the original qualified 2.35 / 1.35 gate;
# R13 MAX1 uses the workflow's maximum-quality lab envelope 3.20 / 1.90.
if($total -lt 1.0 -or $total -gt $totalMax){throw "TSR1 total GPU regression ratio=$total (>$totalMax profile=$perfProfile)"}
if($extra -le 0.0 -or $extra -gt $extraMax){throw "TSR1 incremental GPU regression ratio=$extra (>$extraMax profile=$perfProfile)"}
if($extra -ge $total){throw "TSR1 incremental performance accounting failed extra=$extra total=$total"}

if(Test-Path artifact\PTAR_R8_VALIDATION.txt)
{
    'TSR1_GPU_TIMESTAMP_PERF=PASS' | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_GPU_PERF_PROFILE='+$perfProfile) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_GPU_RATIO_GATE_TOTAL_MAX='+$totalMax.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_GPU_RATIO_GATE_EXTRA_MAX='+$extraMax.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_R8_TOTAL_TO_R7_GPU_RATIO='+$total.ToString('F4',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_EXTRA_TO_R7_GPU_RATIO='+$extra.ToString('F4',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
}

Write-Host "TSR1_GPU_PERF_PROFILE=$perfProfile"
Write-Host "TSR1_GPU_TOTAL_GATE_MAX=$($totalMax.ToString('F2',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host "TSR1_GPU_EXTRA_GATE_MAX=$($extraMax.ToString('F2',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host "R8_TSR1_GPU_TOTAL_RATIO=$($total.ToString('F4',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host "R8_TSR1_GPU_EXTRA_RATIO=$($extra.ToString('F4',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host 'R8_TSR1_GPU_RATIO_GATES=PASS'
Write-Host 'R8_TSR1_GPU_PERF=PASS'
