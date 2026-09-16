param([int]$Value=-1,[switch]$RequireEnabled)
$ErrorActionPreference='Stop'
function Fail([string]$Message,[int]$Code){Write-Host ('[ERREUR] '+$Message);exit $Code}
if($Value -ne -1 -and $Value -ne 0 -and $Value -ne 1){Fail ('Valeur diagnostic invalide : '+$Value) 9}
$diagDir=Split-Path -Parent $MyInvocation.MyCommand.Path;$root=[IO.Path]::GetFullPath((Join-Path $diagDir '..'));$target=$null
$latest=Join-Path $root '_PTAR_UNINSTALL\state\LATEST_STATE.txt'
if(Test-Path -LiteralPath $latest -PathType Leaf){
 $s=(Get-Content -LiteralPath $latest -TotalCount 1).Trim();$mp=Join-Path $s 'install_state.json'
 if(Test-Path -LiteralPath $mp -PathType Leaf){$m=Get-Content -LiteralPath $mp -Raw|ConvertFrom-Json;$target=[string]$m.game_root}
}
if(-not $target){
 $targetFile=Join-Path $root 'win81_nis_install_target.txt'
 if(Test-Path -LiteralPath $targetFile -PathType Leaf){$target=(Get-Content -LiteralPath $targetFile -TotalCount 1).Trim().Trim('"')}
}
if(-not $target){Fail 'Aucune installation PTAR ciblee. Lance 01-INSTALL_GW16.bat d abord.' 11}
$target=[IO.Path]::GetFullPath($target);$ini=Join-Path $target 'win81_nis.ini'
if(-not(Test-Path -LiteralPath $ini -PathType Leaf)){Fail ('win81_nis.ini introuvable : '+$ini) 12}
if(-not ('PTARVBlankIniNative' -as [type])){Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PTARVBlankIniNative {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetPrivateProfileIntW")]
 public static extern int GetPrivateProfileInt(string app,string key,int def,string fileName);
}
"@}
function Read-VBlank([string]$Path){return [PTARVBlankIniNative]::GetPrivateProfileInt('WIN81_NIS','VBlankDiagnostics',-777,$Path)}
Write-Host ('[CIBLE INI] '+$ini)
if($Value -eq 0 -or $Value -eq 1){
 $backupDir=Join-Path $diagDir 'backups';if(-not(Test-Path -LiteralPath $backupDir)){New-Item -ItemType Directory -Path $backupDir|Out-Null}
 $backup=Join-Path $backupDir ('win81_nis_before_diag_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.ini');Copy-Item -LiteralPath $ini -Destination $backup
 $lines=[IO.File]::ReadAllLines($ini);$sectionStart=-1
 for($i=0;$i -lt $lines.Length;$i++){if($lines[$i].Trim() -ieq '[WIN81_NIS]'){$sectionStart=$i;break}}
 if($sectionStart -lt 0){Fail 'Section [WIN81_NIS] absente.' 13}
 $sectionEnd=$lines.Length;for($i=$sectionStart+1;$i -lt $lines.Length;$i++){if($lines[$i].Trim() -match '^\[.+\]$'){$sectionEnd=$i;break}}
 $key=@();for($i=$sectionStart+1;$i -lt $sectionEnd;$i++){if($lines[$i] -match '^[ \t]*VBlankDiagnostics[ \t]*='){$key+=$i}}
 if($key.Count -gt 1){Fail 'Plusieurs cles VBlankDiagnostics.' 14}
 if($key.Count -eq 1){$lines[$key[0]]='VBlankDiagnostics='+$Value}else{$list=New-Object Collections.ArrayList;[void]$list.AddRange([object[]]$lines);[void]$list.Insert($sectionStart+1,'VBlankDiagnostics='+$Value);$lines=[string[]]$list.ToArray([string])}
 [IO.File]::WriteAllLines($ini,$lines,[Text.Encoding]::ASCII);Write-Host ('[SAUVEGARDE] '+$backup)
}
$readback=Read-VBlank $ini;if($readback -eq -777){Fail 'VBlankDiagnostics non lisible.' 15}
Write-Host ('[READBACK WIN32] VBlankDiagnostics='+$readback)
if(($Value -eq 0 -or $Value -eq 1) -and $readback -ne $Value){Fail 'Ecriture non confirmee.' 16}
if($RequireEnabled -and $readback -ne 1){Fail 'Le diagnostic visible est OFF.' 17}
if($Value -eq 1){Write-Host '[OK] Marqueurs FG ACTIFS.'}elseif($Value -eq 0){Write-Host '[OK] Marqueurs FG MASQUES; HUD/FPS conserve.'}else{Write-Host '[OK] Etat lu sans modification.'}
exit 0
