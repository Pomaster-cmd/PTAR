$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$ExpectedRuntime='774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669';$ExpectedSidecar='ce497e72837f95503f877f73239c63a692c90b8646dbf7a3bbd4ba8082083416';$ExpectedIni='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';$ExpectedVersion='f32d1b3b79a2d5a4973cb28cb1207326dbc42a02c4647489debd3cba3786fc99'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json;$g=[string]$m.game_root;$bad=0
foreach($r in @(@('d3d11.dll',$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$ExpectedRuntime),@('win81_nis.ini',$ExpectedIni),@('win81_nis_version.txt',$ExpectedVersion),@('ptar_borderless.dll',$ExpectedSidecar))){$h=Sha (Join-Path $g $r[0]);if($h -eq $r[1]){Write-Host ('[PASS] '+$r[0])}else{$bad=1;Write-Host ('[FAIL] '+$r[0]+' '+$h)}}
$t=Get-Content -LiteralPath (Join-Path $g 'win81_nis.ini')
foreach($k in @('TargetExe=Warhammer.exe','Enabled=1','UniversalSpatialPresenter=1','PresenterExclusive=0','GraphicsCartographer=0','RootAuthority=0','OutputWidth=1920','OutputHeight=1080','RenderWidth=1280','RenderHeight=720','Overlay=1','FrameGeneration=0')){if($t -contains $k){Write-Host ('[PASS] '+$k)}else{$bad=1;Write-Host ('[FAIL] '+$k)}}
$rb=[IO.File]::ReadAllBytes((Join-Path $g 'd3d11.dll'))
if($rb.Length -ne 336384){$bad=1;Write-Host '[FAIL] runtime size'}else{Write-Host '[PASS] runtime size 336384'}
if($rb[0xCD00] -ne 0xC3){$bad=1;Write-Host '[FAIL] lowres patch byte'}else{Write-Host '[PASS] lowres patch preserved'}
function Bytes-At([byte[]]$b,[int]$o,[int[]]$e){for($x=0;$x -lt $e.Count;$x++){if($b[$o+$x] -ne $e[$x]){return $false}};return $true}
if(Bytes-At $rb 0xB6FE @(0xE8,0xAD,0x70,0xFF,0xFF)){Write-Host '[PASS] RC40 old presenter hook restored -> RVA 0x000033B0'}else{$bad=1;Write-Host '[FAIL] RC40 restored presenter hook bytes'}
if(Bytes-At $rb 0x2C06 @(0xE8,0xF5,0xCC,0x4F,0x03)){Write-Host '[PASS] RC40 loader moved to RVA 0x00003806 -> RVA 0x03500500'}else{$bad=1;Write-Host '[FAIL] RC40 post-game-HWND loader hook bytes'}
if(Bytes-At $rb 0x4E717 @(0x49,0x8D,0x83,0x60,0xC4,0x00,0x00)){Write-Host '[PASS] RC40 loader first calls P1U46 installer RVA 0x0000C460'}else{$bad=1;Write-Host '[FAIL] RC40 loader P1U46 target bytes'}
$manifest=Join-Path $PSScriptRoot 'BASE_TRACKED_SHA256.txt';$baseCount=0
foreach($line in Get-Content -LiteralPath $manifest){if($line -match '^([0-9a-fA-F]{64})  (.+)$'){$exp=$Matches[1].ToLowerInvariant();$rel=$Matches[2];$p=Join-Path $PackRoot ($rel -replace '/','\');$h=Sha $p;$baseCount++;if($h -ne $exp){$bad=1;Write-Host ('[FAIL] BASE MODIFIEE '+$rel+' '+$h)}}}
if($baseCount -ne 107){$bad=1;Write-Host ('[FAIL] Base manifest count '+$baseCount)}else{Write-Host '[PASS] BASE GITHUB 107/107 fichiers byte-identiques'}
if($bad){exit 9}else{Write-Host 'VERIFY_RC40_POSTWNDPROC_AUTHORITY=PASS';exit 0}
