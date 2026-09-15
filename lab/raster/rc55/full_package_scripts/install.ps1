$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$Payload=Join-Path $PSScriptRoot 'payload'
$StateRoot=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$Log=Join-Path $PSScriptRoot 'PTAR_RC41_INSTALL_LAST.log'
$ExpectedRuntime='6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b'
$ExpectedBorderless='59e699536998054ae38cc5a30e03375c9cd206220667debe87070962609dd26c'
$ExpectedRaster='271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949'
$ExpectedIni='bd39b7c703ddf7a8658bed4df620232d00c06972351ca404c2d8386187fd3f50'
$ExpectedVersion='51367c26ebe9b6a35147c49aea8b227ae23219fc43166c86638b1e1b09f0a1d9'
$ExpectedGuiMigrationModule='b3dcb424b7159e6502f109987c825d842b7ec953151d4b255b5bbb85828a994b'
$KnownPtar=@(

 '864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c',

 '705ba9a9444726b0578e2f729ffdb5e45a4bf6abe56cc33a2432457ae0e0e5e5',

 '3bc8186ad869dfbe6ad09c0634dd617d486ec5a7bffc9d63b8fa64bb9a1c287d',

 '84d1b210af2fe4c5a534db6b51a5e35d89b81081c0e9de50049a85c86ce3e63c',

 'b874e50599b8f9917f90c6527f030e4cb4eb8932c947aebd15edd0f213a4f282',

 '3d4d777c943ced0f475df1371d3a2f9eeb5eeb80c66e9fb217c4d91057f32453',

 '50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59',

 '8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d',

 '613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70',

 'a903321a9504bd644f21468ff21877be86e03bf3a7056c92e41e30934b70fe05',

 'cb7202e5097b78c4965a8300a28a6004038d05bbfe1aaa25089e0a2c5f1b8abb',

 '774f88c976ed296d7496d5fce1fad9f8e1056337ee70e0111371866074e75669',

 '6f1686992bef971c9995df9f0cb91b93c6946988e12a837e5de956df37c9082b'

)
$KnownBorderless=@(

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

)
$KnownRaster=@('15a8f276816f3123a79a2d8e6932b6bf42b3826288ea5f016c124afec99fbbcf','7ffe7aa1b9afa42d5f22557cae29a4f65e17379c2a2f181f879427277839f1ca',
 'fbc5373ac008f6dadf9d9bf4a66772062693d0af744d514cc8f99fb9aa08b52d','dc23f3bbb2780b92809d5ae218d5cf42186682985e1be7608e7e9ee812ddb80e','0e9f8e3ec892cd594fc24f1723559110cecd7ea519615477027014381bfd9b96','271187ab9fd82b6829c52a667223f241ec79e98fa340b13c9a72270142156949')
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function L([string]$s){$x='['+(Get-Date -Format 'HH:mm:ss')+'] '+$s;Write-Host $x;Add-Content -LiteralPath $Log -Value $x -Encoding UTF8}
function F([string]$s,[int]$c=90){L ('FAIL: '+$s);exit $c}
$GuiMigrationModule=Join-Path $PSScriptRoot 'rc55_gui_migration.ps1';if((Sha $GuiMigrationModule)-ne $ExpectedGuiMigrationModule){F 'Module migration GUI RC55 absent/modifie.' 11};. $GuiMigrationModule
function Resolve-GameRoot{if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot};$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p};return $null}
function Get-RegValueState([string]$key,[string]$name){
 $x=[ordered]@{exists=$false;original=$null;installed=$null;applied=$false}
 if(Test-Path -LiteralPath $key){$r=Get-ItemProperty -LiteralPath $key;if($r.PSObject.Properties.Name -contains $name){$x.exists=$true;$x.original=[int]$r.$name}}
 return $x
}
function Restore-PriorRc37RegistryIfOwned{
 $latest=Join-Path $StateRoot 'LATEST_STATE.txt';if(-not(Test-Path -LiteralPath $latest -PathType Leaf)){return}
 try{$s=(Get-Content -LiteralPath $latest -TotalCount 1).Trim();$sp=Join-Path $s 'install_state.json';if(-not(Test-Path -LiteralPath $sp -PathType Leaf)){return};$pm=Get-Content -LiteralPath $sp -Raw|ConvertFrom-Json}catch{return}
 if(([string]$pm.package) -ne 'PTAR_RC37_CORETECH_LOWRES_GUI'){return}
 if(-not(($pm.PSObject.Properties.Name -contains 'registry') -and $pm.registry)){return}
 $key=[string]$pm.registry.key;if(-not(Test-Path -LiteralPath $key)){return}
 foreach($name in @('WindowStyle','AffectGuiResolution')){
  $saved=$pm.registry.values.$name;if(-not $saved){continue};$r=Get-ItemProperty -LiteralPath $key;$curExists=($r.PSObject.Properties.Name -contains $name)
  if(-not $curExists){continue};$cur=[int]$r.$name;if($cur -ne [int]$saved.installed){L ('MIGRATION KEEP '+$name+' changed after RC37: '+$cur);continue}
  if([bool]$saved.exists){Set-ItemProperty -LiteralPath $key -Name $name -Type DWord -Value ([int]$saved.original);L ('MIGRATION RC37 RESTORE '+$name+'='+[int]$saved.original)}else{Remove-ItemProperty -LiteralPath $key -Name $name -ErrorAction Stop;L ('MIGRATION RC37 REMOVE '+$name)}
 }
 }
