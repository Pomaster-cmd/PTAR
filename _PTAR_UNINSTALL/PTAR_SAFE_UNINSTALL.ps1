param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'

trap {
    Write-Host ('[FAIL] ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}

# Windows 8.1 / PowerShell 4 safe normalization.
# The caller now passes a path without trailing backslash, but strip any
# accidental surrounding quotes as a second defensive layer.
$Root = ([string]$Root).Trim().Trim('"')
if([string]::IsNullOrWhiteSpace($Root)){
    throw 'Chemin racine PTAR vide.'
}
$Root=[IO.Path]::GetFullPath($Root)
if(-not $Root.EndsWith('\')){$Root+='\'}
$U=Join-Path $Root '_PTAR_UNINSTALL'
$Static=Join-Path $U 'PTAR_STATIC_OWNERSHIP.tsv'
$Dirs=Join-Path $U 'PTAR_STATIC_DIRS.tsv'
$Runtime=Join-Path $Root 'win81_nis_dx11_x64.dll'
$TargetFile=Join-Path $Root 'win81_nis_install_target.txt'
$ExeFile=Join-Path $Root 'win81_nis_install_exe.txt'
$BackupFile=Join-Path $Root 'win81_nis_install_backup.txt'
$QuarFile=Join-Path $Root 'win81_nis_quarantined_p1fg7n.txt'
$Log=New-Object Collections.Generic.List[string]
function L([string]$s){$Log.Add($s);Write-Host $s}
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function DelExact([string]$p,[string]$desc){
    if(Test-Path -LiteralPath $p -PathType Leaf){
        Remove-Item -LiteralPath $p -Force -ErrorAction Stop
        L ('[OK] '+$desc+' : '+$p)
    }
}
function SafeRemoveDir([string]$p){
    if(Test-Path -LiteralPath $p -PathType Container){
        try{Remove-Item -LiteralPath $p -Force -ErrorAction Stop;L('[OK] Dossier PTAR vide retire : '+$p)}catch{}
    }
}

if(-not(Test-Path -LiteralPath $Static)){throw 'Registre statique PTAR absent.'}
if(-not(Test-Path -LiteralPath $Runtime)){throw 'DLL package PTAR absente; impossible de verifier le proxy actif.'}

$target=$Root
if(Test-Path -LiteralPath $TargetFile){
    $x=(Get-Content -LiteralPath $TargetFile -TotalCount 1).Trim().Trim('"')
    if($x){$target=[IO.Path]::GetFullPath($x);if(-not $target.EndsWith('\')){$target+='\'}} 
}

# Game must be closed.
if(Test-Path -LiteralPath $ExeFile){
    $exe=(Get-Content -LiteralPath $ExeFile -TotalCount 1).Trim().Trim('"')
    if($exe){
        $running=$false
        Get-Process|ForEach-Object{try{if([IO.Path]::GetFullPath($_.MainModule.FileName) -ieq [IO.Path]::GetFullPath($exe)){$running=$true}}catch{}}
        if($running){throw 'Le jeu est ouvert. Aucun fichier ne sera desinstalle.'}
    }
}

L('[CIBLE] '+$target)
L('[POLITIQUE] Registre numerique externe uniquement; aucun ADS; aucun hook; aucun registre Windows.')

# Restore/remove only the exact d3d11 installed by PTAR.
$active=Join-Path $target 'd3d11.dll'
if(Test-Path -LiteralPath $active){
    $expected=Sha $Runtime
    $got=Sha $active
    if($got -eq $expected){
        $backup='NONE'
        if(Test-Path -LiteralPath $BackupFile){$backup=(Get-Content -LiteralPath $BackupFile -TotalCount 1).Trim().Trim('"')}
        if($backup -and $backup -ne 'NONE' -and (Test-Path -LiteralPath $backup -PathType Leaf)){
            Copy-Item -LiteralPath $backup -Destination $active -Force
            L('[OK] Wrapper d3d11 precedent restaure.')
            Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
        }else{
            Remove-Item -LiteralPath $active -Force
            L('[OK] d3d11.dll PTAR retire.')
        }
    }else{
        L('[KEEP] d3d11.dll actif ne correspond plus a la DLL PTAR fournie.')
    }
}

# Restore quarantined old wrappers only from the exact installer ledger.
if(Test-Path -LiteralPath $QuarFile){
    foreach($line in Get-Content -LiteralPath $QuarFile){
        $a=$line -split '\|',2
        if($a.Count -eq 2 -and (Test-Path -LiteralPath $a[1]) -and -not(Test-Path -LiteralPath $a[0])){
            Move-Item -LiteralPath $a[1] -Destination $a[0] -Force
            L('[OK] Wrapper mis en quarantaine restaure : '+$a[0])
        }
    }
}

# Root-side dynamic ledgers/logs created by the extracted PTAR package.
# They are not necessarily located in the renderer target directory.
foreach($x in @(
    $TargetFile,$ExeFile,$BackupFile,$QuarFile,
    (Join-Path $Root 'PTAR_INSTALL_LAST.log'),
    (Join-Path $Root 'PTAR_VERIFY_LAST.log'),
    (Join-Path $Root 'PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt')
)){
    if($x){DelExact $x 'Sortie dynamique PTAR racine retiree'}
}

# PTAR-specific dynamic files. Numeric rule IDs exist in PTAR_DYNAMIC_RULES.tsv.
# No generic "delete everything that did not exist before" behavior.
$patterns=@(
    'win81_nis.ini','win81_nis.pre_p1fg7n.bak','win81_nis_version.txt','win81_nis.log',
    'win81_nis.pre_p1fg7n_*.log','win81_nis_install_target.txt','win81_nis_install_exe.txt',
    'win81_nis_install_backup.txt','win81_nis_quarantined_p1fg7n.txt',
    'd3d11.win81nis_previous_*.dll','win81_nis_capture_*.bmp',
    'INSTALL_QSV_HELPER_WIN81.log','TEST_QSV_PROFILE_SELFTEST.log',
    'PTAR_VERIFY_LAST.log','PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt'
)
foreach($pat in $patterns){
    Get-ChildItem -LiteralPath $target -Filter $pat -File -ErrorAction SilentlyContinue|ForEach-Object{
        # Never remove the active d3d11 here; handled above.
        if($_.Name -ine 'd3d11.dll'){Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue;L('[OK] Sortie PTAR retiree : '+$_.Name)}
    }
}

# Known diagnostic outputs created below diag by DIAGREPAIR1/2/3 helpers.
# Exact names/patterns only; no generic cleanup of user files.
$legacyVisible=Join-Path $Root 'diag\LAST_VISIBLE_VERIFIER.log'
if(Test-Path -LiteralPath $legacyVisible -PathType Leaf){
    Remove-Item -LiteralPath $legacyVisible -Force -ErrorAction SilentlyContinue
    L('[OK] Ancien journal visible-verifier PTAR retire.')
}
$diagBackups=Join-Path $Root 'diag\backups'
if(Test-Path -LiteralPath $diagBackups -PathType Container){
    Get-ChildItem -LiteralPath $diagBackups -Filter 'win81_nis_before_diag_*.ini' -File -ErrorAction SilentlyContinue|ForEach-Object{
        Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
        L('[OK] Sauvegarde diagnostic PTAR retiree : '+$_.Name)
    }
    SafeRemoveDir $diagBackups
}

$rec=Join-Path $target 'recordings'
if(Test-Path -LiteralPath $rec -PathType Container){
    Get-ChildItem -LiteralPath $rec -File -ErrorAction SilentlyContinue|Where-Object{
        $_.Name -like 'B18K18_QSV_*.mp4' -or $_.Name -like 'PTAR_PROFILE*_SELFTEST_5S_*.mp4'
    }|ForEach-Object{Remove-Item -LiteralPath $_.FullName -Force;L('[OK] Video PTAR retiree : '+$_.Name)}
    SafeRemoveDir $rec
}

$qsv=Join-Path $target 'tools\qsv'
foreach($n in @('b18k17_ffmpeg.exe','B18K18_QSV_READY.txt')){
    $p=Join-Path $qsv $n
    if(Test-Path -LiteralPath $p -PathType Leaf){Remove-Item -LiteralPath $p -Force;L('[OK] Dependance PTAR retiree : '+$n)}
}
SafeRemoveDir $qsv
SafeRemoveDir (Join-Path $target 'tools')

$dl=Join-Path $Root '_download_qsv'
if(Test-Path -LiteralPath $dl -PathType Container){Remove-Item -LiteralPath $dl -Recurse -Force;L('[OK] Staging QSV PTAR retire.')}

Get-ChildItem -LiteralPath $Root -Directory -Filter 'PTARFG1_RESULTS_*' -ErrorAction SilentlyContinue|ForEach-Object{
    Remove-Item -LiteralPath $_.FullName -Recurse -Force
    L('[OK] Resultats COLLECT PTAR retires : '+$_.Name)
}

# Cache ownership data before any package control file can be deleted.
$rows=@(Get-Content -LiteralPath $Static|Where-Object{$_ -match '^\d+\|'})
$drows=@(Get-Content -LiteralPath $Dirs|Where-Object{$_ -match '^\d+\|'})

# Static package files: numeric manifest + exact SHA.
# A changed file is kept to avoid deleting a later unrelated replacement.
foreach($row in $rows){
    $a=$row.Split('|')
    $id=$a[0];$rel=$a[1];$exp=$a[2]
    $p=Join-Path $Root ($rel -replace '/','\')
    if(Test-Path -LiteralPath $p -PathType Leaf){
        $got=Sha $p
        if($got -eq $exp){
            Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
            L('[OK] OWNER '+$id+' retire : '+$rel)
        }else{
            L('[KEEP] OWNER '+$id+' modifie depuis extraction : '+$rel)
        }
    }
}

# Delete package directories only if empty.
foreach($row in $drows){
    $a=$row.Split('|');$p=Join-Path $Root ($a[2] -replace '/','\')
    SafeRemoveDir $p
}

# The static ownership registry intentionally does not own itself.
# Remove it only after its rows have been cached and processed.
if(Test-Path -LiteralPath $Static -PathType Leaf){
    Remove-Item -LiteralPath $Static -Force -ErrorAction SilentlyContinue
    L('[OK] Registre numerique PTAR retire.')
}
SafeRemoveDir $U

# Archive ZIP d'origine : toujours conservee.
# Elle n'appartient pas a l'installation extraite et ne doit jamais etre supprimee.
L('[INFO] Archive ZIP PTAR d origine conservee.')

L('[PASS] Desinstallation terminee. Aucun fichier generique/non-PTAR n a ete cible.')
exit 0
