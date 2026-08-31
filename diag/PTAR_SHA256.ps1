param([Parameter(Mandatory=$true)][string]$InputFile)
$ErrorActionPreference = 'Stop'
try {
    $raw = ([string]$InputFile).Trim()
    $raw = $raw.Trim([char]34)
    $p = [System.IO.Path]::GetFullPath($raw)
    if (-not [System.IO.File]::Exists($p)) { exit 2 }
    $fs = [System.IO.File]::Open($p,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite)
    try {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { $hash = $sha.ComputeHash($fs) } finally { $sha.Dispose() }
    } finally { $fs.Dispose() }
    $hex = -join ($hash | ForEach-Object { $_.ToString('x2') })
    Write-Output $hex
    exit 0
} catch {
    Write-Error $_.Exception.Message
    exit 3
}
