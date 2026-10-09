$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Replace-ExactOnce([string]$Path,[string]$Old,[string]$New)
{
  $text=Get-Content -LiteralPath $Path -Raw
  $count=([regex]::Matches($text,[regex]::Escape($Old))).Count
  if($count -ne 1){throw "Expected exactly one match in $Path, got $count"}
  [IO.File]::WriteAllText((Resolve-Path -LiteralPath $Path).Path,$text.Replace($Old,$New),(New-Object Text.UTF8Encoding($false)))
}

# Same focus-resilience candidate as fullstack-v2.  A chord must begin while the game
# is foreground, but a transient focus flicker after arming must not discard the hold.
$f5='lab\package_fixture_v2\diag\rawcompare3\PTARRawCompare3Controller.cs'
$f1='lab\package_fixture_v2\diag\rawcompare3\presentshed1\PTARPresentShed1CtrlF1.cs'
$oldFocus='if(!IsTargetForeground()){chordDown=false;longTriggered=false;return;}'
$newFocus='bool targetForeground=IsTargetForeground();if(!chordDown&&!targetForeground)return;'
Replace-ExactOnce $f5 $oldFocus $newFocus
Replace-ExactOnce $f1 $oldFocus $newFocus

$base='lab\ctrl_ui_real_visibility\validate_d3d9_controller_fullstack_same_binary.ps1'
$inner='lab\ctrl_ui_real_visibility\validate_d3d9_controller_fullstack_f1_120_same_binary_inner.ps1'
$src=Get-Content -LiteralPath $base -Raw

# Stabilize only deliberate long presses.  Short presses (used to select 120 s) remain short.
$old='Thread.Sleep(hold);'
$new='Thread.Sleep(hold>=700?Math.Max(hold,1800):hold);'
if(([regex]::Matches($src,[regex]::Escape($old))).Count -ne 1){throw 'Chord hold patch point mismatch'}
$src=$src.Replace($old,$new)

# Allow enough lifetime for a 120 s F1 capture in each scenario.
$old="`$args='--ms=190000 --fg-at=40000'"
$new="`$args='--ms=275000 --fg-at=40000'"
if(([regex]::Matches($src,[regex]::Escape($old))).Count -ne 1){throw 'game duration patch point mismatch'}
$src=$src.Replace($old,$new)

# F1 starts on 60 s. First short press exposes the menu; second short press, while the
# menu is visible, selects 120 s. Then a real long CTRL+F1 starts the 120 s analysis.
$old='[PTARKeys]::Chord($gp.MainWindowHandle,0x70,900)'
$new='[PTARKeys]::Chord($gp.MainWindowHandle,0x70,160);Start-Sleep -Milliseconds 350;[PTARKeys]::Chord($gp.MainWindowHandle,0x70,160);Start-Sleep -Milliseconds 350;[PTARKeys]::Chord($gp.MainWindowHandle,0x70,900)'
if(([regex]::Matches($src,[regex]::Escape($old))).Count -ne 1){throw 'F1 trigger patch point mismatch'}
$src=$src.Replace($old,$new)
$src=$src.Replace("Wait-Text `$f1log 'DONE PRESENTSHED60' 80|Out-Null","Wait-Text `$f1log 'DONE PRESENTSHED120' 150|Out-Null")
$src=$src.Replace("if((Count-Match `$f1log 'DONE PRESENTSHED60') -ne 1)","if((Count-Match `$f1log 'DONE PRESENTSHED120') -ne 1)")
$src=$src.Replace("'F1_PRESENTSHED60=PASS'","'F1_PRESENTSHED120=PASS'")
$src=$src.Replace("'F1_60=PASS'","'F1_120=PASS'")

# The HUD bridge encodes the selected F1 mode in A: 0=60 s, 1=120 s.
# The generic 60 s harness expects a=0; this 120 s variant must require a=1.
$old="'EMIT type=26 a=0 .*reason=F1_DONE'"
$new="'EMIT type=26 a=1 .*reason=F1_DONE'"
if(([regex]::Matches($src,[regex]::Escape($old))).Count -ne 1){throw 'F1 bridge 120-mode assertion patch point mismatch'}
$src=$src.Replace($old,$new)

# Make the final markers unambiguous so the 60 s and 120 s gates cannot be confused.
$src=$src.Replace("PTAR_D3D9_CONTROLLER_FULLSTACK=PASS","PTAR_D3D9_CONTROLLER_FULLSTACK_F1_120=PASS")
$src=$src.Replace("PTAR_D3D9_SAME_BINARY_FULLSTACK=PASS","PTAR_D3D9_SAME_BINARY_FULLSTACK_F1_120=PASS")
[IO.File]::WriteAllText((Join-Path (Get-Location) $inner),$src,(New-Object Text.UTF8Encoding($false)))

& (Join-Path (Get-Location) $inner)
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Host 'PTAR_D3D9_CONTROLLER_FULLSTACK_F1_120_V2=PASS'
exit 0
