$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force build_x86,artifact | Out-Null
$fxc=Get-Command fxc.exe -ErrorAction SilentlyContinue
if(-not $fxc){
  foreach($root in @("$env:ProgramFiles(x86)\Windows Kits\10\bin","$env:ProgramFiles(x86)\Windows Kits\8.1\bin")){
    if(Test-Path $root){$fxc=Get-ChildItem $root -Recurse -Filter fxc.exe -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1;if($fxc){break}}
  }
}
if(-not $fxc){throw 'fxc.exe not found'}
$fxcPath=if($fxc.Path){$fxc.Path}else{$fxc.FullName}
$src='lab\nativelike1\ptar_spec5_nativelike1_ps.hlsl'
$hdr='build_x86\ptar_spec5_nativelike1_ps_bytecode.h'
$asm='artifact\SPEC5_NATIVELIKE1_SM3_ASM.txt'
$cso='artifact\SPEC5_NATIVELIKE1.cso'
& $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh $hdr /Vn g_ptarSpec5NativeLike1Ps /Fo $cso /Fc $asm $src
if($LASTEXITCODE){throw 'NATIVELIKE1 ps_3_0 compile failed'}
$a=Get-Content $asm -Raw
$texld=([regex]::Matches($a,'(?im)^\s*texld')).Count
$m=[regex]::Match($a,'(?i)approximately\s+(\d+)\s+instruction slots')
$slots=if($m.Success){[int]$m.Groups[1].Value}else{0}
Write-Host "NATIVELIKE1_TEXLD=$texld"
Write-Host "NATIVELIKE1_SLOTS=$slots"
if($texld -ne 20){throw "expected 20 texld, got $texld"}
if($slots -gt 512){throw "ps_3_0 slot budget exceeded: $slots"}
$sha=(Get-FileHash $cso -Algorithm SHA256).Hash.ToLowerInvariant()
@(
 'SPEC5_NATIVELIKE1_COMPILE=PASS',
 ('SPEC5_NATIVELIKE1_TEXLD='+$texld),
 ('SPEC5_NATIVELIKE1_SLOTS='+$slots),
 ('SPEC5_NATIVELIKE1_CSO_SHA256='+$sha),
 'SPEC5_NATIVELIKE1_BASE_GAIN=0.53',
 'SPEC5_NATIVELIKE1_DETAIL_GAIN=0.125',
 'SPEC5_NATIVELIKE1_BILINEAR_BLEND=0.045',
 'SPEC5_NATIVELIKE1_EXTRA_TEXLD=0') | Set-Content artifact\SPEC5_NATIVELIKE1_COMPILE_STATUS.txt -Encoding ascii
Write-Host 'SPEC5_NATIVELIKE1_COMPILE=PASS'
