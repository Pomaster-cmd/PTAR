$ErrorActionPreference='Stop';$PackRoot=Split-Path -Parent $PSScriptRoot
$stateRoot=Join-Path $PackRoot '_PTAR_UNINSTALL\state';$latest=Join-Path $stateRoot 'LATEST_STATE.txt';$g=$null
if(Test-Path -LiteralPath $latest -PathType Leaf){
 $s=(Get-Content -LiteralPath $latest -TotalCount 1).Trim()
 $mp=Join-Path $s 'install_state.json'
 if(Test-Path -LiteralPath $mp -PathType Leaf){$m=Get-Content -LiteralPath $mp -Raw|ConvertFrom-Json;$g=[string]$m.game_root}
}
if(-not $g){
 $tf=Join-Path $PackRoot 'win81_nis_install_target.txt'
 if(Test-Path -LiteralPath $tf -PathType Leaf){$g=(Get-Content -LiteralPath $tf -TotalCount 1).Trim().Trim('"')}
}
if(-not $g -or -not(Test-Path -LiteralPath $g -PathType Container)){Write-Host '[FAIL] Cible PTAR installee introuvable';exit 2}
$st=Get-Date -Format 'yyyyMMdd_HHmmss_fff';$d=Join-Path $PSScriptRoot ('collect_'+$st);New-Item -ItemType Directory -Path $d -Force|Out-Null
$gameFiles=@('win81_nis.log','win81_nis.ini','win81_nis_version.txt','PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt','PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt','PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv','PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt')
foreach($n in $gameFiles){$p=Join-Path $g $n;if(Test-Path -LiteralPath $p -PathType Leaf){Copy-Item -LiteralPath $p -Destination (Join-Path $d $n) -Force}}
foreach($n in @('win81_nis_install_target.txt','win81_nis_install_exe.txt')){$p=Join-Path $PackRoot $n;if(Test-Path -LiteralPath $p -PathType Leaf){Copy-Item -LiteralPath $p -Destination (Join-Path $d $n) -Force}}
$log=Join-Path $g 'win81_nis.log'
$autoOut=Join-Path $d 'AUTO_SWITCHES.txt';$hotOut=Join-Path $d 'HOTKEY_EVENTS.txt'
if(Test-Path -LiteralPath $log -PathType Leaf){
 $lines=@(Select-String -LiteralPath $log -SimpleMatch 'GW16 AUTO60/30' -ErrorAction SilentlyContinue|ForEach-Object{$_.Line})
 if($lines.Count -gt 0){$lines|Set-Content -LiteralPath $autoOut -Encoding UTF8}else{'NO_AUTO_SWITCH_LOG_LINES'|Set-Content -LiteralPath $autoOut -Encoding UTF8}
 $hot=@(Select-String -LiteralPath $log -Pattern 'HOTKEY|F6 MANUAL SCALER MODE|P1FG7N QUALITY|QUALITY|FG_PRESENTER_RELEASE|GWINDOW5 TEARDOWN|presenter swapchain creation failed|separate-device preparation failed|NVENC ME ring ready' -ErrorAction SilentlyContinue|ForEach-Object{$_.Line})
 if($hot.Count -gt 0){$hot|Set-Content -LiteralPath $hotOut -Encoding UTF8}else{'NO_HOTKEY_EVENT_LINES'|Set-Content -LiteralPath $hotOut -Encoding UTF8}
}else{'RUNTIME_LOG_ABSENT'|Set-Content -LiteralPath $autoOut -Encoding UTF8;'RUNTIME_LOG_ABSENT'|Set-Content -LiteralPath $hotOut -Encoding UTF8}
Get-ChildItem -LiteralPath $PSScriptRoot -File -ErrorAction SilentlyContinue|Where-Object{$_.Name -match 'VALIDATION|FINDING|MANIFEST'}|ForEach-Object{Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $d $_.Name) -Force}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z=Join-Path $g ('PTAR_RESULTS_'+$st+'.zip');$suffix=0
while(Test-Path -LiteralPath $z){$suffix++;$z=Join-Path $g ('PTAR_RESULTS_'+$st+'_'+$suffix.ToString('000')+'.zip')}
[IO.Compression.ZipFile]::CreateFromDirectory($d,$z,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Host ('RESULTAT='+$z);exit 0
