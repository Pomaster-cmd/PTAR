param([string]$GameExe='')
$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
$Payload=Join-Path $PackRoot 'payload'
$StateRoot=Join-Path $PackRoot '_PTAR_UNINSTALL\state'
$Log=Join-Path $PSScriptRoot 'PTAR_INSTALL_LAST.log'
$ExpectedRuntime='e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429'
$ExpectedIniTemplate='dea8a93e97d3ad1b438973822d67ca9ac12477b5774933eea135ab71776c5648'
$ExpectedVersion='bf31c0593b5fec78c533ba6f994866bc8d4921139bff4d602fde292c6a376f15'
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
 '7f12ff11aaa59e86923d4a697cff584df05faa4401e9122b1898bae0452d40e2',
 '94dc7d3f9890bd8a1ded0b215e2e9b86c46da65611360ca2c03ea02c047e69ec',
 '422c81694d43956a1eda22793a10f415ab6873351b9dfbfadabc6cb2b2aecd96',
 '0a494b58138ebb62eb745c3b07c3669571901ef564921b8c619ed35e7733b10c',
 '8a14244eff0acfaa0f6d2ba238f27472da27ea3fe9d6b30c25a2a107a3972834',
 'ab471f9fd3236a630adabfd9cc761ecefd0887d91f2f97af1c74d1a34a1f8e38',
 '0529f6d64ccc7dcf9fe01d439dc9a4fc67f7e909334f5940bdbb5c09de6b497e',
 'b8d5804977ff175bb5e5638d28269c4788a0de209dd24ace7204d94f1fa583ea',
 '8481ef8d8694e1f1978191e55c30098c1e836cb1c3defade1e3678956402b84d',
 '4c5bc494df5c6feaf01acf37a71ad486eac94b07fb82b9f5f58a78bc6801620f',
 '6621dc14abb4f7c28f4d14568b2549f822da298fc6db9076dff7f97f252d2cf7',
 '7c56148f4d00f7623c4bf44e5f460d81145b660ff5743a5ea5b57610c1d0c630',
 '961b7c0211239364920516e990f9f5a6a3284709bcd4a2a17e6a21b1a76556f2',
 '1b8e8673ff2b046604b282c55d4d9bcfe053ad046fa2c30a69a6b6ff5ffe6e8b',
 '50a329275f410bea032b3a50e81a7838f01f1ef22ee916ad4285df79d146ed92',
 'e7c1b14f263505a64442d16aa1088c43b04a81665641c159a3da1181267bac53',
 'c573b4c4ca102867ca67dbd16e1e6d36e3057f43d85b343fca94803000de0040',
 '613714f5ac70bc94867a3044dd067f4de18bb3ef65262c60f0febce2ca9d4c70',
 '864c0ca8f24f22f6a3cd4c21a0e213f431f5268e69b04e3860c52fe72600fc3c',
 'e81e4c6239462bc7a93c3fd7d7abb4bd96e09db1f013eb48a46f40341ffa6429'
)
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return $null}
function L([string]$s){$x='['+(Get-Date -Format 'HH:mm:ss')+'] '+$s;Write-Host $x;Add-Content -LiteralPath $Log -Value $x -Encoding UTF8}
function F([string]$s,[int]$c=90){L ('FAIL: '+$s);exit $c}
function Full([string]$p){return [IO.Path]::GetFullPath($p.Trim().Trim('"'))}
function Test-Pe64([string]$p){
 try{
  $fs=[IO.File]::OpenRead($p)
  try{
   if($fs.Length -lt 256){return $false}
   $br=New-Object IO.BinaryReader($fs)
   if($br.ReadUInt16() -ne 0x5A4D){return $false}
   $fs.Position=0x3C;$pe=$br.ReadInt32()
   if($pe -lt 0 -or $pe+6 -gt $fs.Length){return $false}
   $fs.Position=$pe
   if($br.ReadUInt32() -ne 0x00004550){return $false}
   return ($br.ReadUInt16() -eq 0x8664)
  }finally{$fs.Dispose()}
 }catch{return $false}
}
function Is-PackageHelper([IO.FileInfo]$f){
 $n=$f.Name.ToLowerInvariant()
 if($n -match '^(b18|ffmpeg|ptar|qsv|dxdiag|unins|setup|install|verify|collect|rollback)'){return $true}
 return $false
}
function Resolve-RequestedExe([string]$raw){
 if([string]::IsNullOrWhiteSpace($raw)){return $null}
 $raw=$raw.Trim().Trim('"')
 if(-not [IO.Path]::IsPathRooted($raw)){$raw=Join-Path $PackRoot $raw}
 $p=Full $raw
 if(-not(Test-Path -LiteralPath $p -PathType Leaf)){F ('Executable cible introuvable : '+$p) 2}
 if([IO.Path]::GetExtension($p) -ine '.exe'){F ('La cible doit etre un .exe : '+$p) 2}
 if(-not(Test-Pe64 $p)){F ('Executable non x64 / PE64 non compatible avec ce runtime : '+$p) 2}
 return $p
}
function Get-LocalCandidates{
 $dirs=New-Object Collections.Generic.List[string]
 $dirs.Add($PackRoot)
 $parent=Split-Path -Parent $PackRoot
 if($parent -and ($parent -ne $PackRoot)){$dirs.Add($parent)}
 $seen=@{};$out=New-Object Collections.Generic.List[string]
 foreach($d in $dirs){
  if(-not(Test-Path -LiteralPath $d -PathType Container)){continue}
  foreach($f in @(Get-ChildItem -LiteralPath $d -Filter '*.exe' -File -ErrorAction SilentlyContinue)){
   if(Is-PackageHelper $f){continue}
   if(-not(Test-Pe64 $f.FullName)){continue}
   $k=$f.FullName.ToLowerInvariant()
   if(-not $seen.ContainsKey($k)){$seen[$k]=$true;$out.Add($f.FullName)}
  }
 }
 return @($out.ToArray())
}
function Select-TargetExe{
 $p=Resolve-RequestedExe $GameExe
 if($p){return $p}
 $p=Resolve-RequestedExe ([string]$env:PTAR_GAME_EXE)
 if($p){return $p}
 $hint=Join-Path $PackRoot 'PTAR_TARGET_EXE.txt'
 if(Test-Path -LiteralPath $hint -PathType Leaf){
  $v=(Get-Content -LiteralPath $hint -TotalCount 1).Trim()
  $p=Resolve-RequestedExe $v
  if($p){return $p}
 }
 $c=@(Get-LocalCandidates)
 if($c.Count -eq 1){L ('Cible x64 detectee automatiquement : '+$c[0]);return $c[0]}
 if($c.Count -gt 1){
  Write-Host ''
  Write-Host 'Executables x64 detectes :'
  for($i=0;$i -lt $c.Count;$i++){Write-Host ('  ['+($i+1)+'] '+$c[$i])}
  while($true){
   $r=Read-Host 'Numero de l executable de rendu a cibler, ou chemin complet'
   $num=0
   if([int]::TryParse($r,[ref]$num) -and $num -ge 1 -and $num -le $c.Count){return $c[$num-1]}
   $p=Resolve-RequestedExe $r
   if($p){return $p}
  }
 }
 Write-Host ''
 Write-Host 'Aucun executable x64 unique detecte a cote du pack.'
 Write-Host 'Indique le chemin complet de l executable de rendu du jeu (ex. ...\Binaries\Win64\Game.exe).'
 while($true){
  $r=Read-Host 'Executable cible'
  $p=Resolve-RequestedExe $r
  if($p){return $p}
 }
}
function Is-TargetRunning([string]$exe){
 $want=Full $exe
 foreach($p in @(Get-Process -ErrorAction SilentlyContinue)){
  try{if((Full $p.MainModule.FileName) -ieq $want){return $true}}catch{}
 }
 return $false
}
Set-Content -LiteralPath $Log -Value ('START '+(Get-Date).ToString('o')) -Encoding UTF8
$TargetExePath=Select-TargetExe
$TargetExeName=[IO.Path]::GetFileName($TargetExePath)
$g=Split-Path -Parent $TargetExePath
if(Is-TargetRunning $TargetExePath){F ('Fermer le jeu avant installation : '+$TargetExePath) 3}
$pr=Join-Path $Payload 'd3d11.dll';$pc=Join-Path $Payload 'win81_nis_dx11_x64.dll';$pi=Join-Path $Payload 'win81_nis.ini';$pv=Join-Path $Payload 'win81_nis_version.txt'
if((Sha $pr)-ne $ExpectedRuntime -or (Sha $pc)-ne $ExpectedRuntime -or (Sha $pi)-ne $ExpectedIniTemplate -or (Sha $pv)-ne $ExpectedVersion){F 'Payload hash mismatch.' 10}
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

