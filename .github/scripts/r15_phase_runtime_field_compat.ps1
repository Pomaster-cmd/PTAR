$ErrorActionPreference='Stop'

$source='lab/r15_jitter9/apply_r15_phase_superresolve_runtime.ps1'
$tmp='build_x86/apply_r15_phase_superresolve_runtime_field.ps1'
$utf8=New-Object System.Text.UTF8Encoding($false)
$s=[IO.File]::ReadAllText((Resolve-Path $source)) -replace "`r`n","`n"

$old=@'
$axisFunction=@'
static float PtR15AxisJitter(int phaseClass)
{
    if(phaseClass==0) return -1.0f/3.0f;
    if(phaseClass==2) return  1.0f/3.0f;
    return 0.0f;
}
'@
$axisPlus=$axisFunction+@'

static UINT PtR15AxisResidue(int phaseClass)
{
    // Calibrated D3D9 integer-raster-center ownership.
    if(phaseClass==0) return 1u;
    if(phaseClass==2) return 2u;
    return 0u;
}
'@
$p=Replace-One $p $axisFunction $axisPlus 'D3D9 phase-to-residue mapping'
'@

$new=@'
$axisPattern='(?s)static float PtR15AxisJitter\(int phaseClass\)\s*\{.*?return\s+0\.0f;\s*\}'
$axisMatches=[regex]::Matches($p,$axisPattern)
if($axisMatches.Count -ne 1)
{ throw "R15 phase runtime PtR15AxisJitter count=$($axisMatches.Count)" }
$axisFunction=$axisMatches[0].Value
$axisPlus=$axisFunction+@'

static UINT PtR15AxisResidue(int phaseClass)
{
    // Calibrated D3D9 integer-raster-center ownership.
    if(phaseClass==0) return 1u;
    if(phaseClass==2) return 2u;
    return 0u;
}
'@
$p=$p.Substring(0,$axisMatches[0].Index)+$axisPlus+$p.Substring($axisMatches[0].Index+$axisMatches[0].Length)
'@

$a=$s.IndexOf($old,[StringComparison]::Ordinal)
if($a -lt 0){throw 'R15 field compat: legacy phase axis block missing'}
if($s.IndexOf($old,$a+$old.Length,[StringComparison]::Ordinal) -ge 0){throw 'R15 field compat: legacy phase axis block not unique'}
$s=$s.Substring(0,$a)+$new+$s.Substring($a+$old.Length)

New-Item -ItemType Directory -Force (Split-Path $tmp) | Out-Null
[IO.File]::WriteAllText((Join-Path (Get-Location) $tmp),$s,$utf8)
& (Join-Path (Get-Location) $tmp)
if($LASTEXITCODE){throw "R15 field compat phase runtime failed: $LASTEXITCODE"}
Write-Host 'R15_PHASE_AXIS_WHITESPACE_COMPAT=PASS'
