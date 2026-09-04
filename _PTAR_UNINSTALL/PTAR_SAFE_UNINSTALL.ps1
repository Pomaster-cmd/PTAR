param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
trap { Write-Host ('[FAIL] ' + $_.Exception.Message) -ForegroundColor Red; exit 1 }

$Root=([string]$Root).Trim().Trim('"')
if([string]::IsNullOrWhiteSpace($Root)){throw 'Chemin racine PTAR vide.'}
$Root=[IO.Path]::GetFullPath($Root);if(-not $Root.EndsWith('\')){$Root+='\'}
$U=Join-Path $Root '_PTAR_UNINSTALL'
$Static=Join-Path $U 'PTAR_STATIC_OWNERSHIP.tsv'
$Dirs=Join-Path $U 'PTAR_STATIC_DIRS.tsv'
$StateRoot=Join-Path $U 'state'
$Latest=Join-Path $StateRoot 'LATEST_STATE.txt'
$Log=New-Object Collections.Generic.List[string]
function L([string]$s){$Log.Add($s);Write-Host $s}
function Sha([string]$p){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function DelExact([string]$p,[string]$desc){if(Test-Path -LiteralPath $p -PathType Leaf){Remove-Item -LiteralPath $p -Force -ErrorAction Stop;L('[OK] '+$desc+' : '+$p)}}
function SafeRemoveDir([string]$p){if(Test-Path -LiteralPath $p -PathType Container){try{Remove-Item -LiteralPath $p -Force -ErrorAction Stop;L('[OK] Dossier PTAR vide retire : '+$p)}catch{}}}

if(-not(Test-Path -LiteralPath $Static -PathType Leaf)){throw 'Registre statique PTAR absent.'}
if(-not(Test-Path -LiteralPath $Dirs -PathType Leaf)){throw 'Registre dossiers PTAR absent.'}
if(-not(Test-Path -LiteralPath $Latest -PathType Leaf)){throw 'Etat installation PTAR absent; desinstallation du runtime refusee par securite.'}
$state=(Get-Content -LiteralPath $Latest -TotalCount 1).Trim().Trim('"')
if(-not(Test-Path -LiteralPath (Join-Path $state 'install_state.json') -PathType Leaf)){throw 'Etat installation PTAR invalide.'}
$m=Get-Content -LiteralPath (Join-Path $state 'install_state.json') -Raw|ConvertFrom-Json
if((-not $m.schema) -or ([int]$m.schema -lt 3)){throw 'Etat installation PTAR trop ancien/incomplet pour cette desinstallation securisee.'}
$target=[IO.Path]::GetFullPath([string]$m.game_root);if(-not $target.EndsWith('\')){$target+='\'}

# Game must be closed. Match by executable path when possible, not only process name.
$exe=Join-Path $target 'Warhammer.exe';$running=$false
Get-Process -ErrorAction SilentlyContinue|ForEach-Object{try{if([IO.Path]::GetFullPath($_.MainModule.FileName) -ieq [IO.Path]::GetFullPath($exe)){$running=$true}}catch{}}
if($running){throw 'Le jeu est ouvert. Aucun fichier ne sera desinstalle.'}
L('[CIBLE] '+$target)
L('[POLITIQUE] Desinstallation par ownership exact + SHA; aucun fichier generique/non-PTAR cible.')

$known=@();foreach($x in $m.known_ptar){$known+=([string]$x).ToLowerInvariant()}
$names=@('d3d11.dll','win81_nis_dx11_x64.dll','win81_nis.ini','win81_nis_version.txt')
foreach($n in $names){
    $dst=Join-Path $target $n;$orig=$m.original.$n;$installed=[string]$m.installed.$n
    if(Test-Path -LiteralPath $dst -PathType Leaf){
        $got=Sha $dst
        # Never overwrite/remove any file replaced since this exact install, even another PTAR gate.
        if($got -ne $installed){L('[KEEP] '+$n+' a ete remplace depuis cette installation : '+$got);continue}
    }
    $restore=$false
    if($orig -and $orig.exists -and $orig.backup -and (Test-Path -LiteralPath ([string]$orig.backup) -PathType Leaf)){
        # Full uninstall must not restore an older PTAR gate/config. Restore only state-classified non-PTAR originals.
        $isPtar=$true
        if($orig.PSObject.Properties.Name -contains 'ptar'){$isPtar=[bool]$orig.ptar}
        if(-not $isPtar){$restore=$true}
    }
    if($restore){
        Copy-Item -LiteralPath ([string]$orig.backup) -Destination $dst -Force
        if((Sha $dst)-ne ([string]$orig.sha).ToLowerInvariant()){throw 'Echec restauration '+$n}
        L('[OK] Fichier anterieur non-PTAR restaure : '+$n)
    }elseif(Test-Path -LiteralPath $dst -PathType Leaf){
        Remove-Item -LiteralPath $dst -Force
        L('[OK] Fichier PTAR retire : '+$n)
    }
}

# Exact PTAR runtime outputs only. Result ZIPs are intentionally kept for the user.
foreach($n in @('win81_nis.log','win81_nis_install_target.txt','win81_nis_install_exe.txt','win81_nis_install_backup.txt','win81_nis_quarantined_p1fg7n.txt','PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt','PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt','PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv','PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt','PTAR_VISIBLE_VERIFIER_LAST_COUNTS.txt','PTAR_FLUIDITY_LAST_OUTPUT.txt','PTAR_FLUIDITY_LAST_SAMPLES.csv','PTAR_FLUIDITY_LAST_DONE.flag','PTAR_FLUIDITY_LAST_ERROR.txt','PTAR_FLUIDITY_PROBE_READY.flag','PTAR_FLUIDITY_PROBE_PID.txt')){
    $p=Join-Path $target $n;if(Test-Path -LiteralPath $p -PathType Leaf){Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue;L('[OK] Sortie dynamique PTAR retiree : '+$n)}
}

# Restore registry value only if this exact GW15 install changed it.
if($m.windowstyle -and $m.windowstyle.applied -and $m.windowstyle.exists){
    if(Test-Path -LiteralPath ([string]$m.windowstyle.key)){Set-ItemProperty -LiteralPath ([string]$m.windowstyle.key) -Name WindowStyle -Value ([int]$m.windowstyle.original);L('[OK] WindowStyle restaure a sa valeur pre-installation GW15.')}
}

# Cache ownership lists before deleting package control files.
$rows=@(Get-Content -LiteralPath $Static|Where-Object{$_ -match '^\d+\|'})
$drows=@(Get-Content -LiteralPath $Dirs|Where-Object{$_ -match '^\d+\|'})

# Dynamic package-side outputs.
foreach($pat in @('PTAR_GW15_INSTALL_LAST.log','PTAR_VERIFY_LAST.log')){
    Get-ChildItem -LiteralPath (Join-Path $Root 'diag') -Filter $pat -File -ErrorAction SilentlyContinue|ForEach-Object{Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue;L('[OK] Journal PTAR retire : '+$_.Name)}
}
Get-ChildItem -LiteralPath (Join-Path $Root 'diag') -Directory -Filter 'collect_*' -ErrorAction SilentlyContinue|ForEach-Object{Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue;L('[OK] Staging collecte PTAR retire : '+$_.Name)}

# Static package files: numeric ownership + exact SHA. Changed files are kept.
foreach($row in $rows){
    $a=$row.Split('|');$id=$a[0];$rel=$a[1];$exp=$a[2].ToLowerInvariant();$p=Join-Path $Root ($rel -replace '/','\')
    if(Test-Path -LiteralPath $p -PathType Leaf){$got=Sha $p;if($got -eq $exp){Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue;L('[OK] OWNER '+$id+' retire : '+$rel)}else{L('[KEEP] OWNER '+$id+' modifie depuis extraction : '+$rel)}}
}

# State is package-owned and can be removed after restoration is complete.
if(Test-Path -LiteralPath $StateRoot -PathType Container){Remove-Item -LiteralPath $StateRoot -Recurse -Force -ErrorAction SilentlyContinue;L('[OK] Etat/backup PTAR retire.')}

# The ownership registry intentionally does not own itself; remove it last.
if(Test-Path -LiteralPath $Static -PathType Leaf){Remove-Item -LiteralPath $Static -Force -ErrorAction SilentlyContinue;L('[OK] Registre ownership PTAR retire.')}

# Directories are deleted only when empty. The game directory itself is never targeted.
foreach($row in $drows){$a=$row.Split('|');$p=Join-Path $Root ($a[2] -replace '/','\');SafeRemoveDir $p}
SafeRemoveDir $U
L('[INFO] Les ZIP de resultats et l archive ZIP d origine sont conserves.')
L('[PASS] Desinstallation complete PTAR terminee; aucun fichier generique/non-PTAR n a ete cible.')
exit 0
