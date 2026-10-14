$ErrorActionPreference='Stop'

$path='lab/tsr1/ptar_tsr1_resolve_ps.hlsl'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

if($text.IndexOf('PTAR R10 TSR3 ALIASRESIDUAL1',[StringComparison]::Ordinal) -lt 0)
{
    throw 'R11 requires R10 ALIASRESIDUAL1 source baseline'
}
if($text.IndexOf('PTAR R11 PLSR1 PHASELATTICE1',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R11 patch already applied'
}

$main='float4 main(float2 uv : TEXCOORD0) : COLOR0'
$mainPos=$text.IndexOf($main,[StringComparison]::Ordinal)
if($mainPos -lt 0){throw 'R11 main anchor missing'}

$helpers=@'
float PtSafeInv(float v)
{
    float s=lerp(-1.0f,1.0f,step(0.0f,v));
    return s/max(abs(v),0.020f);
}

float4 PtLatticeWeights(float f,float hf)
{
    // Two unit-spaced sample pairs: current {-f,1-f} and aligned history
    // {-hf,1-hf}.  Evaluate the unique cubic through those four irregular
    // lattice points directly at the current output position (x=0).
    float d=hf-f;
    float fm1=f-1.0f;
    float hm1=hf-1.0f;
    float inv0=PtSafeInv(d*(1.0f-d));
    float inv1=PtSafeInv(d*(1.0f+d));
    return float4(
        fm1*hf*hm1*inv0,
        f*hf*hm1*inv1,
        -f*fm1*hm1*inv1,
        -f*fm1*hf*inv0);
}

'@
$text=$text.Insert($mainPos,$helpers)

$startMarker='    float wc00=SharpWeight(float2(-f.x,-f.y));'
$endMarker='    resolved-=aliasCorrectionGain*saturate(phaseDiversity)*trust*aliasResidual;'
$start=$text.IndexOf($startMarker,[StringComparison]::Ordinal)
$end=$text.IndexOf($endMarker,[StringComparison]::Ordinal)
if($start -lt 0 -or $end -lt 0 -or $end -le $start)
{
    throw 'R11 R10 temporal reconstruction anchors missing'
}
$end=$end+$endMarker.Length

$plsr=@'
    // PTAR R11 PLSR1 PHASELATTICE1:
    // R8-R10 blended two reconstructions.  That improves average error but it
    // still behaves like a temporal low-pass on exactly the 1-3px structures
    // which create the impression of native resolution.  PLSR1 instead treats
    // current and aligned history as two interleaved sampling lattices.
    //
    // For the axis carrying the best complementary subpixel phase, the four
    // available sample positions form an irregular 1D lattice.  We evaluate
    // the cubic passing through those four samples at the target position.
    // The orthogonal axis is only linearly collapsed first, which keeps the
    // implementation to the same four current + four history reads already
    // paid by R10.  No extra pass, no generic sharpening and no invented noise.
    float localRangeGuard=saturate((lrange-0.03f)*10.0f);
    float r7DetailGuard=saturate((r7Detail-0.003f)*40.0f);
    float detailProtect=max(localRangeGuard,r7DetailGuard);

    // A phase close to the current lattice is redundant and numerically poor
    // for a four-point solve.  Only complementary phases are allowed to steer
    // the reconstruction; otherwise the exact R7 baseline remains dominant.
    float2 phaseDelta=abs(hf-f);
    float2 phaseSep=min(phaseDelta,1.0f-phaseDelta);
    float2 latticeValidity=saturate((phaseSep-0.08f)*5.8823529f);
    float2 axisSupport=phaseAxis*phaseAxis*latticeValidity;
    float dominantX=step(axisSupport.y,axisSupport.x);
    float dominantSupport=lerp(axisSupport.y,axisSupport.x,dominantX);

    // Collapse each 2x2 lattice along the orthogonal axis, then select the
    // dominant reconstruction axis without adding texture reads.
    float4 currentX0=lerp(s11,s12,f.y);
    float4 currentX1=lerp(s21,s22,f.y);
    float4 historyX0=lerp(h11,h12,hf.y);
    float4 historyX1=lerp(h21,h22,hf.y);

    float4 currentY0=lerp(s11,s21,f.x);
    float4 currentY1=lerp(s12,s22,f.x);
    float4 historyY0=lerp(h11,h21,hf.x);
    float4 historyY1=lerp(h12,h22,hf.x);

    float4 v0=lerp(currentY0,currentX0,dominantX);
    float4 v1=lerp(currentY1,currentX1,dominantX);
    float4 v2=lerp(historyY0,historyX0,dominantX);
    float4 v3=lerp(historyY1,historyX1,dominantX);
    float latticeF=lerp(f.y,f.x,dominantX);
    float latticeHF=lerp(hf.y,hf.x,dominantX);
    float4 latticeW=PtLatticeWeights(latticeF,latticeHF);
    float4 latticeSample=v0*latticeW.x+v1*latticeW.y+v2*latticeW.z+v3*latticeW.w;

    // In flat regions stay conservative.  On genuine detail-bearing regions
    // the phase-lattice reconstruction is allowed to replace the spatial R7
    // estimate almost completely: this is where perceived resolution is won.
    float latticeStrength=lerp(0.80f,1.00f,detailProtect);
    float latticeBlend=saturate(dominantSupport)*trust*latticeStrength;
    float4 resolved=lerp(r7,latticeSample,latticeBlend);
'@
$text=$text.Substring(0,$start)+$plsr+$text.Substring($end)
$text=$text.Replace('// PTAR R9 TSR2 DETAILPRESERVE1 - temporal super-resolution resolve for exact 720p -> 1080p.','// PTAR R11 PLSR1 PHASELATTICE1 over R10 - phase-lattice super-resolution for exact 720p -> 1080p.')

foreach($token in @(
    'PTAR R11 PLSR1 PHASELATTICE1',
    'PtLatticeWeights',
    'phaseSep',
    'axisSupport=phaseAxis*phaseAxis*latticeValidity',
    'latticeStrength=lerp(0.80f,1.00f,detailProtect)',
    'float4 resolved=lerp(r7,latticeSample,latticeBlend)'))
{
    if($text.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R11 final token missing: $token"
    }
}
foreach($removed in @('float wc00=SharpWeight','aliasResidual=historyBilinear-currentBilinear'))
{
    if($text.IndexOf($removed,[StringComparison]::Ordinal) -ge 0)
    {
        throw "R11 obsolete temporal averaging token remained: $removed"
    }
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R11_PLSR1_PHASELATTICE1_PATCH=PASS'
Write-Host 'R11_EXTRA_TEXTURE_READS_OVER_R10=0'
Write-Host 'R11_EXTRA_PASSES_OVER_R10=0'
Write-Host 'R11_TEMPORAL_AVERAGING_CORE=REMOVED_REPLACED_BY_IRREGULAR_LATTICE_CUBIC'
Write-Host 'R11_FG_SCOPE=UNCHANGED_R10_FG_OFF_ONLY'
