$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$ExpectedRuntime='6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b';$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c';$ExpectedRaster='271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949';$ExpectedIni='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';$ExpectedVersion='51367c26ebe9b6a35147c49aea8b227ae23219fc43166c86638b1e1b09f0a1d9'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$m=Get-Content -LiteralPath (Join-Path $s 'install_state.json') -Raw|ConvertFrom-Json;$g=[string]$m.game_root;$bad=0
foreach($r in @(@('d3d11.dll',$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$ExpectedRuntime),@('win81_nis.ini',$ExpectedIni),@('win81_nis_version.txt',$ExpectedVersion),@('ptar_borderless.dll',$ExpectedBorderless),@('ptar_rc41.dll',$ExpectedRaster))){$h=Sha (Join-Path $g $r[0]);if($h -eq $r[1]){Write-Host ('[PASS] '+$r[0])}else{$bad=1;Write-Host ('[FAIL] '+$r[0]+' '+$h)}}
$t=Get-Content -LiteralPath (Join-Path $g 'win81_nis.ini')
foreach($k in @('TargetExe=Warhammer.exe','Enabled=1','UniversalSpatialPresenter=1','PresenterExclusive=0','GraphicsCartographer=0','RootAuthority=0','OutputWidth=1920','OutputHeight=1080','RenderWidth=1280','RenderHeight=720','Overlay=1','FrameGeneration=0')){if($t -contains $k){Write-Host ('[PASS] '+$k)}else{$bad=1;Write-Host ('[FAIL] '+$k)}}
$rb=[IO.File]::ReadAllBytes((Join-Path $g 'd3d11.dll'))
if($rb.Length -ne 336384){$bad=1;Write-Host '[FAIL] runtime size'}else{Write-Host '[PASS] runtime size 336384'}
function Bytes-At([byte[]]$b,[int]$o,[int[]]$e){for($x=0;$x -lt $e.Count;$x++){if($b[$o+$x] -ne $e[$x]){return $false}};return $true}
if($rb[0xCD00] -eq 0xC3){Write-Host '[PASS] lowres patch preserved'}else{$bad=1;Write-Host '[FAIL] lowres patch byte'}
if(Bytes-At $rb 0xB6FE @(0xE8,0xAD,0x70,0xFF,0xFF)){Write-Host '[PASS] RC40 old presenter hook restored'}else{$bad=1;Write-Host '[FAIL] RC40 restored presenter hook'}
if(Bytes-At $rb 0x2C06 @(0xE8,0xF5,0xCC,0x4F,0x03)){Write-Host '[PASS] RC40 post-HWND loader preserved'}else{$bad=1;Write-Host '[FAIL] RC40 loader hook'}
if(Bytes-At $rb 0x4E717 @(0x49,0x8D,0x83,0x60,0xC4,0x00,0x00)){Write-Host '[PASS] RC40 P1U46-first loader preserved'}else{$bad=1;Write-Host '[FAIL] RC40 loader P1U46 target'}
if(Bytes-At $rb 0x38EE @(0xE8,0x0D,0xC1,0x4F,0x03,0xEB,0x4E,0x90,0x90)){Write-Host '[PASS] RC41 post-swapchain hook -> RVA 0x03500600'}else{$bad=1;Write-Host '[FAIL] RC41 runtime hook bytes'}
if(Bytes-At $rb 0x4E800 @(0x48,0x83,0xEC,0x58,0x4C,0x8D,0x1D,0x00,0x00,0x00,0x00,0x49,0x81,0xEB,0x0B,0x06,0x50,0x03)){Write-Host '[PASS] RC41 loader module-base prologue'}else{$bad=1;Write-Host '[FAIL] RC41 loader prologue'}
$pe=[BitConverter]::ToInt32($rb,0x3C);$n=[BitConverter]::ToUInt16($rb,$pe+6);$opt=[BitConverter]::ToUInt16($rb,$pe+20);$sec=$pe+24+$opt;$rc38vs=$null
for($q=0;$q -lt $n;$q++){$o=$sec+40*$q;$name=[Text.Encoding]::ASCII.GetString($rb[$o..($o+7)]).Trim([char]0);if($name -eq '.rc38'){$rc38vs=[BitConverter]::ToUInt32($rb,$o+8)}}
if($rc38vs -eq 0x6A2){Write-Host '[PASS] RC41 loader mapped inside .rc38 VS=0x6A2'}else{$bad=1;Write-Host ('[FAIL] .rc38 VS '+$rc38vs)}
$manifest=Join-Path $PSScriptRoot 'BASE_TRACKED_SHA256.txt';$baseCount=0
foreach($line in Get-Content -LiteralPath $manifest){if($line -match '^([0-9a-fA-F]{64})  (.+)$'){$exp=$Matches[1].ToLowerInvariant();$rel=$Matches[2];$p=Join-Path $PackRoot ($rel -replace '/','\');$h=Sha $p;$baseCount++;if($h -ne $exp){$bad=1;Write-Host ('[FAIL] BASE MODIFIEE '+$rel+' '+$h)}}}
if($baseCount -ne 107){$bad=1;Write-Host ('[FAIL] Base manifest count '+$baseCount)}else{Write-Host '[PASS] BASE GITHUB 107/107 fichiers byte-identiques'}
$guiKey='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options';$guiText='ABSENT';if(Test-Path -LiteralPath $guiKey){try{$guiText=[string]([int](Get-ItemProperty -LiteralPath $guiKey -Name AffectGuiResolution -ErrorAction Stop).AffectGuiResolution)}catch{}};Write-Host ('[INFO] AffectGuiResolution='+$guiText+' (RC55 ne l impose pas)')
if($bad){exit 9}else{Write-Host 'VERIFY_RC55_BOUND_PHYSICAL_IDEMPOTENCE=PASS';exit 0}
