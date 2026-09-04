$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$Payload=Join-Path $PackRoot 'payload'
$StateRoot=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$Log=Join-Path $PSScriptRoot 'PTAR_GW15_INSTALL_LAST.log'
$ExpectedRuntime='2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d'
$ExpectedIni='5f1ee54060411ba5b555378cddd3e6d658366a6c8ecc5e9660343e1ef0d0312b'
$ExpectedVersion='23f1dc86bc6e9c0f7863223acf618d2503795c34146e4947efc1795806634eaa'
$KnownPtar=@(
 '60f88d6175c3a42f2a082211503f391309d1381c897909d8dec86398a8c392df',
 '3d4d777c943ced0f475df1371d3a2f9eeb5eeb80c66e9fb217c4d91057f32453',
 '019cbf3d94b04b58142ffb45fda90fea0bed5dd408030909b08b85a8b10eaedf',
 '7fbf685fedeec21b635b4f54e6efd5f76afb9b58e532d13f8e7a2e8f4769674e',
 '46397c0f4f22d767021beb20ea26a0b6a8aa42fef3a1ccaf9cfd276f6c664c28',
 'ebd3fa8ef42ce2752fbd8357b88fb91c93ef1de00b11ea68ec234087916df997',
 '70b86c7a95a2623ce31c0379a758b7ad395eb3607e73896be8af43e664db8e37',
 'e20bb98469a3e2136d6da59f439aba4d2da575fd447d379aa3afb40ca7562fd2',
 '72d991276b44af5d6d64e2a6aa666148c91beb92aece9957df29ec05eb9c147d',
 '878260b161bdff4d71975c7ae88e29d4c07cbacaf64fa04c2a55bbc98afce690',
 'e7ad4913179ffbd8ca97ca8176e71bee9e0ad84e33b01a7346dfb74ff6ec1974',
 '40f0d3edf339c08bcfe64c22e146a49478e16eeb1a56aa090a8270a2e610ee6f',
 'a185a69332e470eccf1b077d922cd593596a2386bc5c193cf54c713c7f016088',
 '03640c417d5a1b695e8a1082e1cf1fba0aa69f8313bcfe4fb12255b131709458',
 '50cf02fee971e615f0dba26a7614e27b833486a993cf569fe5369a0fa5b41f59',
 '2fbd2343803af619621282fface48c469092c16d5139ec0ce52b35affb83d29d',
 '7f12ff11aaa59e86923d4a697cff584df05faa4401e9122b1898bae0452d40e2'
)
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function L([string]$s){$x='['+(Get-Date -Format 'HH:mm:ss')+'] '+$s;Write-Host $x;Add-Content -LiteralPath $Log -Value $x -Encoding UTF8}
function F([string]$s,[int]$c=90){L ('FAIL: '+$s);exit $c}
function Resolve-GameRoot{
 if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){return $PackRoot}
 $p=Split-Path -Parent $PackRoot
 if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){return $p}
 return $null
}
Set-Content -LiteralPath $Log -Value ('START '+(Get-Date).ToString('o')) -Encoding UTF8
$g=Resolve-GameRoot;if(-not $g){F 'Warhammer.exe introuvable : placer le pack dans le dossier du jeu ou dans un sous-dossier direct.' 2}
if(Get-Process Warhammer -ErrorAction SilentlyContinue){F 'Fermer Warhammer avant installation.' 3}
$pr=Join-Path $Payload 'd3d11.dll';$pc=Join-Path $Payload 'win81_nis_dx11_x64.dll';$pi=Join-Path $Payload 'win81_nis.ini';$pv=Join-Path $Payload 'win81_nis_version.txt'
if((Sha $pr)-ne $ExpectedRuntime -or (Sha $pc)-ne $ExpectedRuntime -or (Sha $pi)-ne $ExpectedIni -or (Sha $pv)-ne $ExpectedVersion){F 'Payload hash mismatch.' 10}
$a=Join-Path $g 'd3d11.dll';$c=Join-Path $g 'win81_nis_dx11_x64.dll';$i=Join-Path $g 'win81_nis.ini';$v=Join-Path $g 'win81_nis_version.txt'
$PreexistingPtar=$false
foreach($p in @($a,$c)){
 if(Test-Path -LiteralPath $p -PathType Leaf){
  $h=Sha $p
  if($KnownPtar -notcontains $h){F ('DLL locale inconnue, installation refusee : '+$p+' '+$h) 20}
  $PreexistingPtar=$true
 }
}
New-Item -ItemType Directory -Path $StateRoot -Force|Out-Null
$state=Join-Path $StateRoot ('INSTALL_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'));New-Item -ItemType Directory -Path $state -Force|Out-Null
$m=[ordered]@{schema=3;package='GW15_LOCK30';game_root=$g;pack_root=$PackRoot;installed=@{};original=@{};windowstyle=@{};known_ptar=$KnownPtar}
foreach($r in @(@('d3d11.dll',$a,$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$c,$ExpectedRuntime),@('win81_nis.ini',$i,$ExpectedIni),@('win81_nis_version.txt',$v,$ExpectedVersion))){
 $n=$r[0];$p=$r[1];$installSha=$r[2];$e=Test-Path -LiteralPath $p -PathType Leaf
 $x=[ordered]@{exists=$e;sha=$null;backup=$null;ptar=$false}
 if($e){
  $x.sha=Sha $p
  if(($n -eq 'd3d11.dll') -or ($n -eq 'win81_nis_dx11_x64.dll')){$x.ptar=($KnownPtar -contains $x.sha)}else{$x.ptar=$PreexistingPtar}
  $bk=Join-Path $state ('original_'+$n);Copy-Item -LiteralPath $p -Destination $bk -Force;if((Sha $bk)-ne $x.sha){F ('Backup incoherent '+$n) 21};$x.backup=$bk
 }
 $m.original[$n]=$x;$m.installed[$n]=$installSha
}
$key='HKCU:\Software\NeoCore Games\Warhammer Martyr\Options';$m.windowstyle.key=$key;$m.windowstyle.exists=$false;$m.windowstyle.original=$null;$m.windowstyle.applied=$false
if(Test-Path -LiteralPath $key){$r=Get-ItemProperty -LiteralPath $key;if($r.PSObject.Properties.Name -contains 'WindowStyle'){$m.windowstyle.exists=$true;$m.windowstyle.original=[int]$r.WindowStyle;if([int]$r.WindowStyle -ne 1){Set-ItemProperty -LiteralPath $key -Name WindowStyle -Value 1;$m.windowstyle.applied=$true}}}
$m|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $state 'install_state.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $StateRoot 'LATEST_STATE.txt') -Value $state -Encoding UTF8
Copy-Item -LiteralPath $pr -Destination $a -Force;Copy-Item -LiteralPath $pc -Destination $c -Force;Copy-Item -LiteralPath $pi -Destination $i -Force;Copy-Item -LiteralPath $pv -Destination $v -Force
if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedIni -or (Sha $v)-ne $ExpectedVersion){F 'Post-install hash mismatch.' 30}
L 'INSTALL=PASS';L ('GAME_ROOT='+$g);L ('RUNTIME_SHA256='+$ExpectedRuntime);L 'GW15 LOCK30: GW13 Sync2 runtime retained byte-for-byte; FrameGenerationTargetFPS=30 => REAL governor target 15 FPS. Safe uninstaller retained.';exit 0
