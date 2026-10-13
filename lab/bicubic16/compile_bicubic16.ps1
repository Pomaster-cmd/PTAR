$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force artifact | Out-Null
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
$src='compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl'
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh artifact\ptar_bicubic16_ps_bytecode.h /Vn g_ptarBicubic16Ps /Fo artifact\PTAR_BICUBIC16.cso /Fc artifact\PTAR_BICUBIC16_ASM.txt $src
if($LASTEXITCODE){throw 'Bicubic16 R3 compile failed'}
$asm=Get-Content artifact\PTAR_BICUBIC16_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$slots=0;$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
if($texld -ne 16){throw "Expected 16 texld, got $texld"}
if($slots -ne 154){throw "Expected 154 slots, got $slots"}
$sha=(Get-FileHash artifact\PTAR_BICUBIC16.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($sha -ne '134f513ad18af35336c8881ec0504881a51e0fadc9f311dfa50646e4e878cc46'){throw "Unexpected Bic16 R3 CSO $sha"}
@(
 'PTAR_BICUBIC16_R3_COMPILE=PASS',
 ('TEXLD='+$texld),
 ('SLOTS='+$slots),
 ('CSO_SHA256='+$sha),
 'KERNEL=CATMULL_ROM_KEYS_A_MINUS_0P5',
 'DETAIL_RESIDUAL_GAIN=1.10',
 'LOCAL_CLAMP_MARGIN=0.10',
 'LOCAL_CLAMP_SOURCE=CENTRAL_2X2_MINMAX',
 'EXTRA_FULLSCREEN_PASS=0') | Set-Content artifact\PTAR_BIC16_R3_COMPILE.txt -Encoding ascii
Copy-Item $src artifact\ptar_bic16_r3_ps.hlsl -Force
Write-Host "BIC16_R3_TEXLD=$texld"
Write-Host "BIC16_R3_SLOTS=$slots"
Write-Host "BIC16_R3_CSO_SHA256=$sha"
Write-Host 'PTAR_BICUBIC16_R3_COMPILE=PASS'
