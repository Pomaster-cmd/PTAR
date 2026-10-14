$ErrorActionPreference='Stop'

$path='lab/tsr1/ptar_tsr1_resolve_ps.hlsl'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

if($text.IndexOf('PTAR R9 TSR2 DETAILPRESERVE1',[StringComparison]::Ordinal) -lt 0)
{
    throw 'R10 requires exact R9 DETAILPRESERVE1 source baseline'
}
if($text.IndexOf('PTAR R10 TSR3 ALIASRESIDUAL1',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R10 patch already applied'
}

$anchor='    float4 resolved=lerp(r7,unionSample,tsrBlend);'
$matches=[regex]::Matches($text,[regex]::Escape($anchor))
if($matches.Count -ne 1)
{
    throw "R10 resolve semantic anchor count=$($matches.Count)"
}

$insert=@'

    // PTAR R10 TSR3 ALIASRESIDUAL1:
    // R8/R9 temporal union is intentionally positive-weight and stable, but real
    // POINT/native captures show that this can still erase part of the phase
    // structure which distinguishes true 1080p microdetail from 720p smoothing.
    // The aligned history-vs-current bilinear delta is the directly observed
    // lattice alias residual. Subtracting a small, confidence-gated fraction
    // from the R9 result counteracts that averaging without a generic sharpen.
    // Axis-dominant motion can use the stronger correction because it provides
    // a cleaner complementary lattice; diagonal phase diversity remains more
    // conservative. No new texture read and no extra pass are introduced.
    float phaseAnisotropy=abs(phaseAxis.x-phaseAxis.y);
    float aliasCorrectionGain=lerp(0.15f,0.50f,phaseAnisotropy);
    float4 aliasResidual=historyBilinear-currentBilinear;
    resolved-=aliasCorrectionGain*saturate(phaseDiversity)*trust*aliasResidual;
'@
$text=$text.Replace($anchor,$anchor+$insert)
$text=$text.Replace('// PTAR R9 TSR2 DETAILPRESERVE1 - temporal super-resolution resolve for exact 720p -> 1080p.','// PTAR R10 TSR3 ALIASRESIDUAL1 over R9 DETAILPRESERVE1 - temporal super-resolution resolve for exact 720p -> 1080p.')

foreach($token in @(
    'PTAR R10 TSR3 ALIASRESIDUAL1',
    'phaseAnisotropy',
    'aliasCorrectionGain=lerp(0.15f,0.50f,phaseAnisotropy)',
    'aliasResidual=historyBilinear-currentBilinear',
    'resolved-=aliasCorrectionGain*saturate(phaseDiversity)*trust*aliasResidual'))
{
    if($text.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R10 final token missing: $token"
    }
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R10_TSR3_ALIASRESIDUAL1_PATCH=PASS'
Write-Host 'R10_EXTRA_TEXTURE_READS_OVER_R9=0'
Write-Host 'R10_EXTRA_PASSES_OVER_R9=0'
Write-Host 'R10_FG_SCOPE=UNCHANGED_R9_FG_OFF_ONLY'
