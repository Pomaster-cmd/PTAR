$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

$base='lab\ctrl_ui_real_visibility\validate_d3d9_controller_fullstack_same_binary.ps1'
$inner='lab\ctrl_ui_real_visibility\validate_d3d9_controller_fullstack_same_binary_v3_inner.ps1'
$src=Get-Content -LiteralPath $base -Raw

$pattern="(?s)Add-Type @'\r?\nusing System; using System.Runtime.InteropServices; using System.Threading;.*?\r?\n'@"
$newLines=@(
"Add-Type @'",
'using System; using System.Runtime.InteropServices; using System.Threading;',
'public static class PTARKeys {',
' [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);',
' [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr h);',
' [DllImport("user32.dll")] static extern IntPtr SetActiveWindow(IntPtr h);',
' [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);',
' [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h,int n);',
' [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();',
' [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);',
' [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();',
' [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a,uint b,bool attach);',
' [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);',
' const uint UP=2; const int SW_RESTORE=9;',
' static void Link(uint a,uint b,bool on){if(a!=0&&b!=0&&a!=b)AttachThreadInput(a,b,on);}',
' public static void AcquireForeground(IntPtr h){',
'   if(h==IntPtr.Zero)throw new InvalidOperationException("GAME_HWND_ZERO");',
'   for(int i=0;i<24;i++){',
'     if(GetForegroundWindow()==h)return;',
'     IntPtr fg=GetForegroundWindow(); uint dummy; uint tt=GetWindowThreadProcessId(h,out dummy); uint ft=fg==IntPtr.Zero?0:GetWindowThreadProcessId(fg,out dummy); uint ct=GetCurrentThreadId();',
'     try{Link(ct,ft,true);Link(ct,tt,true);Link(ft,tt,true);ShowWindow(h,SW_RESTORE);BringWindowToTop(h);SetForegroundWindow(h);SetActiveWindow(h);SetFocus(h);}finally{Link(ft,tt,false);Link(ct,tt,false);Link(ct,ft,false);}',
'     Thread.Sleep(100);',
'   }',
'   throw new InvalidOperationException("FOREGROUND_ACQUIRE_FAILED target=0x"+h.ToInt64().ToString("X")+" actual=0x"+GetForegroundWindow().ToInt64().ToString("X"));',
' }',
' public static void Chord(IntPtr h, byte vk, int hold){',
'   AcquireForeground(h); Thread.Sleep(180);',
'   if(GetForegroundWindow()!=h)throw new InvalidOperationException("FOREGROUND_LOST_BEFORE_CHORD");',
'   keybd_event(0x11,0,0,UIntPtr.Zero);keybd_event(vk,0,0,UIntPtr.Zero);Thread.Sleep(hold);keybd_event(vk,0,UP,UIntPtr.Zero);keybd_event(0x11,0,UP,UIntPtr.Zero);Thread.Sleep(120);',
' }',
'}',
"'@"
)
$new=$newLines -join "`r`n"
$m=[regex]::Matches($src,$pattern)
if($m.Count -ne 1){throw "PTARKeys block patch point mismatch: $($m.Count)"}
$src=[regex]::Replace($src,$pattern,[System.Text.RegularExpressions.MatchEvaluator]{param($x)$new},1)

# Give only deliberate long presses a wide scheduler margin. F6 stays short.
if(([regex]::Matches($src,[regex]::Escape('[PTARKeys]::Chord($gp.MainWindowHandle,0x74,900)'))).Count -ne 3){throw 'F5 long-trigger patch point mismatch'}
if(([regex]::Matches($src,[regex]::Escape('[PTARKeys]::Chord($gp.MainWindowHandle,0x70,900)'))).Count -ne 1){throw 'F1 long-trigger patch point mismatch'}
$src=$src.Replace('[PTARKeys]::Chord($gp.MainWindowHandle,0x74,900)','[PTARKeys]::Chord($gp.MainWindowHandle,0x74,1600)')
$src=$src.Replace('[PTARKeys]::Chord($gp.MainWindowHandle,0x70,900)','[PTARKeys]::Chord($gp.MainWindowHandle,0x70,1600)')
[IO.File]::WriteAllText((Join-Path (Get-Location) $inner),$src,(New-Object Text.UTF8Encoding($false)))

& (Join-Path (Get-Location) $inner)
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Host 'PTAR_D3D9_CONTROLLER_FULLSTACK_V3=PASS'
exit 0