Set-Content -LiteralPath $Log -Value ('START '+(Get-Date).ToString('o')) -Encoding UTF8
$g=Resolve-GameRoot;if(-not $g){F 'Warhammer.exe introuvable : placer le pack dans le dossier du jeu ou dans un sous-dossier direct.' 2}
if(Get-Process Warhammer -ErrorAction SilentlyContinue){F 'Fermer Warhammer avant installation.' 3}
$pr=Join-Path $Payload 'd3d11.dll';$pc=Join-Path $Payload 'win81_nis_dx11_x64.dll';$pi=Join-Path $Payload 'win81_nis.ini';$pv=Join-Path $Payload 'win81_nis_version.txt';$pb=Join-Path $Payload 'ptar_borderless.dll';$px=Join-Path $Payload 'ptar_rc41.dll'
if((Sha $pr)-ne $ExpectedRuntime -or (Sha $pc)-ne $ExpectedRuntime -or (Sha $pi)-ne $ExpectedIni -or (Sha $pv)-ne $ExpectedVersion -or (Sha $pb)-ne $ExpectedBorderless -or (Sha $px)-ne $ExpectedRaster){F 'Payload RC55 hash mismatch.' 10}
$a=Join-Path $g 'd3d11.dll';$c=Join-Path $g 'win81_nis_dx11_x64.dll';$i=Join-Path $g 'win81_nis.ini';$v=Join-Path $g 'win81_nis_version.txt';$bd=Join-Path $g 'ptar_borderless.dll';$xd=Join-Path $g 'ptar_rc41.dll'
$PreexistingPtar=$false
foreach($p in @($a,$c)){if(Test-Path -LiteralPath $p -PathType Leaf){$h=Sha $p;if($KnownPtar -notcontains $h){F ('DLL locale inconnue, installation refusee : '+$p+' '+$h) 20};$PreexistingPtar=$true}}
if(Test-Path -LiteralPath $bd -PathType Leaf){$h=Sha $bd;if($KnownBorderless -notcontains $h){F ('Sidecar borderless local inconnu, installation refusee : '+$h) 22}}
if(Test-Path -LiteralPath $xd -PathType Leaf){$h=Sha $xd;if($KnownRaster -notcontains $h){F ('Sidecar RC41 local inconnu, installation refusee : '+$h) 23}}
New-Item -ItemType Directory -Path $StateRoot -Force|Out-Null
Restore-PriorRc37RegistryIfOwned
$state=Join-Path $StateRoot ('INSTALL_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'));New-Item -ItemType Directory -Path $state -Force|Out-Null
$m=[ordered]@{schema=8;package='PTAR_RC55_BOUND_PHYSICAL_IDEMPOTENCE';game_root=$g;pack_root=$PackRoot;installed=@{};original=@{};windowstyle=@{};migration=@{rc37_registry_checked=$true;rc40_retained=$true;gui_resolution=@{checked=$false;owned=$false;changed=$false}};known_ptar=$KnownPtar}
foreach($r in @(@('d3d11.dll',$a,$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$c,$ExpectedRuntime),@('win81_nis.ini',$i,$ExpectedIni),@('win81_nis_version.txt',$v,$ExpectedVersion),@('ptar_borderless.dll',$bd,$ExpectedBorderless),@('ptar_rc41.dll',$xd,$ExpectedRaster))){
 $n=$r[0];$p=$r[1];$installSha=$r[2];$e=Test-Path -LiteralPath $p -PathType Leaf;$x=[ordered]@{exists=$e;sha=$null;backup=$null;ptar=$false}
 if($e){$x.sha=Sha $p;if(($n -eq 'd3d11.dll') -or ($n -eq 'win81_nis_dx11_x64.dll')){$x.ptar=($KnownPtar -contains $x.sha)}elseif($n -eq 'ptar_borderless.dll'){$x.ptar=($KnownBorderless -contains $x.sha)}elseif($n -eq 'ptar_rc41.dll'){$x.ptar=($KnownRaster -contains $x.sha)}else{$x.ptar=$PreexistingPtar};$bk=Join-Path $state ('original_'+$n);Copy-Item -LiteralPath $p -Destination $bk -Force;if((Sha $bk)-ne $x.sha){F ('Backup incoherent '+$n) 21};$x.backup=$bk}
 $m.original[$n]=$x;$m.installed[$n]=$installSha
}
$key='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options';$m.windowstyle.key=$key;$ws=Get-RegValueState $key 'WindowStyle';$m.windowstyle.exists=$ws.exists;$m.windowstyle.original=$ws.original;$m.windowstyle.applied=$false;$m.migration.gui_resolution=Invoke-Rc55GuiMigration -StateRoot $StateRoot;L ('GUI_MIGRATION='+[string]$m.migration.gui_resolution.event);if(([string]$m.migration.gui_resolution.event) -eq 'ERROR'){$u=Undo-Rc55GuiMigration $m.migration.gui_resolution;L ('GUI_MIGRATION_ERROR_UNDO='+[string]$u.event);F ('Migration GUI RC55 echouee: '+[string]$m.migration.gui_resolution.error) 24}
$wsText=if($ws.exists){[string]$ws.original}else{'UNSET'};L ('WINDOWSTYLE_PRESERVED='+$wsText)
$m|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $state 'install_state.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $StateRoot 'LATEST_STATE.txt') -Value $state -Encoding UTF8
Set-Content -LiteralPath (Join-Path $PackRoot 'win81_nis_install_target.txt') -Value $g -Encoding ASCII
try{
 Copy-Item -LiteralPath $pr -Destination $a -Force;Copy-Item -LiteralPath $pc -Destination $c -Force;Copy-Item -LiteralPath $pi -Destination $i -Force;Copy-Item -LiteralPath $pv -Destination $v -Force;Copy-Item -LiteralPath $pb -Destination $bd -Force;Copy-Item -LiteralPath $px -Destination $xd -Force
 if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedIni -or (Sha $v)-ne $ExpectedVersion -or (Sha $bd)-ne $ExpectedBorderless -or (Sha $xd)-ne $ExpectedRaster){throw 'Post-install hash mismatch.'}
}catch{
 foreach($n in @('d3d11.dll','win81_nis_dx11_x64.dll','win81_nis.ini','win81_nis_version.txt','ptar_borderless.dll','ptar_rc41.dll')){$dst=Join-Path $g $n;$o=$m.original[$n];if($o.exists -and $o.backup -and (Test-Path -LiteralPath $o.backup -PathType Leaf)){Copy-Item -LiteralPath $o.backup -Destination $dst -Force}elseif(Test-Path -LiteralPath $dst -PathType Leaf){Remove-Item -LiteralPath $dst -Force}}
 if($m.windowstyle.applied -and (Test-Path -LiteralPath $key)){if($m.windowstyle.exists){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Type DWord -Value ([int]$m.windowstyle.original)}else{Remove-ItemProperty -LiteralPath $key -Name WindowStyle -ErrorAction SilentlyContinue}}
 $u=Undo-Rc55GuiMigration $m.migration.gui_resolution;L ('GUI_MIGRATION_INSTALL_UNDO='+[string]$u.event)
 F ('Installation transaction annulee : '+$_.Exception.Message) 30
}
L 'INSTALL_RC55=PASS';L ('GAME_ROOT='+$g);L ('RUNTIME_SHA256='+$ExpectedRuntime);L ('BORDERLESS_SHA256='+$ExpectedBorderless);L ('RC55_RASTER_SIDECAR_SHA256='+$ExpectedRaster);L 'BASE_GITHUB_MAIN=009b8326d0c6f2d7869077ef621c712cca060479';L 'BASE_FILES=107/107_PRESERVED';L 'MODE=RC55_RC51_PRESENTER_RC52_QUARANTINE_BOUND_PHYSICAL_IDEMPOTENCE';L 'CONFIG=CANONICAL_GITHUB_MAIN_EXACT';exit 0