# Build target-specific INI from immutable package template.
$iniLines=[IO.File]::ReadAllLines($pi)
$targetCount=0
for($x=0;$x -lt $iniLines.Length;$x++){
 if($iniLines[$x] -match '^[ \t]*TargetExe[ \t]*='){$iniLines[$x]='TargetExe='+$TargetExeName;$targetCount++}
}
if($targetCount -ne 1){F ('Template INI invalide : TargetExe count='+$targetCount) 11}
$generatedIni=Join-Path $state 'generated_win81_nis.ini'
[IO.File]::WriteAllLines($generatedIni,$iniLines,[Text.Encoding]::ASCII)
$ExpectedInstalledIni=Sha $generatedIni

$m=[ordered]@{schema=4;package='GW16H_UNIFIEDREC3_SAFEPOINT11_FUSEDDETAIL1_HUDREC1_UNIVERSAL1';game_root=$g;target_exe=$TargetExePath;target_exe_name=$TargetExeName;pack_root=$PackRoot;installed=@{};original=@{};known_ptar=$KnownPtar}
foreach($r in @(@('d3d11.dll',$a,$ExpectedRuntime),@('win81_nis_dx11_x64.dll',$c,$ExpectedRuntime),@('win81_nis.ini',$i,$ExpectedInstalledIni),@('win81_nis_version.txt',$v,$ExpectedVersion))){
 $n=$r[0];$p=$r[1];$installSha=$r[2];$e=Test-Path -LiteralPath $p -PathType Leaf
 $x=[ordered]@{exists=$e;sha=$null;backup=$null;ptar=$false}
 if($e){
  $x.sha=Sha $p
  if(($n -eq 'd3d11.dll') -or ($n -eq 'win81_nis_dx11_x64.dll')){$x.ptar=($KnownPtar -contains $x.sha)}else{$x.ptar=$PreexistingPtar}
  $bk=Join-Path $state ('original_'+$n);Copy-Item -LiteralPath $p -Destination $bk -Force;if((Sha $bk)-ne $x.sha){F ('Backup incoherent '+$n) 21};$x.backup=$bk
 }
 $m.original[$n]=$x;$m.installed[$n]=$installSha
}
$m|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $state 'install_state.json') -Encoding UTF8
Set-Content -LiteralPath (Join-Path $StateRoot 'LATEST_STATE.txt') -Value $state -Encoding UTF8
Set-Content -LiteralPath (Join-Path $PackRoot 'win81_nis_install_target.txt') -Value $g -Encoding ASCII
Set-Content -LiteralPath (Join-Path $PackRoot 'win81_nis_install_exe.txt') -Value $TargetExePath -Encoding UTF8
Copy-Item -LiteralPath $pr -Destination $a -Force
Copy-Item -LiteralPath $pc -Destination $c -Force
Copy-Item -LiteralPath $generatedIni -Destination $i -Force
Copy-Item -LiteralPath $pv -Destination $v -Force
if((Sha $a)-ne $ExpectedRuntime -or (Sha $c)-ne $ExpectedRuntime -or (Sha $i)-ne $ExpectedInstalledIni -or (Sha $v)-ne $ExpectedVersion){F 'Post-install hash mismatch.' 30}
L 'INSTALL=PASS';L ('TARGET_EXE='+$TargetExePath);L ('GAME_ROOT='+$g);L ('RUNTIME_SHA256='+$ExpectedRuntime)
L 'UNIVERSAL1: aucun nom de jeu ni registre jeu specifique dans le chemin d installation.'
exit 0
