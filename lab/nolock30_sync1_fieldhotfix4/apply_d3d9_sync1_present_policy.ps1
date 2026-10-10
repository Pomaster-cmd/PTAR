$ErrorActionPreference='Stop'
$p='compat\x86_d3d9\ptar_d3d9_proxy.cpp'
if(-not(Test-Path -LiteralPath $p -PathType Leaf)){throw 'ptar_d3d9_proxy.cpp missing'}
$s=[IO.File]::ReadAllText($p)

# D3D11 GW16F/G NOLOCK30_1 keeps Present SyncInterval=1 and explicitly avoids
# any extra Sleep/WaitForVBlank pacing detour.  D3D9 has no per-Present
# SyncInterval, so establish the equivalent device-level interval once.
$old='    actual->EnableAutoDepthStencil=FALSE;'
$new=@'
    actual->EnableAutoDepthStencil=FALSE;

    // D3D11 GW16F/G NOLOCK30_1 parity: synchronized Present is the only
    // cadence authority. D3D9 has no per-Present SyncInterval parameter, so
    // the equivalent SyncInterval=1 contract is established on the device.
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
Write-Host 'D3D9_D3D11_NOLOCK30_SYNC1_PRESENT_POLICY=PASS'
Write-Host 'D3D9_EXTRA_SOFTWARE_PRESENT_WAIT=NONE'
