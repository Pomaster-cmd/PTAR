$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Wait-Text([string]$Path,[string]$Pattern,[int]$TimeoutSec)
{
  $sw=[Diagnostics.Stopwatch]::StartNew()
  while($sw.Elapsed.TotalSeconds -lt $TimeoutSec){
    if(Test-Path -LiteralPath $Path -PathType Leaf){
      try{$fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete);try{$sr=New-Object IO.StreamReader($fs);try{$t=$sr.ReadToEnd()}finally{$sr.Dispose()}}finally{$fs.Dispose()};if($t -match $Pattern){return $t}}catch{}
    }
    Start-Sleep -Milliseconds 200
  }
  throw "Timeout waiting $Pattern in $Path"
}
function Count-Match([string]$Path,[string]$Pattern){if(-not(Test-Path $Path)){return 0};$t=Get-Content $Path -Raw;return ([regex]::Matches($t,$Pattern,[Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count}

# Build exactly once. Every scenario below consumes this same d3d9.dll byte stream.
& .\lab\ctrl_ui_real_visibility\validate_d3d9_d3d11_diag_port.ps1
if($LASTEXITCODE -ne 0){throw "candidate build failed rc=$LASTEXITCODE"}
$runtime=(Resolve-Path artifact\d3d9.dll).Path
$runtimeSha=(Get-FileHash $runtime -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "SAME_BINARY_SHA256=$runtimeSha"

& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DUNICODE /D_UNICODE /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_real_present_e2e_game.cpp /link d3d9.lib user32.lib /OUT:artifact\ptar_real_d3d9_game.exe /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE -ne 0){throw 'game harness compile failed'}

$stage=(Join-Path (Get-Location) 'fullstack_pkg')
if(Test-Path $stage){Remove-Item $stage -Recurse -Force}
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item lab\package_fixture_v2\diag (Join-Path $stage 'diag') -Recurse -Force
$old='20e7f725d9aa391de43e11c0d0496135b6fa89347e6defad9b0481137b553846'
foreach($p in @(Get-ChildItem (Join-Path $stage 'diag') -Recurse -File)){
  if($p.Extension -in @('.ps1','.cs')){$s=Get-Content $p.FullName -Raw;if($s.Contains($old)){Set-Content $p.FullName ($s.Replace($old,$runtimeSha)) -Encoding UTF8}}
}
# Prove no active V2 hash gate remains in the staged candidate.
foreach($p in @(Get-ChildItem (Join-Path $stage 'diag') -Recurse -File)){if((Get-Content $p.FullName -Raw) -match [regex]::Escape($old)){throw "stale V2 hash gate in $($p.FullName)"}}

$csc32=Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
$csc64=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(-not(Test-Path $csc32) -or -not(Test-Path $csc64)){throw '.NET Framework compilers missing'}
$tools=(Join-Path (Get-Location) 'fullstack_tools');if(Test-Path $tools){Remove-Item $tools -Recurse -Force};New-Item -ItemType Directory -Force $tools | Out-Null
& $csc64 /nologo /target:winexe /optimize+ /platform:x64 /reference:System.Windows.Forms.dll /reference:System.Drawing.dll ('/out:'+(Join-Path $tools 'ptar_rawcompare3_controller.exe')) (Join-Path $stage 'diag\rawcompare3\PTARRawCompare3Controller.cs');if($LASTEXITCODE -ne 0){throw 'F5 controller compile failed'}
& $csc64 /nologo /target:winexe /optimize+ /platform:x64 /reference:System.Windows.Forms.dll /reference:System.Drawing.dll ('/out:'+(Join-Path $tools 'ptar_presentshed1_ctrl_f1.exe')) (Join-Path $stage 'diag\rawcompare3\presentshed1\PTARPresentShed1CtrlF1.cs');if($LASTEXITCODE -ne 0){throw 'F1 controller compile failed'}
& $csc64 /nologo /target:exe /optimize+ /platform:x64 ('/out:'+(Join-Path $tools 'ptar_d3d9_diag_hud_bridge.exe')) lab\ctrl_ui_real_visibility\PTARD3D9DiagHudBridgeController.cs;if($LASTEXITCODE -ne 0){throw 'HUD bridge compile failed'}
foreach($e in @('ptar_rawcompare3_controller.exe','ptar_presentshed1_ctrl_f1.exe','ptar_d3d9_diag_hud_bridge.exe')){$p=Start-Process (Join-Path $tools $e) -ArgumentList '--selftest' -PassThru -WindowStyle Hidden;$p.WaitForExit();if($p.ExitCode -ne 0){throw "$e selftest failed $($p.ExitCode)"}}

Add-Type @'
using System; using System.Runtime.InteropServices; using System.Threading;
public static class PTARKeys {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
 [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
 const uint UP=2;
 public static void Chord(IntPtr h, byte vk, int hold){SetForegroundWindow(h);SetFocus(h);Thread.Sleep(120);keybd_event(0x11,0,0,UIntPtr.Zero);keybd_event(vk,0,0,UIntPtr.Zero);Thread.Sleep(hold);keybd_event(vk,0,UP,UIntPtr.Zero);keybd_event(0x11,0,UP,UIntPtr.Zero);}
}
'@

function Run-Scenario([string]$Name,[bool]$Exclusive)
{
  $root=Join-Path (Get-Location) ('fullstack_game_'+$Name.ToLowerInvariant());if(Test-Path $root){Remove-Item $root -Recurse -Force};New-Item -ItemType Directory -Force $root | Out-Null
  Copy-Item $runtime (Join-Path $root 'd3d9.dll') -Force;Copy-Item artifact\ptar_real_d3d9_game.exe (Join-Path $root 'game.exe') -Force;Set-Content (Join-Path $root 'PTAR_D3D9_DIAGNOSTICS.ON') 'enabled' -Encoding ASCII
  $game=(Resolve-Path (Join-Path $root 'game.exe')).Path;Set-Content (Join-Path $stage 'PTAR_X86_D3D9_LAST_TARGET.txt') $game -Encoding UTF8
  $env:PTAR_GAME_ROOT=$root;$env:PTAR_TARGET_EXE=$game;$env:PTAR_PACKAGE_ROOT=$stage;$env:PTAR_PS_EXE=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe';$env:PTAR_RAWCOMPARE3_VERIFIER_EXE=(Resolve-Path artifact\ptar_vblank3_d3d9_autostart.exe).Path;$env:PTAR_RAWCOMPARE3_RAW_EXE=(Resolve-Path artifact\ptar_rawcadence3_d3d9_autostart.exe).Path;$env:PTAR_RAWCOMPARE3_SESSION_ID=('FULL_'+$Name+'_'+(Get-Date -Format 'HHmmssfff'))
  $resultDir=Join-Path $root 'diag\PTAR_PRESENTSHED1';New-Item -ItemType Directory -Force $resultDir|Out-Null;$env:PTAR_PRESENTSHED1_RESULT_DIR=$resultDir;$env:PTAR_PRESENTSHED1_VERIFIER_EXE=(Resolve-Path artifact\ptar_presentshed1_visible.exe).Path;$env:PTAR_PRESENTSHED1_SESSION_ID=('F1_'+$Name+'_'+(Get-Date -Format 'HHmmssfff'))
  $args='--ms=190000 --fg-at=40000';if($Exclusive){$args+=' --exclusive'}
  $gp=Start-Process $game -ArgumentList $args -WorkingDirectory $root -PassThru
  $f5=$null;$f1=$null;$bridge=$null
  try{
    $runtimeLog=Join-Path $root 'PTAR_X86_D3D9.log';$start=Wait-Text $runtimeLog 'stage=PRESENT_REAL' 25;if($Exclusive -and $start -notmatch 'CREATEDEVICE_PP[^\r\n]*windowed=0'){throw 'exclusive device not proven'};if((-not $Exclusive) -and $start -notmatch 'CREATEDEVICE_PP[^\r\n]*windowed=1'){throw 'windowed device not proven'}
    $mods=@($gp.Modules|? ModuleName -ieq 'd3d9.dll');$expected=[IO.Path]::GetFullPath((Join-Path $root 'd3d9.dll'));if(@($mods|? {[string]::Equals([IO.Path]::GetFullPath($_.FileName),$expected,[StringComparison]::OrdinalIgnoreCase)}).Count -ne 1){throw 'exact candidate proxy not loaded'}
    $f5=Start-Process (Join-Path $tools 'ptar_rawcompare3_controller.exe') -PassThru -WindowStyle Hidden;$f1=Start-Process (Join-Path $tools 'ptar_presentshed1_ctrl_f1.exe') -PassThru -WindowStyle Hidden;$bridge=Start-Process (Join-Path $tools 'ptar_d3d9_diag_hud_bridge.exe') -PassThru -WindowStyle Hidden;Start-Sleep -Milliseconds 1200
    if($f5.HasExited -or $f1.HasExited -or $bridge.HasExited){throw 'controller/bridge exited at startup'}
    [PTARKeys]::Chord($gp.MainWindowHandle,0x74,900)
    $f5log=Join-Path $root 'PTAR_RAWCOMPARE3_CONTROLLER.log';Wait-Text $f5log 'DONE label=PRE_FG rc=0' 35|Out-Null
    Wait-Text $runtimeLog 'HOTKEY CTRL\+F6 FrameGeneration=ON' 30|Out-Null
    [PTARKeys]::Chord($gp.MainWindowHandle,0x74,900);Wait-Text $f5log 'DONE label=FG_ACTIVE rc=0' 35|Out-Null
    [PTARKeys]::Chord($gp.MainWindowHandle,0x70,900)
    $f1log=Join-Path $resultDir ('PTAR_PRESENTSHED1_CTRL_F1_'+$env:PTAR_PRESENTSHED1_SESSION_ID+'.log');Wait-Text $f1log 'DONE PRESENTSHED60' 80|Out-Null
    [PTARKeys]::Chord($gp.MainWindowHandle,0x75,180);Start-Sleep -Milliseconds 500
    $all=Wait-Text $runtimeLog 'HOTKEY CTRL\+F6 FrameGeneration=OFF' 10
    Start-Sleep -Seconds 3
    [PTARKeys]::Chord($gp.MainWindowHandle,0x74,900);Wait-Text $f5log 'DONE label=POST_FG rc=0' 35|Out-Null
    $blog=Join-Path $root 'PTAR_D3D9_DIAG_UI_BRIDGE.log';$bt=Wait-Text $blog 'F5_SYNC next_mode=0' 8
    foreach($pat in @('EMIT type=23 a=0 .*reason=F5_DONE','EMIT type=23 a=1 .*reason=F5_DONE','EMIT type=23 a=2 .*reason=F5_DONE','EMIT type=26 a=0 .*reason=F1_DONE')){if($bt -notmatch $pat){throw "bridge missing $pat"}}
    if($bt -match 'reason=F5_ERROR|reason=F1_ERROR|RESULT_POLL_FAIL'){throw 'bridge emitted an error'}
    if((Count-Match $f5log 'DONE label=PRE_FG rc=0') -ne 1 -or (Count-Match $f5log 'DONE label=FG_ACTIVE rc=0') -ne 1 -or (Count-Match $f5log 'DONE label=POST_FG rc=0') -ne 1){throw 'F5 phase completion count mismatch'}
    if((Count-Match $f1log 'DONE PRESENTSHED60') -ne 1){throw 'F1 completion count mismatch'}
    if($gp.HasExited){throw "game exited during full stack rc=$($gp.ExitCode)"}
    $rt=Get-Content $runtimeLog -Raw;if($rt -match 'PRESENT_STATE_CAPTURE_FAIL|FG_PRESENT_GENERATED_FAIL|SPATIAL_CURRENT_REAL_FAIL'){throw 'runtime hard Present failure logged'}
    @('PTAR_D3D9_CONTROLLER_FULLSTACK=PASS',('MODE='+$Name),('D3D9_SHA256='+$runtimeSha),'ACTUAL_D3D9_DEVICE=YES','ACTUAL_PROXY_HOOKPRESENT=YES','F5_PRE_20S=PASS','F5_FG_ACTIVE_20S=PASS','F1_PRESENTSHED60=PASS','F5_POST_20S=PASS','BRIDGE_F5_PRE_DONE=PASS','BRIDGE_F5_FG_DONE=PASS','BRIDGE_F5_POST_DONE=PASS','BRIDGE_F1_DONE=PASS','FALSE_STALE_DONE=NO','GAME_SURVIVED=YES')|Set-Content (Join-Path artifact ('FULLSTACK_'+$Name+'.txt')) -Encoding ASCII
    Write-Host ('FULLSTACK_'+$Name+'=PASS')
  } finally {foreach($p in @($bridge,$f1,$f5,$gp)){if($null -ne $p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue}}}
}

Run-Scenario 'WINDOWED' $false
Run-Scenario 'EXCLUSIVE' $true
$shaAfter=(Get-FileHash $runtime -Algorithm SHA256).Hash.ToLowerInvariant();if($shaAfter -ne $runtimeSha){throw 'candidate binary changed between scenarios'}
@('PTAR_D3D9_SAME_BINARY_FULLSTACK=PASS',('D3D9_SHA256='+$runtimeSha),'WINDOWED=PASS','EXCLUSIVE=PASS','F5_PRE_FG_POST=PASS','F1_60=PASS','HUD_BRIDGE_CORRELATED=PASS','SAME_BINARY_BOTH_MODES=YES','SOURCE_COMMIT='+$env:GITHUB_SHA)|Set-Content artifact\PTAR_D3D9_SAME_BINARY_FULLSTACK_VALIDATION.txt -Encoding ASCII
Write-Host 'PTAR_D3D9_SAME_BINARY_FULLSTACK=PASS'
