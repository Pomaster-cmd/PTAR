$ErrorActionPreference='Stop';$PackRoot=Split-Path -Parent $PSScriptRoot;$g=$null
if(Test-Path -LiteralPath (Join-Path $PackRoot 'Warhammer.exe') -PathType Leaf){$g=$PackRoot}else{$p=Split-Path -Parent $PackRoot;if(Test-Path -LiteralPath (Join-Path $p 'Warhammer.exe') -PathType Leaf){$g=$p}}
if(-not $g){Write-Host '[FAIL] Warhammer.exe introuvable';exit 2}
$st=Get-Date -Format 'yyyyMMdd_HHmmss';$d=Join-Path $PSScriptRoot ('collect_'+$st);New-Item -ItemType Directory -Path $d -Force|Out-Null
$gameFiles=@(
 'win81_nis.log','win81_nis.ini','win81_nis_version.txt',
 'PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt','PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt','PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv','PTAR_VISIBLE_VERIFIER_LAST_ERROR.txt'
)
foreach($n in $gameFiles){$p=Join-Path $g $n;if(Test-Path -LiteralPath $p -PathType Leaf){Copy-Item -LiteralPath $p -Destination (Join-Path $d $n) -Force}}
$log=Join-Path $g 'win81_nis.log'
$autoOut=Join-Path $d 'AUTO_SWITCHES.txt'
$hotOut=Join-Path $d 'HOTKEY_EVENTS.txt'
if(Test-Path -LiteralPath $log -PathType Leaf){
 $lines=@(Select-String -LiteralPath $log -SimpleMatch 'GW16 AUTO60/30' -ErrorAction SilentlyContinue | ForEach-Object {$_.Line})
 if($lines.Count -gt 0){$lines | Set-Content -LiteralPath $autoOut -Encoding UTF8}else{'NO_AUTO_SWITCH_LOG_LINES' | Set-Content -LiteralPath $autoOut -Encoding UTF8}
 $hot=@(Select-String -LiteralPath $log -Pattern 'HOTKEY|F6 MANUAL SCALER MODE|P1FG7N QUALITY|QUALITY|FG_PRESENTER_RELEASE|GWINDOW5 TEARDOWN|presenter swapchain creation failed|separate-device preparation failed|NVENC ME ring ready' -ErrorAction SilentlyContinue | ForEach-Object {$_.Line})
 if($hot.Count -gt 0){$hot | Set-Content -LiteralPath $hotOut -Encoding UTF8}else{'NO_HOTKEY_EVENT_LINES' | Set-Content -LiteralPath $hotOut -Encoding UTF8}
}else{
 'RUNTIME_LOG_ABSENT' | Set-Content -LiteralPath $autoOut -Encoding UTF8
 'RUNTIME_LOG_ABSENT' | Set-Content -LiteralPath $hotOut -Encoding UTF8
}
foreach($n in @('PTAR_GW16_INSTALL_LAST.log','LAB_STATIC_VALIDATION.txt','BUILD_MANIFEST.json','GW15_FIELD_FINDING.txt','GW16A_HEAVY_FIELD_FINDING.txt','GW16B_HEAVY_FIELD_FINDING.txt','GW16C_HIGH_GATE_FINDING.txt','GW16D_LIGHT_HOTKEY_FIELD_FINDING.txt','GW16F_CTRL_F8_FIELD_FINDING.txt','GW16_RUNTIME_VALIDATION.txt','QUALITY_HOTKEY_VALIDATION.txt','PACINGVERIFIER3_VALIDATION.txt','AUTO_WRAPPER_BUILD_VALIDATION.txt','GW16H_SAFEPOINT2_ZEROFRAME_FINDING.txt','RECORDER_LOCAL_GPU_STATE_VALIDATION.txt','SP2_INHERITED_PACKAGE_VALIDATION.txt','GW16H_SAFEPOINT3_NATIVE_CONTEXT_LEAK_FINDING.txt','RECORDER_NATIVE_STATE_GUARD_VALIDATION.txt','GW16H_SAFEPOINT4_FG_PROFILE3_QUEUE_SATURATION_FINDING.txt','RECORDER_QSV_PRIORITY_VALIDATION.txt','GW16H_SAFEPOINT5_Q3_TDETAIL_FINDING.txt','FG_TDETAIL2_VALIDATION.txt','GW16H_SAFEPOINT6_TDETAIL2_FIELD_FINDING.txt','FG_TDETAIL3_VALIDATION.txt','GW16H_SAFEPOINT7_TDETAIL3_FIELD_FINDING.txt','FG_TDETAIL4_VALIDATION.txt','GW16H_SAFEPOINT11_FUSEDDETAIL1_LAB_FINDING.txt','FG_FUSEDDETAIL1_VALIDATION.txt','GW16_PACKAGE_VALIDATION.txt')){$p=Join-Path $PSScriptRoot $n;if(Test-Path -LiteralPath $p -PathType Leaf){Copy-Item -LiteralPath $p -Destination (Join-Path $d $n) -Force}}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z=Join-Path $g ('PTAR_GW16H_RESULTS_'+$st+'.zip')
if(Test-Path -LiteralPath $z){Remove-Item -LiteralPath $z -Force}
[IO.Compression.ZipFile]::CreateFromDirectory($d,$z,[IO.Compression.CompressionLevel]::Optimal,$false)
Write-Host ('RESULTAT='+$z);exit 0
