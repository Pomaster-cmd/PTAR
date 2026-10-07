$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

$exe = Join-Path $here 'PTAR_Conviction_Hotspot_Sampler.exe'
if (-not (Test-Path $exe)) { throw 'PTAR_Conviction_Hotspot_Sampler.exe manquant.' }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PTARKeys {
    [DllImport("user32.dll")]
    public static extern short GetAsyncKeyState(int vKey);
    public static bool Down(int vKey) { return (GetAsyncKeyState(vKey) & 0x8000) != 0; }
}
'@

function Get-ConvictionProcess {
    $procs = @(Get-Process -Name 'Conviction_game' -ErrorAction SilentlyContinue)
    if ($procs.Count -eq 0) { return $null }
    return ($procs | Sort-Object StartTime -Descending | Select-Object -First 1)
}

function Wait-ForCtrlT {
    Write-Host '[PTAR] ARME : reste dans Conviction et appuie CTRL+T dans la scene a ~8 FPS.'
    $wasTDown = $false
    while ($true) {
        $ctrl = [PTARKeys]::Down(0x11)
        $t = [PTARKeys]::Down(0x54)
        if ($ctrl -and $t -and -not $wasTDown) { return }
        $wasTDown = $t
        Start-Sleep -Milliseconds 10
    }
}

Write-Host '[PTAR] Mode global CTRL+T. Le runner peut etre lance avant Conviction.'
$p = Get-ConvictionProcess
while ($null -eq $p) {
    Start-Sleep -Seconds 1
    $p = Get-ConvictionProcess
}
Write-Host ('[PTAR] Conviction detecte : PID {0}.' -f $p.Id)
Wait-ForCtrlT

$p = Get-ConvictionProcess
if ($null -eq $p) { exit 3 }

$threadRows = @()
foreach ($t in $p.Threads) {
    try { $threadRows += [pscustomobject]@{ Id=[int]$t.Id; StartTime=$t.StartTime } } catch { }
}
if ($threadRows.Count -eq 0) { throw 'Impossible de determiner les threads de Conviction.' }
$mainThread = $threadRows | Sort-Object StartTime, Id | Select-Object -First 1

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$sessionBase = Join-Path $here ('PTAR_CONVICTION_DIAG_' + $stamp)
$session = $sessionBase
$sessionIndex = 1
while (Test-Path $session) { $session=$sessionBase+'_'+$sessionIndex; $sessionIndex++ }
New-Item -ItemType Directory -Path $session | Out-Null

@(
 'PTAR_CONVICTION_HOTSPOT_SESSION=2',
 'TRIGGER=GLOBAL_CTRL_T',
 ('DATE_LOCAL='+(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')),
 ('PID='+$p.Id),
 ('PROCESS_START='+$p.StartTime.ToString('o')),
 ('INITIAL_THREAD_TID='+$mainThread.Id),
 ('THREAD_COUNT_AT_TRIGGER='+$threadRows.Count),
 'PASS1=INITIAL_PROCESS_THREAD',
 'PASS2=HOTTEST_CPU_THREAD_AUTO',
 'SECONDS_PER_PASS=12',
 'INTERVAL_MS=4',
 'GAME_FILES_MODIFIED=NO',
 'PTAR_FILES_MODIFIED=NO'
) | Set-Content (Join-Path $session 'SESSION_CONTEXT.txt') -Encoding ASCII

Push-Location $session
try {
    & $exe --pid $p.Id --tid $mainThread.Id --seconds 12 --interval-ms 4
    if ($LASTEXITCODE -ne 0) { throw ('PASS 1 echouee, code '+$LASTEXITCODE) }
    & $exe --pid $p.Id --seconds 12 --interval-ms 4
    if ($LASTEXITCODE -ne 0) { throw ('PASS 2 echouee, code '+$LASTEXITCODE) }
}
finally { Pop-Location }

$zip = $session + '.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($session,$zip,[System.IO.Compression.CompressionLevel]::Optimal,$false)
try {
    [console]::Beep(1000,150); Start-Sleep -Milliseconds 100
    [console]::Beep(1300,150); Start-Sleep -Milliseconds 100
    [console]::Beep(1600,220)
} catch { }
Write-Host ('[PTAR] RESULTAT : '+$zip)
exit 0
