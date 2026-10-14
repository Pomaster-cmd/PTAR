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
    // Each corner serves BOTH X and Y reconstruction, keeping memory traffic at
    // only four extra reads while preserving phase-aware wide luma information.
    float4 cmm=LoadCurrent(q+float2(-1,-1));
    float4 cpm=LoadCurrent(q+float2( 2,-1));
    float4 cmp=LoadCurrent(q+float2(-1, 2));
    float4 cpp=LoadCurrent(q+float2( 2, 2));
    float lmm=Luma(cmm);
    float lpm=Luma(cpm);
    float lmp=Luma(cmp);
    float lpp=Luma(cpp);

    // Outer corners are three source pixels apart. Map the current subpixel
    // phase into that span so the same four reads provide X and Y outer pairs.
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

    // Follow the edge tangent, using the central RESAA gradient already paid.
    float tangentX=cay/(cax+cay+0.0005f);
    float directionalSpatialLuma=lerp(spatialYL,spatialXL,tangentX);

    // Wide geometry classification strengthens AA on persistent staircase
    // silhouettes while detailProtect suppresses it on text/weave/foliage.
    // Crucially, MAX1 itself is enabled only when current/history carry a real
    // complementary phase. Zero-motion/failure therefore preserves the exact
    // R7 fallback contract rather than inventing a new purely spatial path.
    float wideEdge=saturate((max(abs(xOuter3L-xOuter0L),abs(yOuter3L-yOuter0L))-0.018f)*11.0f);
    float coherentStair=wideEdge*stairOrientation*lerp(1.00f,0.22f,detailProtect);
    float maxPhase=historyAA;
    float maxAaDelta=clamp(aaReferenceLuma-Luma(resolved),-aaLimit,aaLimit);
    resolved.rgb+=maxAaDelta*(0.24f*coherentStair*maxPhase);

    // Recover source microstructure in luma. R11 already computed detailProtect
    // from local range plus the independent R7 high-frequency magnitude, so
    // reuse that evidence instead of recomputing an expensive second luma
    // residual/sign test. Temporal phase gating also prevents static false detail.
    float directionalDetail=directionalSpatialLuma-Luma(currentBilinear);
    float detailLimit=0.012f+0.16f*lrange;
    directionalDetail=clamp(directionalDetail,-detailLimit,detailLimit);
    float detailActivity=saturate((abs(directionalDetail)-0.0015f)*34.0f);
    float microGain=maxPhase*detailActivity*lerp(0.34f,0.62f,detailProtect)*lerp(1.00f,0.50f,coherentStair);

    // PLSR remains the source of complementary temporal samples. MAX1 only
    // pulls luminance toward a sharper R7+wide-source target where detail is
    // supported, with no extra history surface or full-resolution pass.
    float targetMicroLuma=Luma(r7)+0.52f*directionalDetail;
    float microLimit=0.010f+0.14f*lrange;
    float microDelta=clamp(targetMicroLuma-Luma(resolved),-microLimit,microLimit);
    resolved.rgb+=microDelta*microGain;
'@

$text=$text.Insert($insertPos,$max)
$text=$text.Replace('// PTAR R12 RESAA1 over R11 PLSR1 - phase-lattice SR plus selective resolution anti-aliasing for exact 720p -> 1080p.','// PTAR R13 PLSR MAX1 over R12 - maximum practical single-pass detail, sharpness and coherent-edge regularity for GTX 960M.')

foreach($token in @(
    'PTAR R13 PLSR MAX1',
    'PtCatmull1D',
    'directionalSpatialLuma',
    'coherentStair',
    'float maxPhase=historyAA',
    'detailProtect',
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
Write-Host 'R13_FINE_DETAIL_GATE=REUSE_R11_R7_DETAIL_SUPPORT_PLUS_TEMPORAL_PHASE'
Write-Host 'R13_ZERO_MOTION_CONTRACT=EXACT_R7_FALLBACK'
Write-Host 'R13_EXTRA_CURRENT_SOURCE_READS_OVER_R12=4'
Write-Host 'R13_EXTRA_PASSES_OVER_R12=0'
Write-Host 'R13_HISTORY_SURFACES_OVER_R12=0'
Write-Host 'R13_FG_SCOPE=UNCHANGED_R12_FG_OFF_ONLY'
