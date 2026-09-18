$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$Latest=Join-Path (Join-Path $PackRoot '_CLICK_INPUT_DIAG') 'LATEST_SESSION.txt'
if(-not(Test-Path -LiteralPath $Latest -PathType Leaf)){Write-Host '[FAIL] Aucune session.';exit 2}
$Session=(Get-Content -LiteralPath $Latest -TotalCount 1).Trim()
$meta=Get-Content -LiteralPath (Join-Path $Session 'session.json') -Raw|ConvertFrom-Json
$ini=Join-Path ([string]$meta.game_root) 'win81_nis.ini'
$backup=[string]$meta.backup_ini
if(-not(Test-Path -LiteralPath $backup -PathType Leaf)){Write-Host '[FAIL] Backup INI introuvable.';exit 3}
Copy-Item -LiteralPath $backup -Destination $ini -Force
$sha=(Get-FileHash -LiteralPath $ini -Algorithm SHA256).Hash.ToLowerInvariant()
if($sha -ne [string]$meta.original_ini_sha256){Write-Host '[FAIL] Hash INI restaure incoherent.';exit 4}
Write-Host ('[PASS] INI original restaure : '+$sha)
exit 0
