$ErrorActionPreference='Stop'

& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 /I build_x86 lab\tsr1\d3d9_tsr1_gpu_smoke.cpp /link d3d9.lib user32.lib /OUT:artifact\d3d9_tsr1_gpu_smoke.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'TSR1 GPU smoke compile failed'}

& artifact\d3d9_tsr1_gpu_smoke.exe 2>&1 | Tee-Object artifact\R8_TSR1_GPU_SMOKE.txt
if($LASTEXITCODE){throw 'TSR1 GPU smoke execution failed'}
$smoke=Get-Content artifact\R8_TSR1_GPU_SMOKE.txt -Raw
foreach($marker in @(
 'TSR1_ZERO_FALLBACK=PASS',
 'TSR1_FRACTIONAL_PATH=PASS',
 'TSR1_SUBPIXEL_MOTION=PASS',
 'D3D9_TSR1_GPU_SMOKE=PASS'))
{
    if($smoke -notmatch [regex]::Escape($marker)){throw "TSR1 GPU smoke marker missing: $marker"}
}
Write-Host 'R8_TSR1_GPU_SMOKE=PASS'
