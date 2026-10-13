$ErrorActionPreference='Stop'

$canonical='lab/tsr1/run_r8_tsr_ci.ps1'
$text=[IO.File]::ReadAllText((Resolve-Path $canonical)) -replace "`r`n","`n"
$old='.\lab\tsr1\apply_r8_proxy_patch.ps1'
$new='.\lab\tsr1\apply_r8_proxy_patch_compat.ps1'
$first=$text.IndexOf($old,[StringComparison]::Ordinal)
if($first -lt 0){throw 'R8 CI compat: canonical proxy patch call missing'}
$second=$text.IndexOf($old,$first+$old.Length,[StringComparison]::Ordinal)
if($second -ge 0){throw 'R8 CI compat: canonical proxy patch call not unique'}
$text=$text.Substring(0,$first)+$new+$text.Substring($first+$old.Length)

$temp='lab/tsr1/.run_r8_tsr_ci_compat_generated.ps1'
$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path (Get-Location) $temp),$text,$utf8)
try
{
    & $temp
    if($LASTEXITCODE){throw "R8 CI compat failed with exit code $LASTEXITCODE"}
}
finally
{
    Remove-Item $temp -Force -ErrorAction SilentlyContinue
}

Write-Host 'R8_TSR1_CI_COMPAT=PASS'
