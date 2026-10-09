param([ValidateSet('PRESENTSHED60','PRESENTSHED120')][string]$Mode='PRESENTSHED60')
$ErrorActionPreference='Stop'
$Tools=$PSScriptRoot;$RawCompare3=Split-Path -Parent $Tools;$Diag=Split-Path -Parent $RawCompare3;$D9=Split-Path -Parent $Diag
$expected='20e7f725d9aa391de43e11c0d0496135b6fa89347e6defad9b0481137b553846'
function Fail([int]$Code,[string]$Message){Write-Host ('[FAIL] '+$Message);exit $Code}
$targetFile=Join-Path $D9 'PTAR_X86_D3D9_LAST_TARGET.txt';if(-not(Test-Path -LiteralPath $targetFile -PathType Leaf)){Fail 20 'Installation D3D9 non memorisee.'}
$gameExe=(Get-Content -LiteralPath $targetFile -TotalCount 1).Trim().Trim([char]0xFEFF).Trim('"');if(-not(Test-Path -LiteralPath $gameExe -PathType Leaf)){Fail 21 'Cible jeu invalide.'};$g=Split-Path -Parent $gameExe
$runtime=Join-Path $g 'd3d9.dll';if(-not(Test-Path -LiteralPath $runtime -PathType Leaf)){Fail 22 'd3d9.dll actif absent.'};$got=(Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash.ToLowerInvariant();if($got -ne $expected){Fail 23 ('D3D9 Legacy Repair V1 requis. Runtime actif: '+$got)};$variant='D3D9_LEGACY_REPAIR_V1_D3D11_DIAG_PORT_V2'
$ini=Join-Path $g 'win81_nis.ini';if(Test-Path -LiteralPath $ini -PathType Leaf){if(-not ('PTARD3D9ReadIniF1' -as [type])){Add-Type @"
using System; using System.Runtime.InteropServices;
public static class PTARD3D9ReadIniF1 { [DllImport("kernel32.dll",CharSet=CharSet.Unicode,EntryPoint="GetPrivateProfileIntW")] public static extern int GetPrivateProfileInt(string app,string key,int def,string fileName); }
"@
};$diag=[PTARD3D9ReadIniF1]::GetPrivateProfileInt('WIN81_NIS','VBlankDiagnostics',1,$ini);if($diag -ne 1){Fail 27 'VBlankDiagnostics=1 requis au chargement du runtime D3D9.'}}
$ver=[string]$env:PTAR_PRESENTSHED1_VERIFIER_EXE;if([string]::IsNullOrWhiteSpace($ver) -or -not(Test-Path -LiteralPath $ver -PathType Leaf)){Fail 28 'Verifier PRESENTSHED1 absent.'}
$resultDir=[string]$env:PTAR_PRESENTSHED1_RESULT_DIR;if([string]::IsNullOrWhiteSpace($resultDir)){$resultDir=Join-Path $g 'diag\PTAR_PRESENTSHED1'};[IO.Directory]::CreateDirectory($resultDir)|Out-Null;$session=[string]$env:PTAR_PRESENTSHED1_SESSION_ID;if([string]::IsNullOrWhiteSpace($session)){$session=Get-Date -Format 'yyyyMMdd_HHmmss_fff'}
$duration=$(if($Mode -eq 'PRESENTSHED120'){120}else{60});$stamp=Get-Date -Format 'yyyyMMdd_HHmmss_fff';$prefix='PTAR_'+$variant+'_'+$session+'_'+$Mode+'_'+$stamp;$capturePrefix=Join-Path $resultDir $prefix;$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=$capturePrefix
& $ver $duration '--autostart';$rc=$LASTEXITCODE;if($rc -ne 0){exit $rc}
$out=$capturePrefix+'_OUTPUT.txt';$csv=$capturePrefix+'_SAMPLES.csv';$status=$capturePrefix+'_STATUS.txt';$rr=$capturePrefix+'_RR_FOCUS.txt';$lh=$capturePrefix+'_LONG_HOLD_FOCUS.txt';$gp=$capturePrefix+'_GEN_PRESSURE_FOCUS.txt';$metrics=$capturePrefix+'_METRICS.txt';$fluid=$capturePrefix+'_RG_DIAG.txt';$mi=$capturePrefix+'_MARKER_INTEGRITY.txt';$percept=$capturePrefix+'_PERCEPTUAL.txt'
foreach($p in @($out,$csv,$status,$rr,$lh,$gp)){if(-not(Test-Path -LiteralPath $p -PathType Leaf)){Fail 43 ('Sortie telemetrie D3D9 incomplete: '+$p)}}
& $env:PTAR_PS_EXE -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Tools 'analyze_visible.ps1') -CsvPath $csv -OutputPath $out -OutPath $metrics -Label 'FG_ACTIVE' -Variant $variant -RuntimeSha256 $got;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $env:PTAR_PS_EXE -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Tools 'analyze_rg.ps1') -CsvPath $csv -OutPath $fluid -Variant $variant -RuntimeSha256 $got;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $env:PTAR_PS_EXE -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Tools 'analyze_marker_integrity.ps1') -CsvPath $csv -OutPath $mi -Variant $variant -RuntimeSha256 $got;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& $env:PTAR_PS_EXE -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Tools 'build_perceptual_summary.ps1') -MetricsPath $metrics -FluidityPath $fluid -RrPath $rr -LongHoldPath $lh -MarkerIntegrityPath $mi -OutPath $percept;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
$kv=@{};foreach($ln in Get-Content -LiteralPath $metrics){if($ln -match '^([^=]+)=(.*)$'){$kv[$matches[1]]=$matches[2]}};$gen=0;$real=0;[int]::TryParse([string]$kv['GENERATED_CONTENTS'],[ref]$gen)|Out-Null;[int]::TryParse([string]$kv['REAL_CONTENTS'],[ref]$real)|Out-Null;if($gen -le 0 -or $real -le 0){Fail 25 ('FG ON non confirme par la telemetrie Present D3D9: REAL='+$real+' GENERATED='+$gen)}
$log=Join-Path $g 'PTAR_X86_D3D9.log';if(Test-Path -LiteralPath $log -PathType Leaf){Copy-Item -LiteralPath $log -Destination ($capturePrefix+'_RUNTIME.log')}
$meta=@('PTAR_PRESENTSHED1_RESULT=1','BACKEND=D3D9_X86','SESSION_ID='+$session,'VARIANT='+$variant,'MODE='+$Mode,'RUNTIME_SHA256='+$got,'PREFIX='+$prefix,'CAPTURED_AT='+(Get-Date).ToString('o'));[IO.File]::WriteAllLines(($capturePrefix+'_META.txt'),$meta,(New-Object Text.UTF8Encoding($false)));[IO.File]::AppendAllText((Join-Path $resultDir 'PTAR_PRESENTSHED1_INDEX.txt'),($prefix+' | '+$Mode+' | '+$got+[Environment]::NewLine),(New-Object Text.UTF8Encoding($false)))
Write-Host ('[PASS] '+$Mode+' D3D9 -> '+$percept);exit 0
