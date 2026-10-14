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
float PtCatmull1D(float p0,float p1,float p2,float p3,float t)
{
    float t2=t*t;
    return 0.5f*((2.0f*p1)+(-p0+p2)*t+(2.0f*p0-5.0f*p1+4.0f*p2-p3)*t2+(-p0+3.0f*p1-3.0f*p2+p3)*(t2*t));
}

'@
$text=$text.Insert($mainPos,$helper)

$anchor='    resolved.rgb+=aaDelta*aaStrength;'
$pos=$text.IndexOf($anchor,[StringComparison]::Ordinal)
if($pos -lt 0){throw 'R13 R12 AA anchor missing'}
$insertPos=$pos+$anchor.Length

$max=@'

    // PTAR R13 PLSR MAX1:
    // Spend the remaining GTX 960M budget in the existing resolve pass only.
    // Eight extra current-source reads create a wider support shared by fine-
    // detail recovery and coherent-edge classification.  Reconstruction is
    // luminance-only here: perceived resolution lives mainly in luma and this
    // saves a large amount of ps_3_0 ALU versus RGB cubic reconstruction.
    float4 sxm10=LoadCurrent(q+float2(-1,0));
    float4 sxm11=LoadCurrent(q+float2(-1,1));
    float4 sxp20=LoadCurrent(q+float2( 2,0));
    float4 sxp21=LoadCurrent(q+float2( 2,1));
    float4 sym10=LoadCurrent(q+float2(0,-1));
    float4 sym11=LoadCurrent(q+float2(1,-1));
    float4 syp20=LoadCurrent(q+float2(0, 2));
    float4 syp21=LoadCurrent(q+float2(1, 2));

    float xOuter0L=lerp(Luma(sxm10),Luma(sxm11),f.y);
    float xOuter3L=lerp(Luma(sxp20),Luma(sxp21),f.y);
    float yOuter0L=lerp(Luma(sym10),Luma(sym11),f.x);
    float yOuter3L=lerp(Luma(syp20),Luma(syp21),f.x);

    float currentX0L=lerp(l11,l12,f.y);
    float currentX1L=lerp(l21,l22,f.y);
    float currentY0L=lerp(l11,l21,f.x);
    float currentY1L=lerp(l12,l22,f.x);
    float spatialXL=PtCatmull1D(xOuter0L,currentX0L,currentX1L,xOuter3L,f.x);
    float spatialYL=PtCatmull1D(yOuter0L,currentY0L,currentY1L,yOuter3L,f.y);

    // Gradient is normal to an edge, so the tangent reconstruction receives
    // weight from the opposite gradient component.  Diagonals blend X/Y.
    float wideGX=xOuter3L-xOuter0L;
    float wideGY=yOuter3L-yOuter0L;
    float wideAX=abs(wideGX);
    float wideAY=abs(wideGY);
    float wideSum=wideAX+wideAY+0.0005f;
    float tangentX=wideAY/wideSum;
    float directionalSpatialLuma=lerp(spatialYL,spatialXL,tangentX);

    // Long geometric edges keep a coherent gradient from the central 2x2 to
    // the wider support.  Fine texture usually oscillates and loses coherence.
    float centralSum=cax+cay+0.0005f;
    float orientationCoherence=saturate(abs(cgx*wideGX+cgy*wideGY)/(centralSum*wideSum)*1.35f);
    float wideEdge=saturate((max(wideAX,wideAY)-0.018f)*11.0f);
    float coherentStair=wideEdge*orientationCoherence*stairOrientation;

    // R12 AA remains the base. MAX1 adds coverage correction only on confirmed
    // coherent slanted geometry, with strong protection for 1-3px detail.
    float maxAaDelta=clamp(aaReferenceLuma-Luma(resolved),-aaLimit,aaLimit);
    float maxAaGate=coherentStair*lerp(1.00f,0.18f,detailProtect);
    resolved.rgb+=maxAaDelta*(0.24f*maxAaGate);

    // Recover fine source structure without a blind global sharpen.  The wide
    // directional residual gets its strongest gain only when its sign agrees
    // with R7's independently reconstructed high-pass.
    float directionalDetail=directionalSpatialLuma-Luma(currentBilinear);
    float r7Residual=Luma(r7)-Luma(currentBilinear);
    float detailSignAgree=step(0.0f,directionalDetail*r7Residual);
    float detailLimit=0.012f+0.16f*lrange;
    directionalDetail=clamp(directionalDetail,-detailLimit,detailLimit);
    float detailActivity=saturate((abs(directionalDetail)-0.0015f)*34.0f);
    float microGain=detailActivity*lerp(1.00f,0.48f,coherentStair)*lerp(0.28f,0.62f,detailSignAgree);

    // Keep phase-lattice SR as the temporal source of new samples, then pull
    // only luma toward a sharper R7+wide-source target where native-resolution
    // cues are actually present.
    float targetMicroLuma=Luma(r7)+0.52f*directionalDetail;
    float microLimit=0.010f+0.14f*lrange;
    float microDelta=clamp(targetMicroLuma-Luma(resolved),-microLimit,microLimit);
    float phaseKeep=lerp(1.00f,0.72f,saturate(dominantSupport*trust));
    float texturePriority=lerp(0.55f,1.00f,detailProtect);
    resolved.rgb+=microDelta*(microGain*phaseKeep*texturePriority);
'@

$text=$text.Insert($insertPos,$max)
$text=$text.Replace('// PTAR R12 RESAA1 over R11 PLSR1 - phase-lattice SR plus selective resolution anti-aliasing for exact 720p -> 1080p.','// PTAR R13 PLSR MAX1 over R12 - maximum practical single-pass detail, sharpness and coherent-edge regularity for GTX 960M.')

foreach($token in @(
    'PTAR R13 PLSR MAX1',
    'PtCatmull1D',
    'directionalSpatialLuma',
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
Write-Host 'R13_WIDE_RECONSTRUCTION=LUMA_ONLY_DIRECTIONAL_CATMULL'
Write-Host 'R13_EXTRA_CURRENT_SOURCE_READS_OVER_R12=8'
Write-Host 'R13_EXTRA_PASSES_OVER_R12=0'
Write-Host 'R13_HISTORY_SURFACES_OVER_R12=0'
Write-Host 'R13_FG_SCOPE=UNCHANGED_R12_FG_OFF_ONLY'
