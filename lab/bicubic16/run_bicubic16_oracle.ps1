$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force artifact,build_x86 | Out-Null
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh build_x86\ptar_spec5_d3d9_ps_bytecode.h /Vn g_ptarBicubic16Ps /Fo artifact\PTAR_BICUBIC16_ORACLE.cso compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'Bicubic16 oracle shader compile failed'}
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\bicubic16\bicubic16_gpu_oracle.cpp /link d3d9.lib user32.lib /OUT:artifact\bicubic16_gpu_oracle.exe /SUBSYSTEM:CONSOLE,6.03
if($LASTEXITCODE){throw 'Bicubic16 oracle compile failed'}
& artifact\bicubic16_gpu_oracle.exe 2>&1 | Tee-Object artifact\BICUBIC16_GPU_ORACLE.txt
if($LASTEXITCODE){throw 'Bicubic16 GPU oracle failed'}
if((Get-Content artifact\BICUBIC16_GPU_ORACLE.txt -Raw) -notmatch 'BICUBIC16_PIXEL_PARITY=PASS'){throw 'Bicubic16 parity PASS marker missing'}
Write-Host 'BICUBIC16_GPU_ORACLE=PASS'
