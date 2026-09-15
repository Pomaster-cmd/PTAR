param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][string]$Engine)
$ErrorActionPreference='Stop';$Root=[IO.Path]::GetFullPath($Root);$ov=Join-Path $Root 'RC41_LOGICAL_NATIVE_SUBRASTER';$own=Join-Path $ov 'OVERLAY_OWNERSHIP.tsv'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$gameRoot=$null;$borderPath=$null;$rasterPath=$null;$borderExpected=@(

 '4bbf83ac8001c2ef6dabc40f58a3d69fb9e35b72fc922605430463595b2257bd',

 'd7eef6d9a2c01e40fed34722c86cee36003a636d663a67174515af5086d274f8',

 'f8654c6d20f243a4ba75cece6a2ceb7df559d9e33ecad2c1275376038826e89b',

 'ce497e72837f95503f877f73239c63a692c90b8646dbf7a3bbd4ba8082083416',
 '3fca95b75a1dbf41434dd45858300e9d64312a2138f2e0799c8ab1bae3ee3beb',
 'cd38729a93b8594203196bdda89efc343db0854f024aaba3a5663009a7503612',
 '13f544d39bbe39811a83c1f5b3bf823d18ef00edf6c9d8cc8ac891208bdc8551',
 'a9c0eb2ec74ac278c2aaef33d1bca237aa504366b43774f8e624374314a6695c',
 '8269d32ccb77b5e65ed159f75ae24266c22b410b8b465a7d504775a937b54890',
 'a4f0c2cc95a8e0624d493b402e46f6938c60cb59c1827d2b677f786fe005c61f',
 '0201c9ae7c1b02163a7960e26effebedd9f6d478987da63a19412af7b758cad3',
 '59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'

);$rasterExpected=@('15a8f276816f3123a79a2d8e6932b6bf42b3826288ea5f016c124afec99fbbcf','7ffe7aa1b9afa42d5f22557cae29a4f65e17379c2a2f181f879427277839f1ca',
 'fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d','dc23f3bbb2780b92809d5ae218d5cf42186682985e1be7608e7e9ee812ddb80e','0e9f8e3ec892cd594fc24f1723559110cecd7ea519615477027014381bfd9b96','271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949')
$latest=Join-Path $Root '_PTAR_UNINSTALL\state\LATEST_STATE.txt'
if(Test-Path -LiteralPath $latest -PathType Leaf){try{$s=(Get-Content -LiteralPath $latest -TotalCount 1).Trim();$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json;$gameRoot=[string]$m.game_root;$borderPath=Join-Path $gameRoot 'ptar_borderless.dll';$rasterPath=Join-Path $gameRoot 'ptar_rc41.dll'}catch{}}
& $Engine -Root $Root;$rc=$LASTEXITCODE;if($rc -ne 0){exit $rc}
if($borderPath -and (Test-Path -LiteralPath $borderPath -PathType Leaf)){$h=Sha $borderPath;if($borderExpected -contains $h){Remove-Item -LiteralPath $borderPath -Force;Write-Host '[OK] Sidecar borderless connu retire.'}else{Write-Host ('[KEEP] Sidecar borderless modifie/inconnu conserve: '+$h)}}
if($rasterPath -and (Test-Path -LiteralPath $rasterPath -PathType Leaf)){$h=Sha $rasterPath;if($rasterExpected -contains $h){Remove-Item -LiteralPath $rasterPath -Force;Write-Host '[OK] Sidecar raster RC41/RC42 connu retire.'}else{Write-Host ('[KEEP] Sidecar RC41 modifie/inconnu conserve: '+$h)}}
if(Test-Path -LiteralPath $own -PathType Leaf){foreach($row in Get-Content -LiteralPath $own){if($row -notmatch '^\d+\|'){continue};$a=$row.Split('|');$p=Join-Path $Root ($a[1]-replace '/','\');if(Test-Path -LiteralPath $p -PathType Leaf){if((Sha $p)-eq $a[2]){Remove-Item -LiteralPath $p -Force}else{Write-Host ('[KEEP] Additif RC41 modifie: '+$a[1])}}};Remove-Item -LiteralPath $own -Force -ErrorAction SilentlyContinue}
$dyn=Join-Path $ov 'PTAR_RC41_INSTALL_LAST.log';$boot=if($gameRoot){Join-Path $gameRoot 'ptar_rc41_bootstrap.log'}else{$null};if(Test-Path -LiteralPath $dyn -PathType Leaf){Remove-Item -LiteralPath $dyn -Force -ErrorAction SilentlyContinue};if($boot -and (Test-Path -LiteralPath $boot -PathType Leaf)){Remove-Item -LiteralPath $boot -Force -ErrorAction SilentlyContinue}
foreach($d in @((Join-Path $ov 'payload'),(Join-Path $ov 'evidence\rc40_inherited'),(Join-Path $ov 'evidence\rc41_lab'),(Join-Path $ov 'evidence\rc41_production'),(Join-Path $ov 'evidence'),$ov)){if(Test-Path -LiteralPath $d -PathType Container){try{Remove-Item -LiteralPath $d -Force -ErrorAction Stop}catch{}}}
Write-Host '[PASS] Moteur canonique GitHub execute puis sidecars/additifs RC55 nettoyes par ownership/SHA.';exit 0
