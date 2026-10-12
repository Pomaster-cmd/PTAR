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

$jobs=@(
  @{Name='QSTAGE4'; Source='compat\x86_d3d9\ptar_qstage4diag2_d3d9_ps.hlsl'; Header='build_x86\ptar_qstage4diag2_d3d9_ps_bytecode.h'; Var='g_ptarQstage4Diag2Ps'; Asm='artifact\QSTAGE4_D3D9_SM3_ASM.txt'},
  @{Name='SPEC5HQ900'; Source='compat\x86_d3d9\ptar_spec5_hq900_d3d9_ps.hlsl'; Header='build_x86\ptar_spec5_hq900_d3d9_ps_bytecode.h'; Var='g_ptarSpec5Hq900Ps'; Asm='artifact\SPEC5_HQ900_D3D9_SM3_ASM.txt'}
)
foreach($j in $jobs){
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /Fh $j.Header /Vn $j.Var $j.Source
  if($LASTEXITCODE){throw "$($j.Name) ps_3_0 compile failed"}
  & $fxcPath /nologo /T ps_3_0 /E main /O3 /Fc $j.Asm $j.Source
  if($LASTEXITCODE){throw "$($j.Name) assembly listing failed"}
  $a=Get-Content $j.Asm -Raw
  if($a -notmatch 'ps_3_0'){throw "$($j.Name) missing ps_3_0 marker"}
  $texld=([regex]::Matches($a,'(?im)^\s*texld')).Count
  $slots=0
  $m=[regex]::Match($a,'(?i)approximately\s+(\d+)\s+instruction slots')
  if($m.Success){$slots=[int]$m.Groups[1].Value}
  Write-Host "$($j.Name)_TEXLD=$texld"
  Write-Host "$($j.Name)_INSTRUCTION_SLOTS=$slots"
  if($j.Name -eq 'QSTAGE4' -and $texld -ne 10){throw "QSTAGE4 expected 10 D3D9 texture reads, got $texld"}
  if($j.Name -eq 'SPEC5HQ900' -and $texld -gt 32){throw "SPEC5 exceeds ps_3_0 texture instruction budget: $texld"}
  if($slots -gt 512){throw "$($j.Name) exceeds ps_3_0 arithmetic slot budget: $slots"}
}

$qh=(Get-FileHash build_x86\ptar_qstage4diag2_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant()
$sh=(Get-FileHash build_x86\ptar_spec5_hq900_d3d9_ps_bytecode.h -Algorithm SHA256).Hash.ToLowerInvariant()
@(
 'FULLQUALITY1_SM3_COMPILE=PASS',
 ('QSTAGE4_HEADER_SHA256='+$qh),
 ('SPEC5_HQ900_HEADER_SHA256='+$sh),
 'QSTAGE4_EXPECTED_FETCHES=10',
 'SPEC5_MAX_FETCHES=20_SOURCE_LOADS',
 'PS30_ARITHMETIC_BUDGET=512',
 'PS30_TEXTURE_BUDGET=32') | Set-Content artifact\FULLQUALITY1_SM3_COMPILE_STATUS.txt -Encoding ascii
Write-Host 'FULLQUALITY1_SM3_COMPILE=PASS'
