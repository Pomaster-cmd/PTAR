$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$l=Join-Path $S 'LATEST_STATE.txt'
if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim()
$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json
$g=[string]$m.game_root
$bad=0
foreach($r in @(
  @('d3d11.dll','2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d'),
  @('win81_nis_dx11_x64.dll','2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d'),
  @('win81_nis.ini','5f1ee54060411ba5b555378cddd3e6d658366a6c8ecc5e9660343e1ef0d0312b'),
  @('win81_nis_version.txt','23f1dc86bc6e9c0f7863223acf618d2503795c34146e4947efc1795806634eaa')
)){
  $h=Sha (Join-Path $g $r[0])
  if($h -eq $r[1]){Write-Host ('[PASS] '+$r[0])}else{$bad=1;Write-Host ('[FAIL] '+$r[0]+' '+$h)}
}
$t=Get-Content -LiteralPath (Join-Path $g 'win81_nis.ini')
foreach($k in @('TargetExe=Warhammer.exe','Enabled=1','UniversalSpatialPresenter=1','PresenterExclusive=0','Overlay=1','FrameGeneration=0','FrameGenerationPresentSync=2','FrameGenerationTargetFPS=30')){
  if($t -contains $k){Write-Host ('[PASS] '+$k)}else{$bad=1;Write-Host ('[FAIL] '+$k)}
}
if(Test-Path -LiteralPath (Join-Path $PackRoot '_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1')){Write-Host '[PASS] Safe uninstaller engine'}else{$bad=1;Write-Host '[FAIL] Safe uninstaller engine'}

# Systematic diagnostic preflight on the actual Windows 8.1 host. This validates
# the exact path handoff, .NET Framework csc.exe compilation, assembly loading and
# Run method discovery BEFORE the user starts a 20-second measurement.
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
