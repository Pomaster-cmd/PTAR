$ErrorActionPreference='Stop';$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function Full([string]$p){return [IO.Path]::GetFullPath($p.Trim().Trim('"'))}
function Is-Running([string]$exe){$want=Full $exe;foreach($p in @(Get-Process -ErrorAction SilentlyContinue)){try{if((Full $p.MainModule.FileName) -ieq $want){return $true}}catch{}}return $false}
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l)){Write-Host '[FAIL] Etat absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json
$g=[string]$m.game_root;$exe=[string]$m.target_exe
if(Is-Running $exe){Write-Host ('[FAIL] Fermer le jeu : '+$exe);exit 3}
$q=Join-Path $s ('ROLLBACK_QUARANTINE_'+(Get-Date -Format 'yyyyMMdd_HHmmss'));New-Item -ItemType Directory -Path $q -Force|Out-Null
foreach($n in @('d3d11.dll','win81_nis_dx11_x64.dll','win81_nis.ini','win81_nis_version.txt')){
 $d=Join-Path $g $n;$o=$m.original.$n
 if($o.exists){Copy-Item -LiteralPath $o.backup -Destination $d -Force;if((Sha $d)-ne $o.sha){Write-Host ('[FAIL] '+$n);exit 10}}
 elseif(Test-Path -LiteralPath $d -PathType Leaf){Move-Item -LiteralPath $d -Destination (Join-Path $q $n) -Force}
}
Write-Host 'ROLLBACK_TEST=PASS';exit 0
