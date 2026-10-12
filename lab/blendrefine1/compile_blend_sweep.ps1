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

$strengths=@(0,40,60,80,100,120,140,160,180,200,240,280,320)
$rows=@()
foreach($s in $strengths){
  $tag=('B{0:D2}' -f [int]($s/10))
  $header="build_x86\ptar_spec5_blend_$tag.h"
  $asm="artifact\SPEC5_BLEND_${tag}_SM3_ASM.txt"
  $var="g_ptarSpec5Blend$tag"
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /DPTAR_BLEND_PERMILLE=$s /Fh $header /Vn $var lab\blendrefine1\ptar_spec5_blendrefine_ps.hlsl
  if($LASTEXITCODE){throw "$tag compile failed"}
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /DPTAR_BLEND_PERMILLE=$s /Fc $asm lab\blendrefine1\ptar_spec5_blendrefine_ps.hlsl
  if($LASTEXITCODE){throw "$tag asm failed"}
  $a=Get-Content $asm -Raw
  $texld=([regex]::Matches($a,'(?im)^\s*texld')).Count
  $slots=0;$m=[regex]::Match($a,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
  if($texld -ne 20){throw "$tag expected 20 texld got $texld"}
  if($slots -gt 512){throw "$tag exceeds ps_3_0 slots $slots"}
  $rows += [pscustomobject]@{StrengthPermille=$s;Name=$tag;Texld=$texld;Slots=$slots;HeaderSha=(Get-FileHash $header -Algorithm SHA256).Hash.ToLowerInvariant()}
  Write-Host "BLENDREFINE_${tag}_TEXLD=$texld SLOTS=$slots"
}
$rows | ConvertTo-Csv -NoTypeInformation | Set-Content artifact\BLENDREFINE_SM3_BUDGETS.csv -Encoding ascii
'BLENDREFINE_SM3_COMPILE=PASS' | Set-Content artifact\BLENDREFINE_SM3_STATUS.txt -Encoding ascii
