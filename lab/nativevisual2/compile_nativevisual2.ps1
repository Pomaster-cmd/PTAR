$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force artifact | Out-Null
$fxc=Get-Command fxc.exe -ErrorAction Stop
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\SPEC5_NATIVEVISUAL2_PRODUCTION.cso /Fc artifact\SPEC5_NATIVEVISUAL2_PRODUCTION_ASM.txt compat\x86_d3d9\ptar_spec5_d3d9_ps.hlsl
if($LASTEXITCODE){throw 'production compile failed'}
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fo artifact\SPEC5_NATIVEVISUAL2_REFERENCE.cso lab\nativevisual2\ptar_spec5_nativevisual2_ps.hlsl
if($LASTEXITCODE){throw 'reference compile failed'}
$asm=Get-Content artifact\SPEC5_NATIVEVISUAL2_PRODUCTION_ASM.txt -Raw
$texld=([regex]::Matches($asm,'(?im)^\s*texld')).Count
$slots=0;$m=[regex]::Match($asm,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
$prod=(Get-FileHash artifact\SPEC5_NATIVEVISUAL2_PRODUCTION.cso -Algorithm SHA256).Hash.ToLowerInvariant()
$ref=(Get-FileHash artifact\SPEC5_NATIVEVISUAL2_REFERENCE.cso -Algorithm SHA256).Hash.ToLowerInvariant()
if($prod -ne $ref){throw "DXBC mismatch prod=$prod ref=$ref"}
if($texld -ne 20){throw "expected 20 texld got $texld"}
@("TEXLD=$texld","SLOTS=$slots","CSO_SHA256=$prod","DXBC_IDENTITY=PASS") | Set-Content artifact\NATIVEVISUAL2_COMPILE.txt -Encoding ascii
Write-Host "NATIVEVISUAL2_TEXLD=$texld"
Write-Host "NATIVEVISUAL2_SLOTS=$slots"
Write-Host "NATIVEVISUAL2_CSO_SHA256=$prod"
Write-Host 'NATIVEVISUAL2_COMPILE=PASS'
