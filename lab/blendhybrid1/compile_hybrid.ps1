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
$names=@('BASE','B12','B18','B20','H08A08','H10A08','H12A06','H12A08')
$rows=@()
for($i=0;$i -lt $names.Count;$i++){
  $n=$names[$i];$h="build_x86\ptar_spec5_hybrid_$n.h";$asm="artifact\SPEC5_HYBRID_${n}_ASM.txt";$var="g_ptarSpec5Hybrid$n"
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /DPTAR_HYBRID_VARIANT=$i /Fh $h /Vn $var lab\blendhybrid1\ptar_spec5_blendhybrid_ps.hlsl
  if($LASTEXITCODE){throw "$n compile failed"}
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /DPTAR_HYBRID_VARIANT=$i /Fc $asm lab\blendhybrid1\ptar_spec5_blendhybrid_ps.hlsl
  if($LASTEXITCODE){throw "$n asm failed"}
  $a=Get-Content $asm -Raw;$texld=([regex]::Matches($a,'(?im)^\s*texld')).Count;$slots=0;$m=[regex]::Match($a,'(?i)approximately\s+(\d+)\s+instruction slots');if($m.Success){$slots=[int]$m.Groups[1].Value}
  if($texld -ne 20){throw "$n expected 20 texld got $texld"};if($slots -gt 512){throw "$n too many slots $slots"}
  $rows += [pscustomobject]@{Id=$i;Name=$n;Texld=$texld;Slots=$slots;HeaderSha=(Get-FileHash $h -Algorithm SHA256).Hash.ToLowerInvariant()}
  Write-Host "HYBRID_${n}_TEXLD=$texld SLOTS=$slots"
}
$rows|ConvertTo-Csv -NoTypeInformation|Set-Content artifact\HYBRID_SM3_BUDGETS.csv -Encoding ascii
'SPEC5_BLENDHYBRID_COMPILE=PASS'|Set-Content artifact\HYBRID_SM3_STATUS.txt -Encoding ascii
