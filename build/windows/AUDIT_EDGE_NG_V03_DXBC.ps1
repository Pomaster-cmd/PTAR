param([Parameter(Mandatory=$true)][string]$AssemblyPath)
if (-not (Test-Path -LiteralPath $AssemblyPath)) {
    Write-Host "[FAIL] ASM absent: $AssemblyPath"
    exit 2
}
$text = Get-Content -Raw -LiteralPath $AssemblyPath
$gather = ([regex]::Matches($text,'(?im)^\s*gather4(?:_po)?\b')).Count
$sampleL = ([regex]::Matches($text,'(?im)^\s*sample_l\b')).Count
$uav = ([regex]::Matches($text,'(?im)\b(store_uav|ld_uav|uav)\b')).Count
Write-Host "[DXBC] gather4=$gather sample_l=$sampleL uav_tokens=$uav"
if ($gather -ne 1) {
    Write-Host "[FAIL] expected exactly 1 gather4"
    exit 10
}
if ($sampleL -ne 4) {
    Write-Host "[FAIL] expected exactly 4 sample_l"
    exit 11
}
if ($uav -ne 0) {
    Write-Host "[FAIL] unexpected UAV path"
    exit 12
}
Write-Host "[PASS] logical 5-texture-op DXBC path confirmed."
exit 0
