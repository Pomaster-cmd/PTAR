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
    // Maximum practical single-pass quality for GTX 960M. Four shared outer
    // corner samples enlarge the current-source support to a sparse 4x4 cell.
    // Each corner serves BOTH X and Y reconstruction, halving the extra memory
    // traffic of the first MAX1 prototype while keeping phase-aware wide luma.
    float4 cmm=LoadCurrent(q+float2(-1,-1));
    float4 cpm=LoadCurrent(q+float2( 2,-1));
    float4 cmp=LoadCurrent(q+float2(-1, 2));
    float4 cpp=LoadCurrent(q+float2( 2, 2));
    float lmm=Luma(cmm);
    float lpm=Luma(cpm);
    float lmp=Luma(cmp);
    float lpp=Luma(cpp);

    // The requested point lies between source rows/columns 0 and 1. Outer
    // corners sit at -1 and +2, so the matching phase on that 3-pixel span is
    // (f+1)/3. This gives both directional outer pairs from only four fetches.
    float outerTy=(f.y+1.0f)*(1.0f/3.0f);
    float outerTx=(f.x+1.0f)*(1.0f/3.0f);
    float xOuter0L=lerp(lmm,lmp,outerTy);
    float xOuter3L=lerp(lpm,lpp,outerTy);
    float yOuter0L=lerp(lmm,lpm,outerTx);
    float yOuter3L=lerp(lmp,lpp,outerTx);

    float currentX0L=lerp(l11,l12,f.y);
    float currentX1L=lerp(l21,l22,f.y);
    float currentY0L=lerp(l11,l21,f.x);
    float currentY1L=lerp(l12,l22,f.x);
    float spatialXL=PtCatmull1D(xOuter0L,currentX0L,currentX1L,xOuter3L,f.x);
    float spatialYL=PtCatmull1D(yOuter0L,currentY0L,currentY1L,yOuter3L,f.y);

    // Gradient is normal to an edge, therefore reconstruction follows the
    // tangent. Central gradients are already available from RESAA1 and are more
    // stable than a second normalisation on sparse wide samples.
    float tangentX=cay/(cax+cay+0.0005f);
    float directionalSpatialLuma=lerp(spatialYL,spatialXL,tangentX);

    // Wide support distinguishes persistent silhouette staircases from local
    // 1-3px texture. Detail protection remains dominant on text/weave/foliage.
    float wideGX=xOuter3L-xOuter0L;
    float wideGY=yOuter3L-yOuter0L;
    float wideEdge=saturate((max(abs(wideGX),abs(wideGY))-0.018f)*11.0f);
    float coherentStair=wideEdge*stairOrientation*lerp(1.00f,0.22f,detailProtect);
    float maxAaDelta=clamp(aaReferenceLuma-Luma(resolved),-aaLimit,aaLimit);
    resolved.rgb+=maxAaDelta*(0.24f*coherentStair);

    // Recover source microstructure as luma only. R7's independently generated
    // residual supplies a sign-consistency test so a wide cubic oscillation is
    // not blindly promoted as new native detail.
    float directionalDetail=directionalSpatialLuma-Luma(currentBilinear);
    float r7Residual=Luma(r7)-Luma(currentBilinear);
    float detailSignAgree=step(0.0f,directionalDetail*r7Residual);
    float detailLimit=0.012f+0.16f*lrange;
    directionalDetail=clamp(directionalDetail,-detailLimit,detailLimit);
    float detailActivity=saturate((abs(directionalDetail)-0.0015f)*34.0f);
    float microGain=detailActivity*lerp(0.28f,0.62f,detailSignAgree)*lerp(1.00f,0.50f,coherentStair);

    // PLSR remains the source of complementary temporal samples. MAX1 only
    // pulls luminance toward a sharper R7+wide-source target where detail is
    // supported, so no new history surface or full-resolution pass is needed.
    float targetMicroLuma=Luma(r7)+0.52f*directionalDetail;
    float microLimit=0.010f+0.14f*lrange;
    float microDelta=clamp(targetMicroLuma-Luma(resolved),-microLimit,microLimit);
    resolved.rgb+=microDelta*(microGain*lerp(0.55f,1.00f,detailProtect));
'@

$text=$text.Insert($insertPos,$max)
$text=$text.Replace('// PTAR R12 RESAA1 over R11 PLSR1 - phase-lattice SR plus selective resolution anti-aliasing for exact 720p -> 1080p.','// PTAR R13 PLSR MAX1 over R12 - maximum practical single-pass detail, sharpness and coherent-edge regularity for GTX 960M.')

foreach($token in @(
    'PTAR R13 PLSR MAX1',
    'PtCatmull1D',
    'directionalSpatialLuma',
    'coherentStair',
    'detailSignAgree',
    'targetMicroLuma'))
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
Write-Host 'R13_WIDE_RECONSTRUCTION=SHARED_4_CORNER_LUMA_DIRECTIONAL_CATMULL'
Write-Host 'R13_COHERENCE_GATE=COMPACT_WIDE_EDGE_X_STAIR_X_DETAILPROTECT'
Write-Host 'R13_EXTRA_CURRENT_SOURCE_READS_OVER_R12=4'
Write-Host 'R13_EXTRA_PASSES_OVER_R12=0'
Write-Host 'R13_HISTORY_SURFACES_OVER_R12=0'
Write-Host 'R13_FG_SCOPE=UNCHANGED_R12_FG_OFF_ONLY'
