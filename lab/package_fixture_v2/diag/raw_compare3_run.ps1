param([Parameter(Mandatory=$true)][ValidateSet('PRE_FG','FG_ACTIVE','POST_FG')][string]$Label)
$ErrorActionPreference='Stop'
$D9=Split-Path -Parent $PSScriptRoot
$PsExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$expected='20e7f725d9aa391de43e11c0d0496135b6fa89347e6defad9b0481137b553846'
$targetFile=Join-Path $D9 'PTAR_X86_D3D9_LAST_TARGET.txt'
if(-not(Test-Path -LiteralPath $targetFile -PathType Leaf)){exit 20}
$gameExe=(Get-Content -LiteralPath $targetFile -TotalCount 1).Trim().Trim([char]0xFEFF).Trim('"')
if(-not(Test-Path -LiteralPath $gameExe -PathType Leaf)){exit 21}
$g=Split-Path -Parent $gameExe
$runtime=Join-Path $g 'd3d9.dll';if(-not(Test-Path -LiteralPath $runtime -PathType Leaf)){exit 22}
$got=(Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash.ToLowerInvariant();if($got -ne $expected){exit 23}
$log=Join-Path $g 'PTAR_X86_D3D9.log'
$env:PTAR_GAME_ROOT=$g;$env:PTAR_TARGET_EXE=$gameExe
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss_fff';$session=[string]$env:PTAR_RAWCOMPARE3_SESSION_ID;if([string]::IsNullOrWhiteSpace($session)){$session='NOSESSION'};$prefix='PTAR_RAWCOMPARE_'+$session+'_'+$Label+'_'+$stamp
$metrics=Join-Path $g ($prefix+'_METRICS.txt')
if($Label -eq 'FG_ACTIVE'){
  $helper=Join-Path $PSScriptRoot 'set_vblank_diagnostics.ps1';$vsrc=Join-Path $PSScriptRoot 'visible_pacing\PTARVisiblePacingVerifier.cs';$an=Join-Path $PSScriptRoot 'analyze_raw_visible.ps1'
  $state=1;$ini=Join-Path $g 'win81_nis.ini'
  if(Test-Path -LiteralPath $ini -PathType Leaf){
    if(-not ('PTARD3D9ReadIni' -as [type])){Add-Type @"
using System; using System.Runtime.InteropServices;
public static class PTARD3D9ReadIni { [DllImport("kernel32.dll",CharSet=CharSet.Unicode,EntryPoint="GetPrivateProfileIntW")] public static extern int GetPrivateProfileInt(string app,string key,int def,string fileName); }
"@
}
    $state=[PTARD3D9ReadIni]::GetPrivateProfileInt('WIN81_NIS','VBlankDiagnostics',1,$ini)
  }
  if($state -ne 1){Write-Host '[FAIL] VBlankDiagnostics=0 dans le runtime D3D9 charge. Lance 03 avant le jeu ou redemarre le jeu apres armement.';exit 46}
  foreach($n in @('PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt','PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt','PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv','PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt')){$p=Join-Path $g $n;if(Test-Path -LiteralPath $p -PathType Leaf){Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue}}
  $precompiled=[string]$env:PTAR_RAWCOMPARE3_VERIFIER_EXE
  if([string]::IsNullOrWhiteSpace($precompiled) -or -not(Test-Path -LiteralPath $precompiled -PathType Leaf)){exit 44}
  & $precompiled 20 '--autostart';$rc=$LASTEXITCODE;if($rc -ne 0){exit $rc}
  $out=Join-Path $g 'PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt';$csv=Join-Path $g 'PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv';$status=Join-Path $g 'PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt';$err=Join-Path $g 'PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt'
  if(-not(Test-Path -LiteralPath $out -PathType Leaf) -or -not(Test-Path -LiteralPath $csv -PathType Leaf) -or -not(Test-Path -LiteralPath $status -PathType Leaf)){exit 27}
  & $PsExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $an -CsvPath $csv -OutputPath $out -OutPath $metrics -Label $Label;if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
  $kv=@{};foreach($ln in Get-Content -LiteralPath $metrics){if($ln -match '^([^=]+)=(.*)$'){$kv[$matches[1]]=$matches[2]}}
  $gen=0;$real=0;[int]::TryParse([string]$kv['GENERATED_CONTENTS'],[ref]$gen)|Out-Null;[int]::TryParse([string]$kv['REAL_CONTENTS'],[ref]$real)|Out-Null
  if($gen -le 0 -or $real -le 0){Write-Host ('[FAIL] FG_VISIBLE_PROOF: REAL='+$real+' GENERATED='+$gen);exit 24}
}else{
  foreach($n in @('PTAR_RAWCADENCE_LAST_OUTPUT.txt','PTAR_RAWCADENCE_LAST_STATUS.txt','PTAR_RAWCADENCE_LAST_SAMPLES.csv','PTAR_RAWCADENCE_LAST_ERROR.txt')){$p=Join-Path $g $n;if(Test-Path -LiteralPath $p -PathType Leaf){Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue}}
  $rawExe=[string]$env:PTAR_RAWCOMPARE3_RAW_EXE;if([string]::IsNullOrWhiteSpace($rawExe) -or -not(Test-Path -LiteralPath $rawExe -PathType Leaf)){exit 44}
  & $rawExe 20 '--autostart';$rc=$LASTEXITCODE;if($rc -ne 0){exit $rc}
  $out=Join-Path $g 'PTAR_RAWCADENCE_LAST_OUTPUT.txt';$csv=Join-Path $g 'PTAR_RAWCADENCE_LAST_SAMPLES.csv';$status=Join-Path $g 'PTAR_RAWCADENCE_LAST_STATUS.txt';$err=Join-Path $g 'PTAR_RAWCADENCE_LAST_ERROR.txt'
  if(-not(Test-Path -LiteralPath $out -PathType Leaf) -or -not(Test-Path -LiteralPath $csv -PathType Leaf) -or -not(Test-Path -LiteralPath $status -PathType Leaf)){exit 45}
  Copy-Item -LiteralPath $out -Destination $metrics -Force;Add-Content -LiteralPath $metrics -Value ('LABEL='+$Label) -Encoding UTF8
}
Copy-Item -LiteralPath $out -Destination (Join-Path $g ($prefix+'_OUTPUT.txt')) -Force;Copy-Item -LiteralPath $csv -Destination (Join-Path $g ($prefix+'_SAMPLES.csv')) -Force;Copy-Item -LiteralPath $status -Destination (Join-Path $g ($prefix+'_STATUS.txt')) -Force
if(Test-Path -LiteralPath $err -PathType Leaf){Copy-Item -LiteralPath $err -Destination (Join-Path $g ($prefix+'_ERROR.txt')) -Force}
if(Test-Path -LiteralPath $log -PathType Leaf){Copy-Item -LiteralPath $log -Destination (Join-Path $g ($prefix+'_RUNTIME.log')) -Force}
$idx=Join-Path $g 'PTAR_RAWCOMPARE_INDEX.txt';Add-Content -LiteralPath $idx -Value ($prefix+' | backend=D3D9_X86 | session='+$session+' | runtime='+$got) -Encoding UTF8
('LAST_PREFIX='+$prefix+[Environment]::NewLine+'LAST_METRICS='+$metrics)|Set-Content -LiteralPath (Join-Path $g 'PTAR_RAWCOMPARE3_LAST_RESULT.txt') -Encoding UTF8
exit 0
