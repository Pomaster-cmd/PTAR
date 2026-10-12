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

$variants=@(
  @{Id=0;Name='BASE'},
  @{Id=1;Name='B08'},
  @{Id=2;Name='B12'},
  @{Id=3;Name='BADAPT20'},
  @{Id=4;Name='AA0_15'},
  @{Id=5;Name='AA2_15'},
  @{Id=6;Name='AA2B'},
  @{Id=7;Name='AA4B'},
  @{Id=8;Name='AA2B_STRONG'}
)

$rows=@()
foreach($v in $variants){
  $header="build_x86\ptar_spec5_aalab_v$($v.Id).h"
  $asm="artifact\SPEC5_AALAB_V$($v.Id)_$($v.Name)_SM3_ASM.txt"
  $var="g_ptarSpec5AaLabV$($v.Id)"
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /DPTAR_VARIANT=$($v.Id) /Fh $header /Vn $var lab\aalab1\ptar_spec5_aalab_ps.hlsl
  if($LASTEXITCODE){throw "variant $($v.Id) compile failed"}
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /DPTAR_VARIANT=$($v.Id) /Fc $asm lab\aalab1\ptar_spec5_aalab_ps.hlsl
  if($LASTEXITCODE){throw "variant $($v.Id) asm failed"}
  $a=Get-Content $asm -Raw
  $texld=([regex]::Matches($a,'(?im)^\s*texld')).Count
  $slots=0
  $m=[regex]::Match($a,'(?i)approximately\s+(\d+)\s+instruction slots')
  if($m.Success){$slots=[int]$m.Groups[1].Value}
  if($texld -gt 32){throw "variant $($v.Id) exceeds texture budget: $texld"}
  if($slots -gt 512){throw "variant $($v.Id) exceeds instruction budget: $slots"}
  $rows += [pscustomobject]@{Id=$v.Id;Name=$v.Name;Texld=$texld;Slots=$slots;HeaderSha=(Get-FileHash $header -Algorithm SHA256).Hash.ToLowerInvariant()}
  Write-Host "AALAB_VARIANT_$($v.Id)_$($v.Name)_TEXLD=$texld SLOTS=$slots"
}

$rows | ConvertTo-Csv -NoTypeInformation | Set-Content artifact\AALAB_SM3_BUDGETS.csv -Encoding ascii
$base=$rows | Where-Object Id -eq 0
foreach($r in $rows){
  $texDelta=$r.Texld-$base.Texld
  $slotDelta=$r.Slots-$base.Slots
  Add-Content artifact\AALAB_SM3_SUMMARY.txt "V$($r.Id) $($r.Name) texld=$($r.Texld) deltaTexld=$texDelta slots=$($r.Slots) deltaSlots=$slotDelta"
}
'PTAR_SPEC5_AALAB_SM3_COMPILE=PASS' | Add-Content artifact\AALAB_SM3_SUMMARY.txt
Write-Host 'PTAR_SPEC5_AALAB_SM3_COMPILE=PASS'
