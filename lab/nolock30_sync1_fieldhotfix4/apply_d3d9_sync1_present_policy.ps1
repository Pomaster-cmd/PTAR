$ErrorActionPreference='Stop'
$p='compat\x86_d3d9\ptar_d3d9_proxy.cpp'
if(-not(Test-Path -LiteralPath $p -PathType Leaf)){throw 'ptar_d3d9_proxy.cpp missing'}
$s=[IO.File]::ReadAllText($p)

# Current D3D11 MAINPERF2/PAIRBAL2 changes cadence by selecting SyncInterval
# directly on Present and explicitly adds no Sleep/WaitForVBlank/DwmFlush.
# Regular D3D9 cannot choose SyncInterval per Present, so use the directly
# representable PAIRBAL2 Sync1 branch as the device-level interval and do not
# stack any software timing wait on top of it.
$old='    actual->EnableAutoDepthStencil=FALSE;'
$new=@'
    actual->EnableAutoDepthStencil=FALSE;

    // D3D11 MAINPERF2/PAIRBAL2 D3D9 adaptation: Present is the sole cadence
    // authority. Regular D3D9 has no per-Present SyncInterval parameter, so
    // use the directly representable Sync1 branch at device creation/reset.
    // Never add a software QPC/Sleep wait on top of this interval.
    actual->PresentationInterval=D3DPRESENT_INTERVAL_ONE;
'@
$count=([regex]::Matches($s,[regex]::Escape($old))).Count
if($count -ne 1){throw ('presentation-parameter anchor count='+$count+' expected=1')}
$s=$s.Replace($old,$new.TrimEnd("`r","`n"))

[IO.File]::WriteAllText($p,$s,(New-Object Text.UTF8Encoding($false)))
$d=(git diff -- $p) -join "`n"
if($d -notmatch 'PresentationInterval=D3DPRESENT_INTERVAL_ONE'){throw 'Sync1 interval patch absent'}
if($d -match 'Sleep\(' -or $d -match 'WaitForVBlank' -or $d -match 'DwmFlush'){throw 'forbidden additional wait introduced'}
Write-Host 'D3D9_D3D11_PAIRBAL2_SYNC1_PRESENT_POLICY=PASS'
Write-Host 'D3D9_EXTRA_SOFTWARE_PRESENT_WAIT=NONE'
