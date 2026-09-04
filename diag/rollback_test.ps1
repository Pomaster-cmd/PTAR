$ErrorActionPreference='Stop';$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l)){Write-Host '[FAIL] Etat absent';exit 2};$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json;$g=$m.game_root
if(Get-Process Warhammer -ErrorAction SilentlyContinue){Write-Host '[FAIL] Fermer Warhammer';exit 3}
$q=Join-Path $s ('ROLLBACK_QUARANTINE_'+(Get-Date -Format 'yyyyMMdd_HHmmss'));New-Item -ItemType Directory -Path $q -Force|Out-Null
foreach($n in @('d3d11.dll','win81_nis_dx11_x64.dll','win81_nis.ini','win81_nis_version.txt')){$d=Join-Path $g $n;$o=$m.original.$n;if($o.exists){Copy-Item -LiteralPath $o.backup -Destination $d -Force;if((Sha $d)-ne $o.sha){Write-Host ('[FAIL] '+$n);exit 10}}elseif(Test-Path -LiteralPath $d -PathType Leaf){Move-Item -LiteralPath $d -Destination (Join-Path $q $n) -Force}}
if($m.windowstyle.applied -and $m.windowstyle.exists){Set-ItemProperty -LiteralPath $m.windowstyle.key -Name WindowStyle -Value ([int]$m.windowstyle.original)}
Write-Host 'ROLLBACK_TEST=PASS';exit 0
