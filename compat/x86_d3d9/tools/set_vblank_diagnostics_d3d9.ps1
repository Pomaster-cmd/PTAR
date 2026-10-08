param(
  [Parameter(Mandatory=$true)][string]$GameRoot,
  [int]$Value=-1,
  [switch]$RequireEnabled
)
$ErrorActionPreference='Stop'

function Fail([string]$Message,[int]$Code){
  Write-Host ('[ERREUR] '+$Message)
  exit $Code
}

if($Value -ne -1 -and $Value -ne 0 -and $Value -ne 1){
  Fail ('Valeur diagnostic invalide : '+$Value) 9
}

$root=[IO.Path]::GetFullPath($GameRoot.Trim().Trim('"'))
if(-not(Test-Path -LiteralPath $root -PathType Container)){
  Fail ('Racine jeu introuvable : '+$root) 10
}

$ini=Join-Path $root 'win81_nis.ini'

# Legacy D3D9 runtime contract: when win81_nis.ini is absent,
# GetPrivateProfileIntW(...,'VBlankDiagnostics',1,...) keeps the marker enabled.
# Do not create a new INI only for the verifier; preserve the field install.
if(-not(Test-Path -LiteralPath $ini -PathType Leaf)){
  Write-Host ('[INFO] win81_nis.ini absent : VBlankDiagnostics=1 par defaut dans le runtime D3D9 valide.')
  if($Value -eq 0){
    Fail 'Impossible de masquer le marqueur sans creer un nouveau fichier INI; operation volontairement refusee.' 12
  }
  Write-Host '[OK] Marqueurs FG ACTIFS par defaut, aucune modification disque.'
  exit 0
}

if(-not ('PTARD3D9VBlankIniNative' -as [type])){
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PTARD3D9VBlankIniNative {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetPrivateProfileIntW")]
 public static extern int GetPrivateProfileInt(string app,string key,int def,string fileName);
}
"@
}

function Read-VBlank([string]$Path){
  return [PTARD3D9VBlankIniNative]::GetPrivateProfileInt('WIN81_NIS','VBlankDiagnostics',1,$Path)
}

Write-Host ('[CIBLE INI] '+$ini)

if($Value -eq 0 -or $Value -eq 1){
  $backupDir=Join-Path $root 'PTAR_D3D9_VERIFIER_BACKUPS'
  if(-not(Test-Path -LiteralPath $backupDir -PathType Container)){
    [IO.Directory]::CreateDirectory($backupDir) | Out-Null
  }
  $backup=Join-Path $backupDir ('win81_nis_before_visible_verifier_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.ini')
  Copy-Item -LiteralPath $ini -Destination $backup

  $lines=[IO.File]::ReadAllLines($ini)
  $sectionStart=-1
  for($i=0;$i -lt $lines.Length;$i++){
    if($lines[$i].Trim() -ieq '[WIN81_NIS]'){$sectionStart=$i;break}
  }
  if($sectionStart -lt 0){
    Fail 'Section [WIN81_NIS] absente; INI laisse intact, sauvegarde conservee.' 13
  }

  $sectionEnd=$lines.Length
  for($i=$sectionStart+1;$i -lt $lines.Length;$i++){
    if($lines[$i].Trim() -match '^\[.+\]$'){$sectionEnd=$i;break}
  }

  $key=@()
  for($i=$sectionStart+1;$i -lt $sectionEnd;$i++){
    if($lines[$i] -match '^[ \t]*VBlankDiagnostics[ \t]*='){$key+=$i}
  }
  if($key.Count -gt 1){
    Fail 'Plusieurs cles VBlankDiagnostics detectees; aucune ecriture.' 14
  }

  if($key.Count -eq 1){
    $lines[$key[0]]='VBlankDiagnostics='+$Value
  } else {
    $list=New-Object Collections.ArrayList
    [void]$list.AddRange([object[]]$lines)
    [void]$list.Insert($sectionStart+1,'VBlankDiagnostics='+$Value)
    $lines=[string[]]$list.ToArray([string])
  }

  [IO.File]::WriteAllLines($ini,$lines,[Text.Encoding]::ASCII)
  Write-Host ('[SAUVEGARDE] '+$backup)
}

$readback=Read-VBlank $ini
Write-Host ('[READBACK WIN32] VBlankDiagnostics='+$readback)
if(($Value -eq 0 -or $Value -eq 1) -and $readback -ne $Value){
  Fail 'Ecriture non confirmee.' 16
}
if($RequireEnabled -and $readback -ne 1){
  Fail 'Le diagnostic visible est OFF.' 17
}

if($Value -eq 1){
  Write-Host '[OK] Marqueurs FG ACTIFS.'
} elseif($Value -eq 0){
  Write-Host '[OK] Marqueurs FG MASQUES; HUD/FPS conserve.'
} else {
  Write-Host '[OK] Etat lu sans modification.'
}
exit 0
