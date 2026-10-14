$ErrorActionPreference='Stop'

$path='lab/tsr1/ptar_tsr1_resolve_ps.hlsl'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

if($text.IndexOf('PTAR R12 RESAA1',[StringComparison]::Ordinal) -lt 0)
{
    throw 'R13 MAX1 requires R12 RESAA1 source baseline'
}
if($text.IndexOf('PTAR R13 PLSR MAX1',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R13 MAX1 patch already applied'
}

$main='float4 main(float2 uv : TEXCOORD0) : COLOR0'
$mainPos=$text.IndexOf($main,[StringComparison]::Ordinal)
if($mainPos -lt 0){throw 'R13 main anchor missing'}

$helper=@'
float4 PtCatmull1D(float4 p0,float4 p1,float4 p2,float4 p3,float t)
{
    float t2=t*t;
    float t3=t2*t;
    return 0.5f*((2.0f*p1)+(-p0+p2)*t+(2.0f*p0-5.0f*p1+4.0f*p2-p3)*t2+(-p0+3.0f*p1-3.0f*p2+p3)*t3);
}

'@
$text=$text.Insert($mainPos,$helper)

$anchor='    resolved.rgb+=aaDelta*aaStrength;'
$pos=$text.IndexOf($anchor,[StringComparison]::Ordinal)
if($pos -lt 0){throw 'R13 R12 AA anchor missing'}
$insertPos=$pos+$anchor.Length

$max=@'

    // PTAR R13 PLSR MAX1:
    // Deliberately spend the remaining GTX 960M budget in ONE resolve pass.
    // Eight extra current-source reads build a wider 4-tap support around the
    // exact 3:2 phase.  They serve two purposes at once:
    //   1) reconstruct source microdetail with a directional Catmull lattice;
    //   2) distinguish coherent geometric edges from 1-3px texture so RESAA
    //      can regularise real staircases without blurring text/weave/foliage.
    // No second 1080p pass, no extra history surface and no neural inference.
    float4 sxm10=LoadCurrent(q+float2(-1,0));
    float4 sxm11=LoadCurrent(q+float2(-1,1));
    float4 sxp20=LoadCurrent(q+float2( 2,0));
    float4 sxp21=LoadCurrent(q+float2( 2,1));
    float4 sym10=LoadCurrent(q+float2(0,-1));
    float4 sym11=LoadCurrent(q+float2(1,-1));
    float4 syp20=LoadCurrent(q+float2(0, 2));
    float4 syp21=LoadCurrent(q+float2(1, 2));

    float4 xOuter0=lerp(sxm10,sxm11,f.y);
    float4 xOuter3=lerp(sxp20,sxp21,f.y);
    float4 yOuter0=lerp(sym10,sym11,f.x);
    float4 yOuter3=lerp(syp20,syp21,f.x);

    float4 spatialX=PtCatmull1D(xOuter0,currentX0,currentX1,xOuter3,f.x);
    float4 spatialY=PtCatmull1D(yOuter0,currentY0,currentY1,yOuter3,f.y);

    // A gradient is normal to an edge, therefore the reconstruction tangent is
    // weighted by the opposite gradient component. Diagonals blend X/Y instead
    // of selecting a brittle single axis.
    float wideGX=Luma(xOuter3)-Luma(xOuter0);
    float wideGY=Luma(yOuter3)-Luma(yOuter0);
    float wideAX=abs(wideGX);
    float wideAY=abs(wideGY);
    float wideSum=wideAX+wideAY+0.0005f;
    float tangentX=wideAY/wideSum;
    float4 directionalSpatial=lerp(spatialY,spatialX,tangentX);

    // Coherence is intentionally cheap and branch-free.  Long geometric edges
    // keep the same gradient orientation from the central 2x2 support to the
    // wider support; tiny texture oscillations generally do not.
    float centralSum=cax+cay+0.0005f;
    float orientationDot=abs(cgx*wideGX+cgy*wideGY)/(centralSum*wideSum);
    float orientationCoherence=saturate(orientationDot*1.35f);
    float wideEdge=saturate((max(wideAX,wideAY)-0.018f)*11.0f);
    float coherentStair=wideEdge*orientationCoherence*stairOrientation;

    // R12 deliberately stayed conservative. MAX1 spends extra AA only where a
    // wide coherent slanted edge confirms that the pattern is geometry rather
    // than fine texture.  Fine-detail protection remains dominant.
    float maxAaDelta=aaReferenceLuma-Luma(resolved);
    maxAaDelta=clamp(maxAaDelta,-aaLimit,aaLimit);
    float maxAaGate=coherentStair*lerp(1.00f,0.18f,detailProtect);
    resolved.rgb+=maxAaDelta*(0.24f*maxAaGate);

    // Fine-detail recovery.  Use the wide directional cubic only as a residual
    // estimator; R7 remains the spatial anchor.  Require sign agreement with
    // R7's own high-pass before allowing the strongest correction, which makes
    // false high-frequency invention much less likely than blind sharpening.
    float directionalDetail=Luma(directionalSpatial)-Luma(currentBilinear);
    float r7Residual=Luma(r7)-Luma(currentBilinear);
    float detailSignAgree=step(0.0f,directionalDetail*r7Residual);
    float detailLimit=0.012f+0.16f*lrange;
    directionalDetail=clamp(directionalDetail,-detailLimit,detailLimit);
    float detailActivity=saturate((abs(directionalDetail)-0.0015f)*34.0f);
    float edgeRingGuard=lerp(1.00f,0.48f,coherentStair);
    float microGain=detailActivity*edgeRingGuard*lerp(0.28f,0.62f,detailSignAgree);

    // Preserve the temporal lattice when it has genuinely complementary phase,
    // but pull the final luminance toward a sharper R7+source-detail target in
    // regions that carry native-resolution cues (text, weave, foliage, ground).
    float targetMicroLuma=Luma(r7)+0.52f*directionalDetail;
    float microDelta=targetMicroLuma-Luma(resolved);
    float microLimit=0.010f+0.14f*lrange;
    microDelta=clamp(microDelta,-microLimit,microLimit);
    float phaseKeep=lerp(1.00f,0.72f,saturate(dominantSupport*trust));
    float texturePriority=lerp(0.55f,1.00f,detailProtect);
    resolved.rgb+=microDelta*(microGain*phaseKeep*texturePriority);
'@

$text=$text.Insert($insertPos,$max)
$text=$text.Replace('// PTAR R12 RESAA1 over R11 PLSR1 - phase-lattice SR plus selective resolution anti-aliasing for exact 720p -> 1080p.','// PTAR R13 PLSR MAX1 over R12 - maximum practical single-pass detail, sharpness and coherent-edge regularity for GTX 960M.')

foreach($token in @(
    'PTAR R13 PLSR MAX1',
    'PtCatmull1D',
    'directionalSpatial',
    'orientationCoherence',
    'coherentStair',
    'detailSignAgree',
    'targetMicroLuma',
    'microGain*phaseKeep*texturePriority'))
{
    if($text.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R13 MAX1 final token missing: $token"
    }
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R13_PLSR_MAX1_PATCH=PASS'
Write-Host 'R13_OBJECTIVE=MAX_PERCEIVED_1080P_FINE_DETAIL_SHARPNESS_REGULARITY'
Write-Host 'R13_EXTRA_CURRENT_SOURCE_READS_OVER_R12=8'
Write-Host 'R13_EXTRA_PASSES_OVER_R12=0'
Write-Host 'R13_HISTORY_SURFACES_OVER_R12=0'
Write-Host 'R13_FG_SCOPE=UNCHANGED_R12_FG_OFF_ONLY'
