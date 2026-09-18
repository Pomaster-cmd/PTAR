param([string]$GameExe='')
$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$ExpectedRuntime='bc291f0f91013df7a28630ffef44983856fce6eb71d79aca597ab292012165e0'
$SessionRoot=Join-Path $PackRoot '_CLICK_INPUT_DIAG'
$Latest=Join-Path $SessionRoot 'LATEST_SESSION.txt'

function Fail([string]$Message,[int]$Code=90){Write-Host ('[FAIL] '+$Message);exit $Code}
function Full([string]$Path){return [IO.Path]::GetFullPath($Path.Trim().Trim('"'))}
function Sha([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Is-TargetRunning([string]$ExePath){
  $want=Full $ExePath
  foreach($p in @(Get-Process -ErrorAction SilentlyContinue)){
    try{if((Full $p.MainModule.FileName) -ieq $want){return $true}}catch{}
  }
  return $false
}
function Resolve-Target([string]$Requested){
  $candidates=New-Object Collections.Generic.List[string]
  if(-not [string]::IsNullOrWhiteSpace($Requested)){$candidates.Add($Requested)}
  if(-not [string]::IsNullOrWhiteSpace([string]$env:PTAR_GAME_EXE)){$candidates.Add([string]$env:PTAR_GAME_EXE)}
  foreach($name in @('PTAR_TARGET_EXE.txt','win81_nis_install_exe.txt')){
    $p=Join-Path $PackRoot $name
    if(Test-Path -LiteralPath $p -PathType Leaf){$v=(Get-Content -LiteralPath $p -TotalCount 1).Trim();if($v){$candidates.Add($v)}}
  }
  foreach($raw in $candidates){
    try{$p=Full $raw;if(Test-Path -LiteralPath $p -PathType Leaf){return $p}}catch{}
  }
  return $null
}

$TargetExe=Resolve-Target $GameExe
if(-not $TargetExe){Fail 'Executable cible introuvable. Glisse le .exe du jeu sur 01-ENABLE_CLICK_DIAGNOSTICS.bat ou renseigne PTAR_TARGET_EXE.txt.' 2}
if(Is-TargetRunning $TargetExe){Fail ('Ferme le jeu avant activation du diagnostic : '+$TargetExe) 3}
$GameRoot=Split-Path -Parent $TargetExe
$DllA=Join-Path $GameRoot 'd3d11.dll'
$DllB=Join-Path $GameRoot 'win81_nis_dx11_x64.dll'
$Ini=Join-Path $GameRoot 'win81_nis.ini'
$Log=Join-Path $GameRoot 'win81_nis.log'
foreach($p in @($DllA,$DllB,$Ini)){if(-not(Test-Path -LiteralPath $p -PathType Leaf)){Fail ('Fichier PTAR installe manquant : '+$p) 4}}
$ha=Sha $DllA;$hb=Sha $DllB
if($ha -ne $ExpectedRuntime -or $hb -ne $ExpectedRuntime){Fail ('Runtime inattendu. Attendu GW16I_SLATEABS1 '+$ExpectedRuntime+' ; d3d11='+$ha+' mirror='+$hb) 5}

New-Item -ItemType Directory -Path $SessionRoot -Force | Out-Null
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$Session=Join-Path $SessionRoot ('SESSION_'+$stamp)
New-Item -ItemType Directory -Path $Session -Force | Out-Null
$BackupIni=Join-Path $Session 'win81_nis.ini.original'
Copy-Item -LiteralPath $Ini -Destination $BackupIni -Force
$OriginalIniSha=Sha $BackupIni
try{
  $OriginalBytes=[IO.File]::ReadAllBytes($Ini)
  $OriginalText=[Text.Encoding]::ASCII.GetString($OriginalBytes)
  $lines=$OriginalText -split "`r?`n",-1
  $inputCount=0;$diagCount=0;$diagIndex=-1;$inInput=$false
  for($i=0;$i -lt $lines.Length;$i++){
    $line=$lines[$i]
    if($line -match '^\s*\[INPUT\]\s*$'){$inputCount++;$inInput=$true;continue}
    if($line -match '^\s*\[[^\]]+\]\s*$'){$inInput=$false;continue}
    if($inInput -and $line -match '^\s*Diagnostics\s*='){$diagCount++;$diagIndex=$i}
  }
  if($inputCount -ne 1 -or $diagCount -ne 1){throw ('INI invalide pour diagnostic : INPUT sections='+$inputCount+' Diagnostics keys='+$diagCount)}
  $beforeLine=$lines[$diagIndex]
  $lines[$diagIndex]='Diagnostics=1'
  $DiagText=[string]::Join("`r`n",$lines)
  $tmp=$Ini+'.slatecapdiag1.tmp'
  [IO.File]::WriteAllText($tmp,$DiagText,[Text.Encoding]::ASCII)
  Move-Item -LiteralPath $tmp -Destination $Ini -Force
  $DiagIniSha=Sha $Ini
  $check=[IO.File]::ReadAllText($Ini,[Text.Encoding]::ASCII)
  if($check -notmatch '(?ms)^\[INPUT\]\s*.*?^Diagnostics=1\s*$'){throw 'Diagnostics=1 non confirme apres ecriture.'}

  $BaselineLogBytes=0L
  if(Test-Path -LiteralPath $Log -PathType Leaf){$BaselineLogBytes=(Get-Item -LiteralPath $Log).Length}
  $meta=[ordered]@{
    schema=1
    diagnostic='CLICK_INPUT_DIAG'
    created_local=(Get-Date).ToString('o')
    target_exe=$TargetExe
    game_root=$GameRoot
    runtime_sha256=$ExpectedRuntime
    original_ini_sha256=$OriginalIniSha
    diagnostic_ini_sha256=$DiagIniSha
    original_diagnostics_line=$beforeLine
    baseline_log_bytes=$BaselineLogBytes
    backup_ini=$BackupIni
  }
  $meta|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Session 'session.json') -Encoding UTF8
  Set-Content -LiteralPath $Latest -Value $Session -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $PackRoot 'PTAR_TARGET_EXE.txt') -Value $TargetExe -Encoding UTF8
}catch{
  try{Copy-Item -LiteralPath $BackupIni -Destination $Ini -Force}catch{}
  throw
}
Write-Host '[PASS] CLICK_INPUT_DIAG active.'
Write-Host ('[INFO] Runtime confirme : '+$ExpectedRuntime)
Write-Host ('[INFO] INI original sauvegarde : '+$OriginalIniSha)
Write-Host ('[INFO] Baseline log bytes : '+$BaselineLogBytes)
Write-Host '[ACTION] Lance le jeu normalement, reproduis le clic souris qui reagit visuellement mais n execute pas l action, puis appuie UNE FOIS sur F8 juste apres.'
Write-Host '[ACTION] Ferme ensuite le jeu normalement et lance 02-COLLECT_AND_RESTORE_CLICK_DIAGNOSTICS.bat.'
exit 0
