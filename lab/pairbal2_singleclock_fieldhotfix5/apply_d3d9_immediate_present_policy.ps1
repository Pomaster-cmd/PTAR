$ErrorActionPreference='Stop'
$p='compat\x86_d3d9\ptar_d3d9_proxy.cpp'
if(-not(Test-Path -LiteralPath $p -PathType Leaf)){throw 'ptar_d3d9_proxy.cpp missing'}
$s=[IO.File]::ReadAllText($p)

# FIELDHOTFIX5 keeps the existing FIELDHOTFIX3 software PairBal2 scheduler and
# removes the integration defect that stacked it on a synchronized D3D9 Present.
# D3D9 has no per-Present SyncInterval, so the device must not add a second
# cadence authority. The existing software grid is the sole clock.
$old='    actual->EnableAutoDepthStencil=FALSE;'
$new=@'
    actual->EnableAutoDepthStencil=FALSE;

    // D3D11 MAINPERF2/PAIRBAL2 D3D9 single-clock adaptation.
    // PairBal2 timing is already applied by ptar_fg_pacer.h. Do not stack the
    // device's fixed VBlank interval on top of it: that was FIELDHOTFIX3's
    // double-wait failure. Device Present is therefore immediate and the
    // existing PairBal2 software grid is the sole cadence authority.
    actual->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
'@
$count=([regex]::Matches($s,[regex]::Escape($old))).Count
if($count -ne 1){throw ('presentation-parameter anchor count='+$count+' expected=1')}
$s=$s.Replace($old,$new.TrimEnd("`r","`n"))

[IO.File]::WriteAllText($p,$s,(New-Object Text.UTF8Encoding($false)))
$d=(git diff -- $p) -join "`n"
if($d -notmatch 'PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE'){throw 'Immediate interval patch absent'}
if($d -match 'PresentationInterval=D3DPRESENT_INTERVAL_ONE'){throw 'Sync1 device wait still forced'}
Write-Host 'D3D9_PAIRBAL2_SINGLECLOCK_PRESENT_POLICY=PASS'
Write-Host 'D3D9_DEVICE_PRESENT_INTERVAL=IMMEDIATE'
Write-Host 'D3D9_CADENCE_AUTHORITY=EXISTING_PAIRBAL2_SOFTWARE_GRID'
