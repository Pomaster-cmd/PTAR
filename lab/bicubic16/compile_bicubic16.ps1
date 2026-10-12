$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force artifact | Out-Null
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
$src='compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl'
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh artifact\ptar_bicubic16_ps_bytecode.h /Vn g_ptarBicubic16Ps /Fo artifact\PTAR_BICUBIC16.cso /Fc artifact\PTAR_BICUBIC16_ASM.txt $src
if($LASTEXITCODE){throw 'Bicubic16 compile failed'}
$asm=Get-Content artifact\PTAR_BICUBIC16_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$slots=0;$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
if($texld -ne 16){throw "Expected 16 texld, got $texld"}
if($slots -le 0 -or $slots -gt 512){throw "Invalid ps_3_0 slot count $slots"}
$sha=(Get-FileHash artifact\PTAR_BICUBIC16.cso -Algorithm SHA256).Hash.ToLowerInvariant()
@(
 'PTAR_BICUBIC16_COMPILE=PASS',
 ('TEXLD='+$texld),
 ('SLOTS='+$slots),
 ('CSO_SHA256='+$sha),
 'KERNEL=CATMULL_ROM_KEYS_A_MINUS_0P5',
 'DETAIL_RESIDUAL_GAIN=0.60',
 'EXTRA_FULLSCREEN_PASS=0') | Set-Content artifact\PTAR_BICUBIC16_COMPILE.txt -Encoding ascii
Copy-Item $src artifact\ptar_bicubic16_ps.hlsl -Force
Write-Host "BICUBIC16_TEXLD=$texld"
Write-Host "BICUBIC16_SLOTS=$slots"
Write-Host "BICUBIC16_CSO_SHA256=$sha"
Write-Host 'PTAR_BICUBIC16_COMPILE=PASS'
