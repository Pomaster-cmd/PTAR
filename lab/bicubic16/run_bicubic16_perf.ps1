$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force artifact,build_x86 | Out-Null
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
$base=Join-Path $env:RUNNER_TEMP 'ptar_spec5_fullquality1_base.hlsl'
$baseText=git show 'origin/lab/d3d9-ptar-fullquality1-20261012:compat/x86_d3d9/ptar_spec5_d3d9_ps.hlsl'
if($LASTEXITCODE){throw 'cannot retrieve canonical FULLQUALITY1 SPEC5'}
Set-Content $base $baseText -Encoding utf8
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_base_perf.h /Vn g_ptarSpec5BasePerf /Fo artifact\SPEC5_BASE_PERF.cso $base
if($LASTEXITCODE){throw 'SPEC5 base perf compile failed'}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_bicubic16_perf.h /Vn g_ptarBicubic16Perf /Fo artifact\BICUBIC16_PERF.cso compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'Bicubic16 perf compile failed'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\bicubic16\bicubic16_perf_gpu.cpp /link d3d9.lib user32.lib /OUT:artifact\bicubic16_perf_gpu.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'Bicubic16 perf harness compile failed'}
& artifact\bicubic16_perf_gpu.exe 2>&1 | Tee-Object artifact\BICUBIC16_PERF.txt
if($LASTEXITCODE){throw 'Bicubic16 perf harness failed'}
if((Get-Content artifact\BICUBIC16_PERF.txt -Raw) -notmatch 'BIC16_PERF_GATE=PASS'){throw 'Bicubic16 perf PASS marker missing'}
Write-Host 'BICUBIC16_PERF=PASS'
