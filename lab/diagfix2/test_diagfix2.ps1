$ErrorActionPreference='Stop'

# 1) Validate the DIAGFIX2 F5 gap policy under Windows PowerShell.
$rows=@(
  [pscustomobject]@{serial='100';generated_presents='10';real_presents='10';present_successes='20'},
  [pscustomobject]@{serial='103';generated_presents='11';real_presents='10';present_successes='21'},
  [pscustomobject]@{serial='107';generated_presents='11';real_presents='11';present_successes='22'},
  [pscustomobject]@{serial='110';generated_presents='12';real_presents='11';present_successes='23'}
)
$rawGaps=0L
for($i=1;$i -lt $rows.Count;$i++){
  $delta=([int]$rows[$i].serial-[int]$rows[$i-1].serial) -band 0x0FFF
  if($delta -ge 1 -and $delta -le 2048){if($delta -gt 1){$rawGaps+=($delta-1)}}else{throw 'unexpected serial delta'}
}
$observedTransitions=[Math]::Max(0,$rows.Count-1)
$typedPresentDelta=([Int64]$rows[-1].generated_presents+[Int64]$rows[-1].real_presents)-([Int64]$rows[0].generated_presents+[Int64]$rows[0].real_presents)
$presentSuccessDelta=([Int64]$rows[-1].present_successes)-([Int64]$rows[0].present_successes)
$missingByPresent=$presentSuccessDelta-$observedTransitions;if($missingByPresent -lt 0){$missingByPresent=0}
$presentCorroboratedGaps=[Int64][Math]::Min([double]$rawGaps,[double]$missingByPresent)
$markerOnlyAdvances=$rawGaps-$presentCorroboratedGaps
if($rawGaps -le 0){throw 'fixture did not create raw marker gaps'}
if($presentSuccessDelta -ne $observedTransitions){throw 'fixture present delta mismatch'}
if($presentCorroboratedGaps -ne 0){throw 'marker-only advances incorrectly treated as missing Presents'}
if($markerOnlyAdvances -ne $rawGaps){throw 'marker-only advance accounting mismatch'}
Write-Host 'DIAGFIX2_F5_MARKER_SERIAL_POLICY=PASS'

# A real missing Present must remain detectable.
$presentSuccessDelta2=$observedTransitions+2
$missingByPresent2=$presentSuccessDelta2-$observedTransitions
$presentCorroboratedGaps2=[Int64][Math]::Min([double]$rawGaps,[double]$missingByPresent2)
if($presentCorroboratedGaps2 -ne 2){throw 'real Present gaps are no longer detected'}
Write-Host 'DIAGFIX2_F5_REAL_GAP_DETECTION=PASS'

# 2) Build a fake complete-package-in-game layout and validate BAT D3D9 dispatch.
$here=Split-Path -Parent $MyInvocation.MyCommand.Path
$fake=Join-Path $here 'fakegame'
if(Test-Path $fake){Remove-Item $fake -Recurse -Force}
New-Item -ItemType Directory -Force (Join-Path $fake 'diag'),(Join-Path $fake 'BACKENDS\D3D9_X86\diag'),(Join-Path $fake 'BACKENDS\D3D11_X64\diag')|Out-Null
Copy-Item (Join-Path $here 'PTAR_TOGGLE_VISIBLE_FRAME_MARKERS_DIAGFIX2.bat') (Join-Path $fake 'diag\PTAR_TOGGLE_VISIBLE_FRAME_MARKERS.bat')
Set-Content (Join-Path $fake 'd3d9.dll') 'fixture' -Encoding ascii
$d9=@'
param([string]$GameRoot,[int]$Value=-1)
('BACKEND=D3D9`r`nGAME_ROOT='+$GameRoot+'`r`nVALUE='+$Value) | Set-Content -LiteralPath (Join-Path $GameRoot 'diagfix2_dispatch.txt') -Encoding ascii
exit 0
'@
$d11=@'
param([int]$Value=-1)
throw 'D3D11 helper must not be called when d3d9.dll is active'
'@
Set-Content (Join-Path $fake 'BACKENDS\D3D9_X86\diag\set_vblank_diagnostics.ps1') $d9 -Encoding ascii
Set-Content (Join-Path $fake 'BACKENDS\D3D11_X64\diag\set_vblank_diagnostics.ps1') $d11 -Encoding ascii
$bat=Join-Path $fake 'diag\PTAR_TOGGLE_VISIBLE_FRAME_MARKERS.bat'
$cmd='echo.|"'+$bat+'" STATUS'
$p=Start-Process -FilePath $env:ComSpec -ArgumentList '/d','/c',$cmd -PassThru -Wait -WindowStyle Hidden
if($p.ExitCode -ne 0){throw ('BAT STATUS failed rc='+$p.ExitCode)}
$dispatch=Get-Content (Join-Path $fake 'diagfix2_dispatch.txt') -Raw
if($dispatch -notmatch 'BACKEND=D3D9'){throw 'BAT did not dispatch to D3D9'}
if($dispatch -notmatch 'VALUE=-1'){throw 'BAT did not preserve STATUS value'}
Write-Host 'DIAGFIX2_MARKER_TOGGLE_D3D9_DISPATCH=PASS'
