$ErrorActionPreference='Stop'
$Here=$PSScriptRoot
$PackRoot=Split-Path -Parent $Here
$Payload=Join-Path $Here 'payload\ptar_rc41.dll'
$StateDir=Join-Path $Here 'state'
$StateFile=Join-Path $StateDir 'RC55_STATE.json'
$ExpectedRC55='271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949'
$ExpectedRC54='dc23f3bbb2780b92809d5ae218d5cf42186682985e1be7608e7e9ee812ddb80e'
$ExpectedRC52='fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d'
$ExpectedRuntime='6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b'
$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'
$ExpectedIni='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50'
$ExpectedVersion='51367c26ebe9b6a35147c49aea8b227ae23219fc43166c86638b1e1b09f0a1d9'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function Fail([string]$s,[int]$c){Write-Host ('[FAIL] '+$s);exit $c}
function Resolve-GameRoot{
 if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot}
 $p=Split-Path -Parent $PackRoot
 if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p}
 return $null
}
function Read-Rc53State([string]$gameRoot){
 $candidates=New-Object System.Collections.ArrayList
 [void]$candidates.Add($PackRoot);[void]$candidates.Add($gameRoot)
 try{Get-ChildItem -LiteralPath $gameRoot | Where-Object {$_.PSIsContainer} | ForEach-Object {[void]$candidates.Add($_.FullName)}}catch{}
 $best=$null;$bestTime=[datetime]::MinValue
 foreach($root in $candidates){
  $latest=Join-Path $root '_PTAR_UNINSTALL\state\LATEST_STATE.txt'
  if(-not(Test-Path -LiteralPath $latest -PathType Leaf)){continue}
  try{
   $statePath=(Get-Content -LiteralPath $latest -TotalCount 1).Trim()
   $manifest=Join-Path $statePath 'install_state.json'
   if(-not(Test-Path -LiteralPath $manifest -PathType Leaf)){continue}
   $m=Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json
   if(([string]$m.package) -ne 'PTAR_RC53_GUI_RESOLUTION_SYNC'){continue}
   if(-not(($m.PSObject.Properties.Name -contains 'gui') -and $m.gui -and $m.gui.applied)){continue}
   $t=(Get-Item -LiteralPath $manifest).LastWriteTime
   if($t -gt $bestTime){$best=$m;$bestTime=$t}
  }catch{}
 }
 return $best
}
function Restore-Rc53GuiIfOwned([string]$gameRoot){
 $result=[ordered]@{found=$false;changed=$false;before='UNAVAILABLE';after='UNAVAILABLE'}
 $key='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options'
 try{$result.before=[string]([int](Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution)}catch{$result.before='ABSENT'}
 $m=Read-Rc53State $gameRoot
 if(-not $m){$result.after=$result.before;return $result}
 $result.found=$true
 if(-not(Test-Path -LiteralPath $key)){$result.after=$result.before;return $result}
 try{
  $cur=[int](Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution
  if($cur -eq [int]$m.gui.installed){
   if([bool]$m.gui.exists){Set-ItemProperty -LiteralPath $key -Name AffectGuiResolution -Type DWord -Value ([int]$m.gui.original)}
   else{Remove-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop}
   $result.changed=$true
  }
 }catch{}
 try{$result.after=[string]([int](Get-ItemProperty -LiteralPath $key -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution)}catch{$result.after='ABSENT'}
 return $result
}
$g=Resolve-GameRoot
if(-not $g){Fail 'Warhammer.exe introuvable. Extraire le pack dans le dossier du jeu ou dans un sous-dossier direct.' 2}
if(Get-Process Warhammer -ErrorAction SilentlyContinue){Fail 'Fermer Warhammer avant installation.' 3}
if((Sha $Payload)-ne $ExpectedRC55){Fail 'Payload RC55 invalide.' 10}
$checks=@(
 @('d3d11.dll',$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$ExpectedRuntime),@('win81_nis.ini',$ExpectedIni),
 @('win81_nis_version.txt',$ExpectedVersion),@('ptar_borderless.dll',$ExpectedBorderless)
)
foreach($c in $checks){$p=Join-Path $g $c[0];$h=Sha $p;if($h -ne $c[1]){Fail ('Base RC52/RC51 non conforme: '+$c[0]+' '+$h) 20}}
$dst=Join-Path $g 'ptar_rc41.dll';$current=Sha $dst
if(($current -ne $ExpectedRC54) -and ($current -ne $ExpectedRC52) -and ($current -ne $ExpectedRC55)){Fail ('ptar_rc41.dll inattendue: '+$current+'. RC54 terrain ou RC52 exact requis.') 21}
New-Item -ItemType Directory -Path $StateDir -Force|Out-Null
if($current -ne $ExpectedRC55){
 $pre=Join-Path $StateDir 'pre_rc55_ptar_rc41.dll'
 Copy-Item -LiteralPath $dst -Destination $pre -Force
 if((Sha $pre)-ne $current){Fail 'Sauvegarde pre-RC55 incoherente.' 22}
}
$gui=Restore-Rc53GuiIfOwned $g
$state=[ordered]@{schema=1;package='PTAR_RC55_BOUND_PHYSICAL_IDEMPOTENCE';game_root=$g;preinstall_raster_sha=$current;rc55_sha=$ExpectedRC55;rc54_sha=$ExpectedRC54;rc52_sha=$ExpectedRC52;rc53_gui_state_found=$gui.found;rc53_gui_restored=$gui.changed;affect_gui_before=$gui.before;affect_gui_after=$gui.after;installed_at=(Get-Date).ToString('o')}
$state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $StateFile -Encoding UTF8
Copy-Item -LiteralPath $Payload -Destination $dst -Force
if((Sha $dst)-ne $ExpectedRC55){
 $pre=Join-Path $StateDir 'pre_rc55_ptar_rc41.dll'
 if(Test-Path -LiteralPath $pre -PathType Leaf){Copy-Item -LiteralPath $pre -Destination $dst -Force}
 Fail 'Verification post-install RC55 echouee; etat precedent restaure si disponible.' 30
}
Write-Host '[PASS] RC55 bound-physical idempotence installee.'
Write-Host ('GAME_ROOT='+$g)
Write-Host ('PREINSTALL_RASTER_SHA256='+$current)
Write-Host ('RC55_SHA256='+$ExpectedRC55)
Write-Host ('RC53_STATE_FOUND='+$gui.found)
Write-Host ('RC53_GUI_RESTORED='+$gui.changed)
Write-Host ('AFFECT_GUI_BEFORE='+$gui.before)
Write-Host ('AFFECT_GUI_AFTER='+$gui.after)
Write-Host '[INFO] Correction ciblee: un viewport/scissor plein ecran deja physique 1280x720 n est plus remultiplie par 2/3.'
exit 0
