$ErrorActionPreference='Stop'
$Here=$PSScriptRoot;$PackRoot=Split-Path -Parent $Here
$StateDir=Join-Path $Here 'state';$StateFile=Join-Path $StateDir 'RC55_STATE.json'
$RC55='271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949'
$RC54='dc23f3bbb2780b92809d5ae218d5cf42186682985e1be7608e7e9ee812ddb80e'
$RC52='fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function Root{if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot};$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p};return $null}
$g=Root;if(-not $g){Write-Host '[FAIL] Warhammer.exe introuvable.';exit 2}
if(Get-Process Warhammer -ErrorAction SilentlyContinue){Write-Host '[FAIL] Fermer Warhammer avant rollback.';exit 3}
$dst=Join-Path $g 'ptar_rc41.dll';$cur=Sha $dst
if($cur -ne $RC55){Write-Host ('[FAIL] RC55 non active, rollback refuse: '+$cur);exit 10}
if(-not(Test-Path -LiteralPath $StateFile -PathType Leaf)){Write-Host '[FAIL] Etat RC55 absent; restauration automatique refusee.';exit 11}
try{$m=Get-Content -LiteralPath $StateFile -Raw|ConvertFrom-Json}catch{Write-Host '[FAIL] Etat RC55 illisible.';exit 12}
$want=[string]$m.preinstall_raster_sha
if(($want -ne $RC54) -and ($want -ne $RC52)){Write-Host ('[FAIL] Hash preinstall non autorise: '+$want);exit 13}
$pre=Join-Path $StateDir 'pre_rc55_ptar_rc41.dll'
if((Sha $pre)-ne $want){Write-Host '[FAIL] Sauvegarde pre-RC55 incoherente.';exit 14}
Copy-Item -LiteralPath $pre -Destination $dst -Force
if((Sha $dst)-ne $want){Write-Host '[FAIL] Restauration pre-RC55 incoherente.';exit 15}
Write-Host ('[PASS] RC55 retiree; sidecar precedente restauree: '+$want)
Write-Host '[INFO] Le rollback DLL ne re-applique pas un eventuel changement registre RC53; celui-ci reste dans l etat consigne par RC55.'
exit 0
