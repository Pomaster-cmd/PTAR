$ErrorActionPreference='Stop'
$PackRoot=Split-Path -Parent $PSScriptRoot
function Sha([string]$p){if(Test-Path -LiteralPath $p -PathType Leaf){return (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}return 'ABSENT'}
$g=$null
if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){$g=$PackRoot}else{$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){$g=$p}}
if(-not $g){Write-Host '[FAIL] Warhammer.exe introuvable';exit 2}
$canonical=Join-Path $PackRoot 'diag\collect.ps1'
if(-not(Test-Path -LiteralPath $canonical -PathType Leaf)){Write-Host '[FAIL] Collecteur canonique absent';exit 3}
$ps="$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
$arg='-NoLogo -NoProfile -ExecutionPolicy Bypass -File "'+$canonical+'"'
$p=Start-Process -FilePath $ps -ArgumentList $arg -Wait -PassThru
if($p.ExitCode -ne 0){Write-Host ('[FAIL] Collecteur canonique RC='+$p.ExitCode);exit $p.ExitCode}
$z=Get-ChildItem -LiteralPath $g -Filter 'PTAR_GW16H_RESULTS_*.zip' -File | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
if(-not $z){Write-Host '[FAIL] ZIP canonique introuvable';exit 4}
$tmp=Join-Path $env:TEMP ('PTAR_RC39_DIAG_'+[guid]::NewGuid().ToString('N')+'.txt')
$runtime=Join-Path $g 'd3d11.dll';$side=Join-Path $g 'ptar_borderless.dll';$ver=Join-Path $g 'win81_nis_version.txt'
@(
 'RC39_DIAGNOSTIC=1',
 ('RUNTIME_SHA256='+(Sha $runtime)),
 ('SIDECAR_SHA256='+(Sha $side)),
 ('VERSION_SHA256='+(Sha $ver)),
 ('SIDECAR_LOG_NAME=ptar_borderless_rc38.log'),
 'NOTE=The sidecar log keeps the RC38 filename because RC39 reuses the validated base controller.'
) | Set-Content -LiteralPath $tmp -Encoding ASCII
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$a=[IO.Compression.ZipFile]::Open($z.FullName,[IO.Compression.ZipArchiveMode]::Update)
try{
 foreach($spec in @(@((Join-Path $g 'ptar_borderless_rc38.log'),'ptar_borderless_rc38.log'),@($tmp,'RC39_ACTIVE_PAYLOAD_SHA256.txt'))){
  $src=$spec[0];$name=$spec[1]
  if(Test-Path -LiteralPath $src -PathType Leaf){
   $old=$a.GetEntry($name);if($old){$old.Delete()}
   [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($a,$src,$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null
  }
 }
 if(-not(Test-Path -LiteralPath (Join-Path $g 'ptar_borderless_rc38.log') -PathType Leaf)){
  $e=$a.CreateEntry('RC39_SIDECAR_LOG_ABSENT.txt');$w=New-Object IO.StreamWriter($e.Open());try{$w.WriteLine('ptar_borderless_rc38.log absent')}finally{$w.Dispose()}
 }
}finally{$a.Dispose();Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$out=Join-Path $g ('PTAR_RC39_RESULTS_'+$stamp+'.zip')
Move-Item -LiteralPath $z.FullName -Destination $out -Force
Write-Host ('RESULTAT_RC39='+$out)
Write-Host '[PASS] Collecte canonique + journal sidecar RC39 ajoute.';exit 0
