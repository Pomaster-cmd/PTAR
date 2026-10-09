param([switch]$Exclusive)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Wait-Text([string]$Path,[string]$Pattern,[int]$TimeoutSec)
{
    $sw=[Diagnostics.Stopwatch]::StartNew()
    while($sw.Elapsed.TotalSeconds -lt $TimeoutSec)
    {
        if(Test-Path -LiteralPath $Path -PathType Leaf)
        {
            try
            {
                $fs=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
                try {$sr=New-Object IO.StreamReader($fs); try {$t=$sr.ReadToEnd()} finally {$sr.Dispose()}} finally {$fs.Dispose()}
                if($t -match $Pattern){return $t}
            }
            catch {}
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Timeout waiting '$Pattern' in $Path"
}
function Assert-Contains([string]$Path,[string]$Pattern,[string]$Label)
{
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw "$Label missing: $Path"}
    $t=Wait-Text $Path $Pattern 5
    if($t -notmatch $Pattern){throw "$Label failed: pattern '$Pattern' absent in $Path`n$t"}
}

# First rebuild the exact candidate runtime and diagnostic readers with the
# transient, additive post-Present ring hook.  This also preserves the protected
# D3D9 render/FG core and runs the synthetic ABI smoke, but that smoke is NOT
# accepted as end-to-end proof by this script.
& .\lab\ctrl_ui_real_visibility\validate_d3d9_d3d11_diag_port.ps1
if($LASTEXITCODE -ne 0){throw "base diagnostic-port validation failed: $LASTEXITCODE"}

# Compile an actual x86 D3D9 application. It imports Direct3DCreate9 normally,
# so Windows DLL search loads the candidate proxy from the game directory.
& cl.exe /nologo /O2 /MT /W4 /WX /EHsc /DUNICODE /D_UNICODE /DWINVER=0x0603 /D_WIN32_WINNT=0x0603 lab\ctrl_ui_real_visibility\d3d9_real_present_e2e_game.cpp /link d3d9.lib user32.lib /OUT:artifact\ptar_real_d3d9_game.exe /SUBSYSTEM:WINDOWS,6.03
if($LASTEXITCODE -ne 0){throw 'real D3D9 game harness compile failed'}

dumpbin /headers artifact\ptar_real_d3d9_game.exe | Set-Content build_x86\REAL_GAME_HEADERS.txt
$gh=Get-Content build_x86\REAL_GAME_HEADERS.txt -Raw
if($gh -notmatch '14C machine \(x86\)' -or $gh -notmatch '6\.03 subsystem version'){throw 'real game PE compatibility check failed'}

$mode=if($Exclusive){'EXCLUSIVE'}else{'WINDOWED'}
$root=Join-Path (Get-Location) ("real_game_"+$mode.ToLowerInvariant())
if(Test-Path -LiteralPath $root){Remove-Item -LiteralPath $root -Recurse -Force}
New-Item -ItemType Directory -Force $root | Out-Null
Copy-Item artifact\d3d9.dll (Join-Path $root 'd3d9.dll') -Force
Copy-Item artifact\ptar_real_d3d9_game.exe (Join-Path $root 'game.exe') -Force
Set-Content -LiteralPath (Join-Path $root 'PTAR_D3D9_DIAGNOSTICS.ON') -Value 'enabled' -Encoding ASCII

$env:PTAR_GAME_ROOT=$root
$env:PTAR_TARGET_EXE=(Join-Path $root 'game.exe')
$env:PTAR_FG_PRESENTSHED1_OUTPUT_PREFIX=(Join-Path $root 'F1_REAL_PRESENT')

$args='--ms=145000 --fg-at=26000'
if($Exclusive){$args+=' --exclusive'}
$p=Start-Process -FilePath (Join-Path $root 'game.exe') -ArgumentList $args -WorkingDirectory $root -PassThru
try
{
    # The CRT result file stays open for the lifetime of the harness.  The first
    # revision waited on it and produced a false timeout despite a live device.
    # Use the proxy's own share-readable forensic log as the authoritative gate.
    $runtimeLog=Join-Path $root 'PTAR_X86_D3D9.log'
    $startup=Wait-Text $runtimeLog 'HOOK_DEVICE_DONE' 25
    if($Exclusive)
    {
        if($startup -notmatch 'CREATEDEVICE_PP[^\r\n]*windowed=0'){throw 'exclusive mode not actually created'}
    }
    else
    {
        if($startup -notmatch 'CREATEDEVICE_PP[^\r\n]*windowed=1'){throw 'windowed mode not actually created'}
    }
    if($startup -notmatch 'PTAR_ACTIVE'){throw 'PTAR runtime did not reach active state'}
    if($startup -notmatch 'stage=PRESENT_REAL'){throw 'actual HookPresent did not reach PRESENT_REAL'}
    if($p.HasExited){throw "real D3D9 game exited early code=$($p.ExitCode)"}

    # Prove the exact candidate proxy is what the live game process loaded.
    $mods=@($p.Modules | Where-Object {$_.ModuleName -ieq 'd3d9.dll'})
    if($mods.Count -ne 1){throw "expected exactly one d3d9.dll module, got $($mods.Count)"}
    $loaded=[IO.Path]::GetFullPath($mods[0].FileName)
    $expected=[IO.Path]::GetFullPath((Join-Path $root 'd3d9.dll'))
    if(-not [string]::Equals($loaded,$expected,[StringComparison]::OrdinalIgnoreCase)){throw "system/other d3d9 loaded instead of candidate: $loaded"}
    Set-Content build_x86\REAL_GAME_MODULE.txt ("LOADED_D3D9="+$loaded) -Encoding ASCII

    # PRE_FG: exact real Present path, 20 seconds, no generated samples allowed.
    & artifact\ptar_rawcadence3_d3d9_autostart.exe 20 --autostart
    if($LASTEXITCODE -ne 0){throw "real PRE_FG verifier failed rc=$LASTEXITCODE"}
    Assert-Contains (Join-Path $root 'PTAR_RAWCADENCE_LAST_STATUS.txt') 'RESULT=PASS' 'PRE_FG status'
    Assert-Contains (Join-Path $root 'PTAR_RAWCADENCE_LAST_OUTPUT.txt') 'RAW_RESULT=RAW_REAL_ONLY_VALID' 'PRE_FG raw result'
    Assert-Contains (Join-Path $root 'PTAR_RAWCADENCE_LAST_OUTPUT.txt') 'GENERATED_CONTENTS=0' 'PRE_FG generated gate'
    Copy-Item (Join-Path $root 'PTAR_RAWCADENCE_LAST_OUTPUT.txt') ("build_x86\REAL_PRE_"+$mode+".txt") -Force

    # The game holds CTRL+F6 across an actual Present at ~26 s. Require the
    # runtime itself to confirm that it entered FG.
    $hot=Wait-Text $runtimeLog 'HOTKEY CTRL\+F6 FrameGeneration=ON' 20
    if($p.HasExited){throw "real D3D9 game exited before FG phase code=$($p.ExitCode)"}

    # F5 FG_ACTIVE: exact runtime Present ring must contain both REAL and GENERATED.
    & artifact\ptar_vblank3_d3d9_autostart.exe 20 --autostart
    if($LASTEXITCODE -ne 0){throw "real FG_ACTIVE verifier failed rc=$LASTEXITCODE"}
    Assert-Contains (Join-Path $root 'PTAR_VISIBLE_VERIFIER_LAST_STATUS.txt') 'RESULT=PASS' 'FG_ACTIVE status'
    $fgCsv=Import-Csv (Join-Path $root 'PTAR_VISIBLE_VERIFIER_LAST_SAMPLES.csv')
    $r=@($fgCsv | Where-Object {$_.type -eq 'R'}).Count
    $g=@($fgCsv | Where-Object {$_.type -eq 'G'}).Count
    if($r -lt 10 -or $g -lt 10){throw "actual FG telemetry insufficient R=$r G=$g"}
    Copy-Item (Join-Path $root 'PTAR_VISIBLE_VERIFIER_LAST_OUTPUT.txt') ("build_x86\REAL_FG_"+$mode+".txt") -Force

    # F1 PRESENTSHED1: exact shipped 60-second duration, with all output products.
    & artifact\ptar_presentshed1_visible.exe 60 --autostart
    if($LASTEXITCODE -ne 0){throw "real F1 PRESENTSHED1 verifier failed rc=$LASTEXITCODE"}
    foreach($suffix in @('_OUTPUT.txt','_SAMPLES.csv','_STATUS.txt','_RR_FOCUS.txt','_LONG_HOLD_FOCUS.txt','_GEN_PRESSURE_FOCUS.txt'))
    {
        $q=(Join-Path $root ('F1_REAL_PRESENT'+$suffix))
        if(-not(Test-Path -LiteralPath $q -PathType Leaf)){throw "real F1 output missing $suffix"}
    }
    Assert-Contains (Join-Path $root 'F1_REAL_PRESENT_STATUS.txt') 'RESULT=PASS' 'F1 status'
    $f1=Import-Csv (Join-Path $root 'F1_REAL_PRESENT_SAMPLES.csv')
    $f1r=@($f1 | Where-Object {$_.type -eq 'R'}).Count
    $f1g=@($f1 | Where-Object {$_.type -eq 'G'}).Count
    if($f1r -lt 20 -or $f1g -lt 20){throw "actual F1 telemetry insufficient R=$f1r G=$f1g"}

    if($p.HasExited){throw "real D3D9 game exited during diagnostic measurement code=$($p.ExitCode)"}
    # Re-read while process is still alive so buffered runtime evidence cannot be
    # manufactured by process teardown.
    $liveLog=Wait-Text $runtimeLog 'FG_PRESENT_GENERATED' 5
    if($liveLog -notmatch 'stage=PRESENT_REAL'){throw 'runtime real-present stage absent'}
    if($liveLog -match 'PRESENT_STATE_CAPTURE_FAIL|FG_PRESENT_GENERATED_FAIL|SPATIAL_CURRENT_REAL_FAIL'){throw 'runtime Present path logged a hard rendering/presentation failure'}

    @(
      'PTAR_D3D9_REAL_PRESENT_E2E=PASS',
      ('MODE='+$mode),
      'SYNTHETIC_RING_WRITER_ACCEPTED_AS_PROOF=NO',
      'ACTUAL_D3D9_DEVICE=YES',
      'ACTUAL_PROXY_HOOKPRESENT=YES',
      'EXACT_CANDIDATE_DLL_LOADED=YES',
      'PRE_FG_20S=PASS',
      'FG_ACTIVE_20S=PASS',
      'PRESENTSHED1_60S=PASS',
      ('FG_REAL_EVENTS='+$r),
      ('FG_GENERATED_EVENTS='+$g),
      ('F1_REAL_EVENTS='+$f1r),
      ('F1_GENERATED_EVENTS='+$f1g),
      ('SOURCE_COMMIT='+$env:GITHUB_SHA)
    ) | Set-Content ("artifact\PTAR_D3D9_REAL_PRESENT_E2E_"+$mode+'.txt') -Encoding ASCII
    Write-Host ("PTAR_D3D9_REAL_PRESENT_E2E_"+$mode+'=PASS')
}
finally
{
    if($p -and -not $p.HasExited){Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue}
}
