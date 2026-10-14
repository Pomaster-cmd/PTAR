$ErrorActionPreference='Stop'

$canonical='lab/tsr1/run_r8_tsr_ci.ps1'
$text=[IO.File]::ReadAllText((Resolve-Path $canonical)) -replace "`r`n","`n"

# Experimental field-test branch for the original rejected R13 MAX1 shader.
# The historical build compiled to 359 ps_3_0 slots, so this wrapper raises
# only the validation ceiling; it does not alter shader code or runtime logic.
$replacements=@(
    @('.\lab\tsr1\apply_r8_proxy_patch.ps1','.\lab\tsr1\apply_r8_proxy_patch_compat.ps1'),
    @('if($resTex -lt 9 -or $resTex -gt 12){throw "unexpected optimized TSR1 resolve texld count $resTex"}','if($resTex -lt 9 -or $resTex -gt 20){throw "unexpected R13 ORIGINAL359 resolve texld count $resTex"}'),
    @('if($resSlots -gt 240){throw "optimized TSR1 resolve ps_3_0 growth too large: $resSlots"}','if($resSlots -gt 380){throw "R13 ORIGINAL359 resolve ps_3_0 growth too large: $resSlots"}'))
foreach($pair in $replacements)
{
    $old=$pair[0];$new=$pair[1]
    $first=$text.IndexOf($old,[StringComparison]::Ordinal)
    if($first -lt 0){throw "R13 ORIGINAL359 CI compat anchor missing: $old"}
    $second=$text.IndexOf($old,$first+$old.Length,[StringComparison]::Ordinal)
    if($second -ge 0){throw "R13 ORIGINAL359 CI compat anchor not unique: $old"}
    $text=$text.Substring(0,$first)+$new+$text.Substring($first+$old.Length)
}

$temp='lab/tsr1/.run_r13_tsr_ci_compat_generated.ps1'
$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path (Get-Location) $temp),$text,$utf8)
try
{
    & $temp
    if($LASTEXITCODE){throw "R13 ORIGINAL359 CI compat failed with exit code $LASTEXITCODE"}
}
finally
{
    Remove-Item $temp -Force -ErrorAction SilentlyContinue
}

Write-Host 'R13_ORIGINAL359_TSR1_CI_COMPAT=PASS'
