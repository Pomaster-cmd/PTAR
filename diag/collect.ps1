$ErrorActionPreference='Stop';$PackRoot=Split-Path -Parent $PSScriptRoot;$g=$null
if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){$g=$PackRoot}else{$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){$g=$p}}
if(-not $g){Write-Host '[FAIL] Warhammer.exe introuvable';exit 2}
$st=Get-Date -Format 'yyyyMMdd_HHmmss';$d=Join-Path $PSScriptRoot ('collect_'+$st);New-Item -ItemType Directory -Path $d -Force|Out-Null
$gameFiles=@(
 'win81_nis.log','win81_nis.ini','win81_nis_version.txt',
 'PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt','PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt','PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv','PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt'
)
foreach($n in $gameFiles){$p=Join-Path $g $n;if(Test-Path -LiteralPath $p -PathType Leaf){Copy-Item -LiteralPath $p -Destination (Join-Path $d $n) -Force}}
foreach($n in @('PTAR_GW15_INSTALL_LAST.log','LAB_STATIC_VALIDATION.txt','BUILD_MANIFEST.json','GW13_PACING_FINDING.txt','GW14_FIELD_FINDING.txt','GW15_RUNTIME_VALIDATION.txt','PACINGVERIFIER2_VALIDATION.txt')){$p=Join-Path $PSScriptRoot $n;if(Test-Path -LiteralPath $p -PathType Leaf){Copy-Item -LiteralPath $p -Destination (Join-Path $d $n) -Force}}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z=Join-Path $g ('PTAR_GW15_RESULTS_'+$st+'.zip')
if(Test-Path -LiteralPath $z){Remove-Item -LiteralPath $z -Force}
[IO.Compression.ZipFile]::CreateFromDirectory($d,$z,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Host ('RESULTAT='+$z);exit 0
