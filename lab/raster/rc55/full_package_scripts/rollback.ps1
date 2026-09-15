$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot;$S=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$ExpectedGuiMigrationModule='10a87355e56a84af2ad2905d31313b0c17e64aa6169b2fd1084201f3e52c2a1b'
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
$GuiMigrationModule=Join-Path $PSScriptRoot 'rc55_gui_migration.ps1';if((Sha $GuiMigrationModule)-ne $ExpectedGuiMigrationModule){Write-Host '[FAIL] Module migration GUI RC55 absent/modifie.';exit 15};. $GuiMigrationModule
$l=Join-Path $S 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $l -PathType Leaf)){Write-Host '[FAIL] Etat installation absent';exit 2}
$s=(Get-Content -LiteralPath $l -TotalCount 1).Trim();$sp=Join-Path $s 'install_state.json';$m=Get-Content -LiteralPath $sp -Raw|ConvertFrom-Json;$g=[string]$m.game_root
if(Get-Process Warhammer -ErrorAction SilentlyContinue){Write-Host '[FAIL] Fermer Warhammer.';exit 3}
$base=Join-Path $PackRoot 'payload';$E=@{'d3d11.dll'='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c';'win81_nis_dx11_x64.dll'='864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c';'win81_nis.ini'='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';'win81_nis_version.txt'='8c15a4ab74222f1efc7305315bf25dd06903f45bd568abdd3a04d152501e0c51'}
$C=@{'d3d11.dll'='6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b';'win81_nis_dx11_x64.dll'='6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b';'win81_nis.ini'='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50';'win81_nis_version.txt'='51367c26ebe9b6a35147c49aea8b227ae23219fc43166c86638b1e1b09f0a1d9'}
foreach($n in $E.Keys){
 $src=Join-Path $base $n;if((Sha $src)-ne $E[$n]){Write-Host ('[FAIL] Base GitHub alteree: '+$n);exit 10}
 $dst=Join-Path $g $n;$cur=Sha $dst
 $allowed=@($C[$n],$E[$n]);if(($n -eq 'd3d11.dll') -or ($n -eq 'win81_nis_dx11_x64.dll')){$allowed+=@('774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669','cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb')}
 if($n -eq 'win81_nis_version.txt'){$allowed+=@('f32d1b3b79a2d5a4973cb28cb1207326dbc42a02c4647489debd3cba3786fc99','dcbccff254f8caec3823a5f57e4d8177428178aa6f0b05cbcfa911288548c8bc','6126a747df4c366ce26a06000375c3dd060edde9efe5beef0da8cec2d47958a0')}
 if($allowed -notcontains $cur){Write-Host ('[FAIL] Fichier actif inconnu/modifie, rollback refuse: '+$n+' '+$cur);exit 11}
}
$border=Join-Path $g 'ptar_borderless.dll';$bh=Sha $border;$KnownBorder=@(

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

);if($bh -and ($KnownBorder -notcontains $bh)){Write-Host ('[FAIL] Sidecar borderless inconnu/modifie, rollback refuse: '+$bh);exit 13}
$raster=Join-Path $g 'ptar_rc41.dll';$rh=Sha $raster;$KnownRaster=@('15a8f276816f3123a79a2d8e6932b6bf42b3826288ea5f016c124afec99fbbcf','7ffe7aa1b9afa42d5f22557cae29a4f65e17379c2a2f181f879427277839f1ca',
 'fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d','dc23f3bbb2780b92809d5ae218d5cf42186682985e1be7608e7e9ee812ddb80e','0e9f8e3ec892cd594fc24f1723559110cecd7ea519615477027014381bfd9b96','271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949');if($rh -and ($KnownRaster -notcontains $rh)){Write-Host ('[FAIL] Sidecar raster RC41/RC42 inconnu/modifie, rollback refuse: '+$rh);exit 14}
foreach($n in $E.Keys){Copy-Item -LiteralPath (Join-Path $base $n) -Destination (Join-Path $g $n) -Force;if((Sha (Join-Path $g $n))-ne $E[$n]){Write-Host ('[FAIL] Rollback '+$n);exit 12};if($m.installed.PSObject.Properties.Name -contains $n){$m.installed.$n=$E[$n]}}
if($KnownBorder -contains (Sha $border)){Remove-Item -LiteralPath $border -Force}
if($KnownRaster -contains (Sha $raster)){Remove-Item -LiteralPath $raster -Force}
if($m.windowstyle -and $m.windowstyle.applied){$key=[string]$m.windowstyle.key;if(Test-Path -LiteralPath $key){if([bool]$m.windowstyle.exists){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Type DWord -Value ([int]$m.windowstyle.original)}else{Remove-ItemProperty -LiteralPath $key -Name WindowStyle -ErrorAction SilentlyContinue}};$m.windowstyle.applied=$false}
if(($m.PSObject.Properties.Name -contains 'migration') -and $m.migration -and ($m.migration.PSObject.Properties.Name -contains 'gui_resolution') -and $m.migration.gui_resolution){$u=Undo-Rc55GuiMigration $m.migration.gui_resolution;Write-Host ('[INFO] GUI_MIGRATION_ROLLBACK='+[string]$u.event);if(([string]$u.event) -eq 'ERROR'){Write-Host ('[FAIL] '+[string]$u.error);exit 16}}
$m.package='GW16H_SAFEPOINT11_FUSEDDETAIL1_ROLLBACK_GITHUB_MAIN';$m|Add-Member -NotePropertyName rc41_sidecars_removed -NotePropertyValue $true -Force;$m|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $sp -Encoding UTF8
Write-Host '[PASS] RC55 retire; payload GitHub main exact restaure; sidecars connus retires.';exit 0
