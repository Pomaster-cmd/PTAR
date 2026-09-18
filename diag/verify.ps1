$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$ExpectedRuntime='bc291f0f91013df7a28630ffef44983856fce6eb71d79aca597ab292012165e0'
$ExpectedIniTemplate='dea8a93e97d3ad1b438973822d67ca9ac12477b5774933eea135ab71776c5648'
$ExpectedVersion='5eb6553059f675ba1a05f556b06f74dbd07b2fb11e1f1bc91be738057b92c0d2'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$l=Join-Path $S 'LATEST_STATE.txt'
if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim()
$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json
if((-not $m.schema) -or ([int]$m.schema -lt 4)){Write-Host '[FAIL] Etat installation non UNIVERSAL1';exit 2}
$g=[string]$m.game_root
$targetExe=[string]$m.target_exe
$targetName=[string]$m.target_exe_name
$bad=0
if(-not(Test-Path -LiteralPath $targetExe -PathType Leaf)){$bad=1;Write-Host ('[FAIL] Executable cible absent : '+$targetExe)}else{Write-Host ('[PASS] TargetExe='+$targetName)}
foreach($r in @(@('d3d11.dll',$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$ExpectedRuntime),@('win81_nis_version.txt',$ExpectedVersion))){
 $h=Sha (Join-Path $g $r[0])
 if($h -eq $r[1]){Write-Host ('[PASS] '+$r[0])}else{$bad=1;Write-Host ('[FAIL] '+$r[0]+' '+$h)}
}
$payloadIni=Join-Path $PackRoot 'payload\win81_nis.ini'
$gameIni=Join-Path $g 'win81_nis.ini'
if((Sha $payloadIni)-ne $ExpectedIniTemplate){$bad=1;Write-Host '[FAIL] Payload INI template hash mismatch'}
elseif(-not(Test-Path -LiteralPath $gameIni -PathType Leaf)){$bad=1;Write-Host '[FAIL] win81_nis.ini absent'}
else{
 $ExpectedDiagnostics=$(if($targetName -ieq 'SatGat-Win64-Shipping.exe'){1}else{0})
 $base=[IO.File]::ReadAllLines($payloadIni)
 $live=[IO.File]::ReadAllLines($gameIni)
 $baseNorm=New-Object System.Collections.Generic.List[string]
 $liveNorm=New-Object System.Collections.Generic.List[string]
 $baseTarget=0;$liveTarget=0;$liveMarkerCount=0;$liveMarker=-1;$liveDiagCount=0;$liveDiag=-1
 foreach($line in $base){
  if($line -match '^[ \t]*TargetExe[ \t]*='){$baseTarget++;$baseNorm.Add('TargetExe='+$targetName)}
  elseif($line -match '^[ \t]*Diagnostics[ \t]*='){$baseNorm.Add('Diagnostics=<TARGET_PROFILE>')}
  elseif($line -match '^[ \t]*VBlankDiagnostics[ \t]*='){$baseNorm.Add('VBlankDiagnostics=<SUPPORTED_TOGGLE>')}
  else{$baseNorm.Add($line)}
 }
 foreach($line in $live){
  if($line -match '^[ \t]*TargetExe[ \t]*=[ \t]*(.+?)[ \t]*$'){$liveTarget++;$liveNorm.Add('TargetExe='+$matches[1])}
  elseif($line -match '^[ \t]*Diagnostics[ \t]*=[ \t]*([01])[ \t]*$'){$liveDiagCount++;$liveDiag=[int]$matches[1];$liveNorm.Add('Diagnostics=<TARGET_PROFILE>')}
  elseif($line -match '^[ \t]*VBlankDiagnostics[ \t]*=[ \t]*([01])[ \t]*$'){$liveMarkerCount++;$liveMarker=[int]$matches[1];$liveNorm.Add('VBlankDiagnostics=<SUPPORTED_TOGGLE>')}
  elseif($line -match '^[ \t]*VBlankDiagnostics[ \t]*='){$liveMarkerCount++;$liveNorm.Add($line)}
  else{$liveNorm.Add($line)}
 }
 $a=[string]::Join("`n",$baseNorm.ToArray());$b=[string]::Join("`n",$liveNorm.ToArray())
 if($baseTarget -eq 1 -and $liveTarget -eq 1 -and $liveDiagCount -eq 1 -and $liveDiag -eq $ExpectedDiagnostics -and $liveMarkerCount -eq 1 -and ($liveMarker -eq 0 -or $liveMarker -eq 1) -and $a -ceq $b){Write-Host ('[PASS] win81_nis.ini dynamic target + SatGat profile + marker toggle supported')}else{$bad=1;Write-Host ('[FAIL] win81_nis.ini diff non autorisee / Diagnostics attendu='+$ExpectedDiagnostics+' obtenu='+$liveDiag)}
}
$t=Get-Content -LiteralPath $gameIni -ErrorAction SilentlyContinue
foreach($k in @((('TargetExe=' + $targetName)),'Enabled=1','UniversalSpatialPresenter=1','PresenterExclusive=0','Overlay=1','FrameGeneration=0','FrameGenerationPresentSync=1','FrameGenerationTargetFPS=60')){
 if($t -contains $k){Write-Host ('[PASS] '+$k)}else{$bad=1;Write-Host ('[FAIL] '+$k)}
}
if(Test-Path -LiteralPath (Join-Path $PackRoot '_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1')){Write-Host '[PASS] Safe uninstaller engine'}else{$bad=1;Write-Host '[FAIL] Safe uninstaller engine'}
$runner=Join-Path $PackRoot 'diag\visible_pacing\run_single_engine_verifier.ps1'
if(Test-Path -LiteralPath $runner -PathType Leaf){
 try{
  $env:PTAR_GAME_ROOT=[IO.Path]::GetFullPath($g)
  $env:PTAR_TARGET_EXE=[IO.Path]::GetFullPath($targetExe)
  $psExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $pre=@(& $psExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $runner -PreflightOnly 2>&1)
  $preRc=$LASTEXITCODE
  foreach($line in $pre){Write-Host $line}
  if($preRc -eq 0){Write-Host '[PASS] VBLANK3 pre-flight cible Windows/PS4'}else{$bad=1;Write-Host ('[FAIL] VBLANK3 pre-flight code '+$preRc)}
 }catch{$bad=1;Write-Host ('[FAIL] VBLANK3 pre-flight exception : '+$_.Exception.Message)}
}else{$bad=1;Write-Host '[FAIL] Runner VBLANK3 absent'}
if($bad){exit 9}else{Write-Host 'VERIFY=PASS';exit 0}
