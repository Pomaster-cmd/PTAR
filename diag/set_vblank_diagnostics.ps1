param(
    [int]$Value = -1,
    [switch]$RequireEnabled
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message, [int]$Code) {
    Write-Host ('[ERREUR] ' + $Message)
    exit $Code
}

if ($Value -ne -1 -and $Value -ne 0 -and $Value -ne 1) {
    Fail ('Valeur diagnostic invalide : ' + $Value) 9
}

try {
    $diagDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $root = [IO.Path]::GetFullPath((Join-Path $diagDir '..'))
} catch {
    Fail ('Impossible de determiner la racine PTAR : ' + $_.Exception.Message) 10
}

$target = $root
$targetFile = Join-Path $root 'win81_nis_install_target.txt'
if (-not (Test-Path -LiteralPath $targetFile)) {
    $parent = Split-Path -Parent $root
    if ((-not (Test-Path -LiteralPath (Join-Path $root 'Warhammer.exe'))) -and (Test-Path -LiteralPath (Join-Path $parent 'Warhammer.exe'))) {
        $target = $parent
    }
}
if (Test-Path -LiteralPath $targetFile) {
    $candidate = (Get-Content -LiteralPath $targetFile -TotalCount 1).Trim().Trim('"')
    if ($candidate) {
        try {
            if (-not [IO.Path]::IsPathRooted($candidate)) {
                $candidate = Join-Path $root $candidate
            }
            $target = [IO.Path]::GetFullPath($candidate)
        } catch {
            Fail ('Cible invalide dans win81_nis_install_target.txt : ' + $candidate) 11
        }
    }
}

$ini = Join-Path $target 'win81_nis.ini'
if (-not (Test-Path -LiteralPath $ini)) {
    Fail ('win81_nis.ini introuvable : ' + $ini) 12
}

if (-not ('PTARVBlankIniNative' -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PTARVBlankIniNative {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetPrivateProfileIntW")]
    public static extern int GetPrivateProfileInt(string app, string key, int def, string fileName);
}
"@
}

function Read-VBlank([string]$Path) {
    return [PTARVBlankIniNative]::GetPrivateProfileInt('WIN81_NIS', 'VBlankDiagnostics', -777, $Path)
}

Write-Host ('[RACINE PTAR] ' + $root)
Write-Host ('[CIBLE INI] ' + $ini)

if ($Value -eq 0 -or $Value -eq 1) {
    $backupDir = Join-Path $diagDir 'backups'
    if (-not (Test-Path -LiteralPath $backupDir)) {
        New-Item -ItemType Directory -Path $backupDir | Out-Null
    }
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $backup = Join-Path $backupDir ("win81_nis_before_diag_{0}.ini" -f $stamp)
    Copy-Item -LiteralPath $ini -Destination $backup -ErrorAction Stop

    $lines = [IO.File]::ReadAllLines($ini)
    $sectionStart = -1
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i].Trim() -ieq '[WIN81_NIS]') {
            $sectionStart = $i
            break
        }
    }
    if ($sectionStart -lt 0) {
        Fail 'Section [WIN81_NIS] absente. Configuration non modifiee.' 13
    }

    $sectionEnd = $lines.Length
    for ($i = $sectionStart + 1; $i -lt $lines.Length; $i++) {
        if ($lines[$i].Trim() -match '^\[.+\]$') {
            $sectionEnd = $i
            break
        }
    }

    $keyIndexes = @()
    for ($i = $sectionStart + 1; $i -lt $sectionEnd; $i++) {
        if ($lines[$i] -match '^[ \t]*VBlankDiagnostics[ \t]*=') {
            $keyIndexes += $i
        }
    }
    if ($keyIndexes.Count -gt 1) {
        Fail 'Plusieurs cles VBlankDiagnostics existent dans [WIN81_NIS]. Aucune modification automatique.' 14
    }

    if ($keyIndexes.Count -eq 1) {
        $lines[$keyIndexes[0]] = ('VBlankDiagnostics={0}' -f $Value)
    } else {
        $list = New-Object System.Collections.ArrayList
        [void]$list.AddRange([object[]]$lines)
        [void]$list.Insert($sectionStart + 1, ('VBlankDiagnostics={0}' -f $Value))
        $lines = [string[]]$list.ToArray([string])
    }

    [IO.File]::WriteAllLines($ini, $lines, [Text.Encoding]::ASCII)
    Write-Host ('[SAUVEGARDE] ' + $backup)
}

$readback = Read-VBlank $ini
if ($readback -eq -777) {
    Fail 'GetPrivateProfileIntW ne retrouve pas VBlankDiagnostics dans [WIN81_NIS].' 15
}

Write-Host ('[READBACK WIN32] VBlankDiagnostics={0}' -f $readback)

if (($Value -eq 0 -or $Value -eq 1) -and $readback -ne $Value) {
    Fail (('Ecriture demandee={0} mais relecture runtime={1}' -f $Value, $readback)) 16
}
if ($RequireEnabled -and $readback -ne 1) {
    Fail 'Le diagnostic visible est OFF. VBlankDiagnostics doit etre 1.' 17
}

if ($Value -eq 1) {
    Write-Host '[OK] Diagnostic visible ACTIVE et confirme par l API INI Win32.'
    Write-Host '[SI FG EST DEJA ON] Faites CTRL+F6 OFF puis CTRL+F6 ON.'
} elseif ($Value -eq 0) {
    Write-Host '[OK] Diagnostic visible DESACTIVE et confirme par l API INI Win32.'
} else {
    Write-Host '[OK] Etat du diagnostic lu sans modifier la configuration.'
}

exit 0
