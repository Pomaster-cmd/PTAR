param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
trap { Write-Host ('[FAIL] ' + $_.Exception.Message) -ForegroundColor Red; exit 1 }
$Root=([string]$Root).Trim().Trim('"');if([string]::IsNullOrWhiteSpace($Root)){throw 'Chemin racine PTAR vide.'};$Root=[IO.Path]::GetFullPath($Root);if(-not $Root.EndsWith('\')){$Root+='\'}
$U=Join-Path $Root '_PTAR_UNINSTALL';$Static=Join-Path $U 'PTAR_STATIC_OWNERSHIP.tsv';$Dirs=Join-Path $U 'PTAR_STATIC_DIRS.tsv';$PsExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
function Sha([string]$p){
    $fs=[IO.File]::Open($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try{
        $sha=[Security.Cryptography.SHA256]::Create()
        try{
            $hash=$sha.ComputeHash($fs)
            return ([BitConverter]::ToString($hash)).Replace('-','').ToLowerInvariant()
        } finally {
            $sha.Dispose()
        }
    } finally {
        $fs.Dispose()
    }
}
function SafeRemoveDir([string]$p){if(Test-Path -LiteralPath $p -PathType Container){try{Remove-Item -LiteralPath $p -Force -ErrorAction Stop}catch{}}}
function Is-Running([string]$exe){$want=[IO.Path]::GetFullPath($exe);$r=$false;Get-Process -ErrorAction SilentlyContinue|ForEach-Object{try{if([IO.Path]::GetFullPath($_.MainModule.FileName) -ieq $want){$r=$true}}catch{}};return $r}
if(-not(Test-Path -LiteralPath $Static -PathType Leaf)){throw 'Registre statique PTAR absent.'};if(-not(Test-Path -LiteralPath $Dirs -PathType Leaf)){throw 'Registre dossiers PTAR absent.'}
$rows=@(Get-Content -LiteralPath $Static|Where-Object{$_ -match '^\d+\|'});$drows=@(Get-Content -LiteralPath $Dirs|Where-Object{$_ -match '^\d+\|'})
# D3D11 existing safe backend uninstaller.
$B11=Join-Path $Root 'BACKENDS\D3D11_X64';$bu=Join-Path $B11 '_PTAR_UNINSTALL';$engine=Join-Path $bu 'PTAR_SAFE_UNINSTALL.ps1';$latest=Join-Path $bu 'state\LATEST_STATE.txt'
if(Test-Path -LiteralPath $latest -PathType Leaf){if(-not(Test-Path -LiteralPath $engine -PathType Leaf)){throw 'Moteur de desinstallation D3D11 absent.'};$state=(Get-Content -LiteralPath $latest -TotalCount 1).Trim().Trim('"');$json=Join-Path $state 'install_state.json';if(-not(Test-Path -LiteralPath $json -PathType Leaf)){throw 'Etat installation D3D11 invalide.'};$m=Get-Content -LiteralPath $json -Raw|ConvertFrom-Json;$exe=[string]$m.target_exe;if([string]::IsNullOrWhiteSpace($exe)){throw 'Executable D3D11 cible absent de l etat.'};if(Is-Running $exe){throw ('Le jeu D3D11 est ouvert : '+$exe)};$tmp=Join-Path $env:TEMP ('PTAR_D3D11_UNINSTALL_'+[Guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($tmp)|Out-Null;try{Copy-Item -LiteralPath $engine -Destination (Join-Path $tmp 'PTAR_SAFE_UNINSTALL.ps1') -Force;& $PsExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tmp 'PTAR_SAFE_UNINSTALL.ps1') -Root $B11;$rc=$LASTEXITCODE;if($rc -ne 0){throw ('Moteur D3D11 interrompu - code '+$rc)}}finally{if(Test-Path -LiteralPath $tmp){Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue}}}
# D3D9 existing hash/state safe uninstaller.
$B9=Join-Path $Root 'BACKENDS\D3D9_X86';$last9=Join-Path $B9 'PTAR_X86_D3D9_LAST_TARGET.txt';if(Test-Path -LiteralPath $last9 -PathType Leaf){$exe9=(Get-Content -LiteralPath $last9 -TotalCount 1).Trim().Trim([char]0xFEFF).Trim('"');if(-not([string]::IsNullOrWhiteSpace($exe9))){if(Is-Running $exe9){throw ('Le jeu D3D9 est ouvert : '+$exe9)};$u9=Join-Path $B9 'uninstall_x86_d3d9.ps1';if(-not(Test-Path -LiteralPath $u9 -PathType Leaf)){throw 'Moteur de desinstallation D3D9 absent.'};& $PsExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $u9 -GameExe $exe9;$rc=$LASTEXITCODE;if($rc -ne 0){throw ('Moteur D3D9 interrompu - code '+$rc)}};Remove-Item -LiteralPath $last9 -Force -ErrorAction SilentlyContinue}
# Remove package-owned static files only when hashes still match.
# The active top-level BAT is deliberately deferred: deleting the executing
# batch before control returns from PowerShell can make cmd.exe print spurious
# "file not found"/batch continuation errors even though uninstall succeeded.
foreach($row in $rows){$a=$row.Split('|');if($a.Count -lt 3){continue};$rel=[string]$a[1];if($rel -ieq '06-DESINSTALLER_PTAR_AUTO.bat'){Write-Host '[DEFER] Lanceur actif : suppression reportee apres fermeture.';continue};$p=Join-Path $Root ($rel -replace '/','\');if(Test-Path -LiteralPath $p -PathType Leaf){if((Sha $p) -eq $a[2].ToLowerInvariant()){Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue}else{Write-Host ('[KEEP] Fichier modifie : '+$rel)}}}
if(Test-Path -LiteralPath $Static){Remove-Item -LiteralPath $Static -Force -ErrorAction SilentlyContinue};foreach($row in $drows){$a=$row.Split('|');if($a.Count -ge 3){SafeRemoveDir (Join-Path $Root ($a[2] -replace '/','\'))}};SafeRemoveDir $U;Write-Host '[PASS] Desinstallation PTAR COMPLETE terminee.';exit 0
