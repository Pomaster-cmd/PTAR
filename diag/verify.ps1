$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$ExpectedRuntime='e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429'
$ExpectedIni='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50'
$ExpectedVersion='7ea6a7a47b463d780aeb752c7a8b9c1ea74db61336f8a44f6e1799fd8439d2fc'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$l=Join-Path $S 'LATEST_STATE.txt'
if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim()
$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json
$g=[string]$m.game_root
$bad=0
foreach($r in @(
  @('d3d11.dll',$ExpectedRuntime),
  @('win81_nis_dx11_x64.dll',$ExpectedRuntime),
  @('win81_nis_version.txt',$ExpectedVersion)
)){
  $h=Sha (Join-Path $g $r[0])
  if($h -eq $r[1]){Write-Host ('[PASS] '+$r[0])}else{$bad=1;Write-Host ('[FAIL] '+$r[0]+' '+$h)}
}
# The package INI is hash-locked. The installed INI may differ ONLY by the supported VBlankDiagnostics 0/1 marker toggle.
$payloadIni=Join-Path $PackRoot 'payload\win81_nis.ini'
$gameIni=Join-Path $g 'win81_nis.ini'
if((Sha $payloadIni)-ne $ExpectedIni){$bad=1;Write-Host '[FAIL] Payload INI hash mismatch'}
elseif(-not(Test-Path -LiteralPath $gameIni -PathType Leaf)){$bad=1;Write-Host '[FAIL] win81_nis.ini absent'}
else{
  $base=[IO.File]::ReadAllLines($payloadIni)
  $live=[IO.File]::ReadAllLines($gameIni)
  $baseNorm=New-Object System.Collections.Generic.List[string]
  $liveNorm=New-Object System.Collections.Generic.List[string]
  $liveMarkerCount=0;$liveMarker=-1
  foreach($line in $base){if($line -match '^[ \t]*VBlankDiagnostics[ \t]*='){$baseNorm.Add('VBlankDiagnostics=<SUPPORTED_TOGGLE>')}else{$baseNorm.Add($line)}}
  foreach($line in $live){
    if($line -match '^[ \t]*VBlankDiagnostics[ \t]*=[ \t]*([01])[ \t]*$'){$liveMarkerCount++;$liveMarker=[int]$matches[1];$liveNorm.Add('VBlankDiagnostics=<SUPPORTED_TOGGLE>')}
    elseif($line -match '^[ \t]*VBlankDiagnostics[ \t]*='){$liveMarkerCount++;$liveNorm.Add($line)}
    else{$liveNorm.Add($line)}
  }
  $a=[string]::Join("`n",$baseNorm.ToArray());$b=[string]::Join("`n",$liveNorm.ToArray())
  if($liveMarkerCount -eq 1 -and ($liveMarker -eq 0 -or $liveMarker -eq 1) -and $a -ceq $b){Write-Host ('[PASS] win81_nis.ini (marker toggle supported, VBlankDiagnostics='+$liveMarker+')')}
  else{$bad=1;Write-Host '[FAIL] win81_nis.ini differs beyond the supported VBlankDiagnostics=0/1 toggle'}
}
$t=Get-Content -LiteralPath $gameIni -ErrorAction SilentlyContinue
foreach($k in @('TargetExe=Warhammer.exe','Enabled=1','UniversalSpatialPresenter=1','PresenterExclusive=0','Overlay=1','FrameGeneration=0','FrameGenerationPresentSync=1','FrameGenerationTargetFPS=60')){
  if($t -contains $k){Write-Host ('[PASS] '+$k)}else{$bad=1;Write-Host ('[FAIL] '+$k)}
}
if(Test-Path -LiteralPath (Join-Path $PackRoot '_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1')){Write-Host '[PASS] Safe uninstaller engine'}else{$bad=1;Write-Host '[FAIL] Safe uninstaller engine'}

# Systematic diagnostic preflight on the actual Windows 8.1 host.
$runner=Join-Path $PackRoot 'diag\visible_pacing\run_single_engine_verifier.ps1'
if(Test-Path -LiteralPath $runner -PathType Leaf){
  try{
    $env:PTAR_GAME_ROOT=[IO.Path]::GetFullPath($g)
    $psExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $pre=@(& $psExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $runner -PreflightOnly 2>&1)
    $preRc=$LASTEXITCODE
    foreach($line in $pre){Write-Host $line}
    if($preRc -eq 0){Write-Host '[PASS] VBLANK3 pre-flight cible Windows/PS4'}else{$bad=1;Write-Host ('[FAIL] VBLANK3 pre-flight code '+$preRc)}
  }catch{
    $bad=1
    Write-Host ('[FAIL] VBLANK3 pre-flight exception : '+$_.Exception.Message)
  }
}else{
  $bad=1
  Write-Host '[FAIL] Runner VBLANK3 absent'
}

if($bad){exit 9}else{Write-Host 'VERIFY=PASS';exit 0}
