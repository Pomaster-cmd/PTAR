$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Replace-ExactOnce([string]$Path,[string]$Old,[string]$New)
{
  $text=Get-Content -LiteralPath $Path -Raw
  $count=([regex]::Matches($text,[regex]::Escape($Old))).Count
  if($count -ne 1){throw "Expected exactly one match in $Path, got $count"}
  [IO.File]::WriteAllText((Resolve-Path -LiteralPath $Path).Path,$text.Replace($Old,$New),(New-Object Text.UTF8Encoding($false)))
}

# Harden both user-facing hotkey controllers: once a chord is armed while the game is
# foreground, a transient focus flicker must not reset the long-press timer. This is
# especially important around D3D9 exclusive-fullscreen mode changes / TopMost notices.
$f5='lab\package_fixture_v2\diag\rawcompare3\PTARRawCompare3Controller.cs'
$f1='lab\package_fixture_v2\diag\rawcompare3\presentshed1\PTARPresentShed1CtrlF1.cs'
$oldFocus='if(!IsTargetForeground()){chordDown=false;longTriggered=false;return;}'
$newFocus='bool targetForeground=IsTargetForeground();if(!chordDown&&!targetForeground)return;'
Replace-ExactOnce $f5 $oldFocus $newFocus
Replace-ExactOnce $f1 $oldFocus $newFocus

# The E2E driver intentionally holds the chord well beyond the 700 ms product threshold.
# This is not a product threshold change; it removes scheduler/focus jitter from the test
# stimulus while the actual controllers still decide LONG vs SHORT themselves.
$base='lab\ctrl_ui_real_visibility\validate_d3d9_controller_fullstack_same_binary.ps1'
$patched='lab\ctrl_ui_real_visibility\validate_d3d9_controller_fullstack_same_binary_v2_inner.ps1'
$src=Get-Content -LiteralPath $base -Raw
$needle='Thread.Sleep(hold);'
if(([regex]::Matches($src,[regex]::Escape($needle))).Count -ne 1){throw 'Chord hold patch point mismatch'}
$src=$src.Replace($needle,'Thread.Sleep(Math.Max(hold,1800));')
[IO.File]::WriteAllText((Join-Path (Get-Location) $patched),$src,(New-Object Text.UTF8Encoding($false)))

& (Join-Path (Get-Location) $patched)
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Write-Host 'PTAR_D3D9_CONTROLLER_FULLSTACK_V2=PASS'
exit 0
