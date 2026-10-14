$ErrorActionPreference='Stop'

$path='lab/tsr1/ptar_tsr1_resolve_ps.hlsl'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

if($text.IndexOf('PTAR R13 PLSR MAX1',[StringComparison]::Ordinal) -lt 0)
{
    throw 'R14 MFSR1 requires R13 MAX1 source baseline'
}
if($text.IndexOf('PTAR R14 MFSR1 ACCUM1080',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R14 MFSR1 patch already applied'
}

$samplerAnchor='sampler2D gR7      : register(s3);'
$samplerPos=$text.IndexOf($samplerAnchor,[StringComparison]::Ordinal)
if($samplerPos -lt 0){throw 'R14 sampler anchor missing'}
$samplerInsert=$samplerPos+$samplerAnchor.Length
$text=$text.Insert($samplerInsert,@'

sampler2D gAccumHistory : register(s4);
float4 gAccumControl : register(c1); // x = full-resolution accumulation valid
'@)

$anchor='    resolved=lerp(r7,resolved,temporalOutputGate);'
$pos=$text.IndexOf($anchor,[StringComparison]::Ordinal)
if($pos -lt 0){throw 'R14 R13 temporal output anchor missing'}
$insertPos=$pos+$anchor.Length

$mfsr=@'

    // PTAR R14 MFSR1 ACCUM1080:
    // R13 exhausts the useful single-frame/source-history neighbourhood.  R14
    // changes the topology: the previous 1080p reconstruction is reprojected
    // and only its high-frequency residual is transported onto the CURRENT
    // reconstruction. This preserves current-frame colour/geometry while
    // allowing subpixel information from more than one previous phase to live
    // beyond a single source-history frame.
    float2 prevUv=uv+motion/gSizes.xy;
    float2 prevUvClamped=saturate(prevUv);
    float accumInside=
        step(0.0f,prevUv.x)*step(prevUv.x,1.0f)*
        step(0.0f,prevUv.y)*step(prevUv.y,1.0f);
    float4 accumPrev=tex2Dlod(gAccumHistory,float4(prevUvClamped,0.0f,0.0f));
    float prevAccumConfidence=saturate(accumPrev.a*gAccumControl.x);

    // Clip the reprojected 1080p history against CURRENT evidence.  R7 and the
    // R13 candidate are explicitly included so real reconstructed detail is not
    // rejected merely because it lies outside the raw 2x2 720p extrema.
    float3 clipLo=min(currentMin.rgb,min(r7.rgb,resolved.rgb));
    float3 clipHi=max(currentMax.rgb,max(r7.rgb,resolved.rgb));
    float3 clipSpan=max(clipHi-clipLo,float3(0.003f,0.003f,0.003f));
    float3 accumClipped=clamp(
        accumPrev.rgb,
        clipLo-0.055f*clipSpan,
        clipHi+0.055f*clipSpan);
    float3 clipDelta=abs(accumPrev.rgb-accumClipped);
    float clipDistance=max(clipDelta.r,max(clipDelta.g,clipDelta.b));
    float clipBudget=0.012f+0.22f*lrange;
    float clipTrust=saturate(1.0f-clipDistance/max(clipBudget,0.001f));

    // historyBilinear is the previous 720p source aligned to the CURRENT pixel.
    // Subtracting it from the reprojected full-resolution accumulation isolates
    // the information that did not exist in the low-resolution history.  That
    // residual can be carried without low-frequency temporal averaging.
    float accumLuma=dot(accumClipped,float3(0.2126f,0.7152f,0.0722f));
    float historyLuma=Luma(historyBilinear);
    float accumPhotoError=abs(accumLuma-historyLuma);
    float accumPhotoBudget=0.045f+0.82f*lrange;
    float accumPhotoTrust=saturate(
        1.0f-accumPhotoError/max(accumPhotoBudget,0.001f));

    float3 transportedResidual=accumClipped-historyBilinear.rgb;
    float residualLimit=0.020f+0.30f*lrange;
    transportedResidual=clamp(
        transportedResidual,
        -residualLimit.xxx,
         residualLimit.xxx);

    // Carry already-proven high-resolution information even when the CURRENT
    // phase is redundant. New confidence, however, grows primarily when the new
    // source/history pair supplies a complementary phase. Thus a static stream
    // cannot manufacture confidence from identical 720p samples, while detail
    // recovered during earlier subpixel motion remains visible after motion
    // stops.
    float carryTrust=
        accumInside*prevAccumConfidence*trust*
        accumPhotoTrust*clipTrust;
    float carryWeight=carryTrust*lerp(
        0.46f,0.78f,saturate(phaseDiversity));
    resolved.rgb+=transportedResidual*carryWeight;

    // Confidence is stored in the alpha channel of the private A8R8G8B8 1080p
    // accumulation surfaces. Reprojection disagreement decays it immediately;
    // genuinely complementary current phases grow it. A small seed lets the
    // first R14 frame become useful history without pretending it is converged.
    float retainedConfidence=
        prevAccumConfidence*accumInside*accumPhotoTrust*clipTrust*0.985f;
    float newPhaseEvidence=saturate(phaseDiversity)*trust;
    float accumConfidence=saturate(
        max(0.16f,retainedConfidence)+0.30f*newPhaseEvidence);
'@
$text=$text.Insert($insertPos,$mfsr)

$alphaAnchor='    resolved.a=r7.a;'
$alphaPos=$text.IndexOf($alphaAnchor,[StringComparison]::Ordinal)
if($alphaPos -lt 0){throw 'R14 final alpha anchor missing'}
$text=$text.Substring(0,$alphaPos)+'    resolved.a=accumConfidence;'+$text.Substring($alphaPos+$alphaAnchor.Length)

$text=$text.Replace(
    '// PTAR R13 PLSR MAX1 over R12 - maximum practical single-pass detail, sharpness and coherent-edge regularity for GTX 960M.',
    '// PTAR R14 MFSR1 ACCUM1080 over R13 - multi-frame full-resolution residual accumulation for exact 720p -> 1080p.')

foreach($token in @(
    'PTAR R14 MFSR1 ACCUM1080',
    'gAccumHistory : register(s4)',
    'gAccumControl : register(c1)',
    'transportedResidual=accumClipped-historyBilinear.rgb',
    'carryWeight=carryTrust',
    'retainedConfidence',
    'resolved.a=accumConfidence'))
{
    if($text.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R14 final token missing: $token"
    }
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R14_MFSR1_ACCUM1080_PATCH=PASS'
Write-Host 'R14_HISTORY_DOMAIN=PRIVATE_1920X1080_RECONSTRUCTION'
Write-Host 'R14_HISTORY_TRANSPORT=REPROJECTED_HIGH_FREQUENCY_RESIDUAL'
Write-Host 'R14_CONFIDENCE_STORAGE=ACCUM_ALPHA'
Write-Host 'R14_SOURCE_HISTORY=R13_ONE_FRAME_PLUS_MULTI_FRAME_1080P_RESIDUAL'
Write-Host 'R14_ZERO_MOTION_POLICY=KEEP_PREVIOUSLY_RECOVERED_VALID_DETAIL'
Write-Host 'R14_FG_SCOPE=UNCHANGED_R13_FG_OFF_ONLY'
