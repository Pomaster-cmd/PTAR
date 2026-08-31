param(
  [Parameter(Mandatory=$true)][string]$Core,
  [Parameter(Mandatory=$true)][string]$Log
)

$ErrorActionPreference = 'Stop'

function Append-AsciiLine([string]$Text) {
    $enc = [Text.Encoding]::Default
    [IO.File]::AppendAllText($Log, $Text + [Environment]::NewLine, $enc)
}

try {
    if (-not (Test-Path -LiteralPath $Core -PathType Leaf)) {
        Append-AsciiLine "[TEE FAIL] Core installer absent: $Core"
        [Console]::Error.WriteLine("[TEE FAIL] Core installer absent: " + $Core)
        exit 90
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    $psi.Arguments = '/D /S /C ""' + $Core + '" 2>&1"'
    $psi.WorkingDirectory = [IO.Path]::GetDirectoryName($Core)
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $false
    $psi.RedirectStandardInput = $false

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi

    Append-AsciiLine "[TEE] RAW CONSOLE+LOG MIRROR START"

    if (-not $p.Start()) {
        Append-AsciiLine "[TEE FAIL] Process.Start returned false"
        [Console]::Error.WriteLine("[TEE FAIL] Impossible de lancer le core.")
        exit 91
    }

    $src = $p.StandardOutput.BaseStream
    $dst = [Console]::OpenStandardOutput()
    $mode = [IO.FileMode]::Append
    $access = [IO.FileAccess]::Write
    $share = [IO.FileShare]::ReadWrite
    $fs = New-Object IO.FileStream($Log, $mode, $access, $share)
    $buf = New-Object byte[] 4096

    try {
        while (($n = $src.Read($buf, 0, $buf.Length)) -gt 0) {
            $dst.Write($buf, 0, $n)
            $dst.Flush()
            $fs.Write($buf, 0, $n)
            $fs.Flush()
        }
    }
    finally {
        if ($fs) { $fs.Flush(); $fs.Dispose() }
    }

    $p.WaitForExit()
    $rc = $p.ExitCode
    Append-AsciiLine ("[TEE] RAW CONSOLE+LOG MIRROR END RC=" + $rc)
    exit $rc
}
catch {
    try {
        Append-AsciiLine ("[TEE EXCEPTION] " + $_.Exception.GetType().FullName + ": " + $_.Exception.Message)
    } catch {}
    [Console]::Error.WriteLine("[TEE EXCEPTION] " + $_.Exception.Message)
    exit 92
}
