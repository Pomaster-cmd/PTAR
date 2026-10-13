$ErrorActionPreference='Stop'

# Functional GPU smoke: exact R7 fallback, fractional resolve activation,
# subpixel motion production and explicit reuse of the already-rendered R7 frame.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 lab\tsr1\d3d9_tsr1_gpu_smoke.cpp /link d3d9.lib user32.lib /OUT:artifact\d3d9_tsr1_gpu_smoke.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'TSR1 GPU smoke compile failed'}

& artifact\d3d9_tsr1_gpu_smoke.exe 2>&1 | Tee-Object artifact\R8_TSR1_GPU_SMOKE.txt
if($LASTEXITCODE){throw 'TSR1 GPU smoke execution failed'}
$smoke=Get-Content artifact\R8_TSR1_GPU_SMOKE.txt -Raw
foreach($marker in @(
 'TSR1_ZERO_FALLBACK=PASS',
 'TSR1_FRACTIONAL_PATH=PASS',
 'TSR1_SUBPIXEL_MOTION=PASS',
 'TSR1_R7_REUSE=PASS',
 'D3D9_TSR1_GPU_SMOKE=PASS'))
{
    if($smoke -notmatch [regex]::Escape($marker)){throw "TSR1 GPU smoke marker missing: $marker"}
}

# Quality oracle: compare exact R7 and TSR1 against a continuous synthetic
# native-resolution scene. Known half-pixel X/Y/XY phases must all improve and
# the worst direction must retain a large margin; this prevents future perf
# work from silently collapsing the actual super-resolution benefit.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 lab\tsr1\d3d9_tsr1_quality_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\d3d9_tsr1_quality_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'TSR1 native-reference oracle compile failed'}

& artifact\d3d9_tsr1_quality_oracle.exe 2>&1 | Tee-Object artifact\R8_TSR1_QUALITY_ORACLE.txt
if($LASTEXITCODE){throw 'TSR1 native-reference oracle execution failed'}
$quality=Get-Content artifact\R8_TSR1_QUALITY_ORACLE.txt -Raw
foreach($marker in @(
 'TSR1_NATIVE_X_HALF=PASS',
 'TSR1_NATIVE_Y_HALF=PASS',
 'TSR1_NATIVE_XY_HALF=PASS',
 'TSR1_NATIVE_R7_REUSE=PASS',
 'TSR1_NATIVE_REFERENCE_QUALITY=PASS'))
{
    if($quality -notmatch [regex]::Escape($marker)){throw "TSR1 quality oracle marker missing: $marker"}
}

$worstMatch=[regex]::Match($quality,'TSR1_NATIVE_REFERENCE_WORST_IMPROVEMENT_PCT=([0-9.]+)')
if(-not $worstMatch.Success){throw 'TSR1 worst native-reference improvement missing'}
$worst=[double]::Parse($worstMatch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
if($worst -lt 45.0){throw "TSR1 native-reference quality regression: worst improvement=$worst% (<45%)"}

if(Test-Path artifact\PTAR_R8_VALIDATION.txt)
{
    'TSR1_R7_REUSE_GPU_SMOKE=PASS' | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
    ('TSR1_NATIVE_REFERENCE_WORST_IMPROVEMENT_PCT='+$worst.ToString('F3',[Globalization.CultureInfo]::InvariantCulture)) | Add-Content artifact\PTAR_R8_VALIDATION.txt -Encoding ascii
}

Write-Host "R8_TSR1_NATIVE_WORST_IMPROVEMENT_PCT=$($worst.ToString('F3',[Globalization.CultureInfo]::InvariantCulture))"
Write-Host 'R8_TSR1_GPU_SMOKE=PASS'
Write-Host 'R8_TSR1_NATIVE_REFERENCE_QUALITY=PASS'
