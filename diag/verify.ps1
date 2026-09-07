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
  @('d3d11.dll','864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'),
  @('win81_nis_dx11_x64.dll','864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c'),
  @('win81_nis.ini','bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50'),
  @('win81_nis_version.txt','8c15a4ab74222f1efc7305315bf25dd06903f45bd568abdd3a04d152501e0c51')
)){
  $h=Sha (Join-Path $g $r[0])
  if($h -eq $r[1]){Write-Host ('[PASS] '+$r[0])}else{$bad=1;Write-Host ('[FAIL] '+$r[0]+' '+$h)}
}
$t=Get-Content -LiteralPath (Join-Path $g 'win81_nis.ini')
foreach($k in @('TargetExe=Warhammer.exe','Enabled=1','UniversalSpatialPresenter=1','PresenterExclusive=0','Overlay=1','FrameGeneration=0','FrameGenerationPresentSync=1','FrameGenerationTargetFPS=60')){
  if($t -contains $k){Write-Host ('[PASS] '+$k)}else{$bad=1;Write-Host ('[FAIL] '+$k)}
}
if(Test-Path -LiteralPath (Join-Path $PackRoot '_PTAR_UNINSTALL\PTAR_SAFE_UNINSTALL.ps1')){Write-Host '[PASS] Safe uninstaller engine'}else{$bad=1;Write-Host '[FAIL] Safe uninstaller engine'}

# Systematic diagnostic preflight on the actual Windows 8.1 host. This validates
# the exact path handoff, .NET Framework csc.exe compilation, dedicated EXE startup, DPI mode and
# marker-engine self-test BEFORE the user starts a 20-second measurement.
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
