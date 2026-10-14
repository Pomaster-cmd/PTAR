$ErrorActionPreference='Stop'

$path='lab/tsr1/ptar_tsr1_resolve_ps.hlsl'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

if($text.IndexOf('PTAR R13 PLSR MAX1',[StringComparison]::Ordinal) -lt 0){throw 'R14 MFSR1 requires R13 MAX1 source baseline'}
if($text.IndexOf('PTAR R14 MFSR1 ACCUM1080',[StringComparison]::Ordinal) -ge 0){throw 'R14 MFSR1 patch already applied'}

$samplerAnchor='sampler2D gR7      : register(s3);'
$samplerPos=$text.IndexOf($samplerAnchor,[StringComparison]::Ordinal)
if($samplerPos -lt 0){throw 'R14 sampler anchor missing'}
$text=$text.Insert($samplerPos+$samplerAnchor.Length,@'

sampler2D gAccumHistory : register(s4);
float4 gAccumControl : register(c1); // x = full-resolution accumulation valid
'@)

# Insert AFTER the qualified R13/R9 fail-soft temporal envelope, immediately
# before alpha is finalized. This is deliberate: the old one-frame envelope
# must not clip away detail recovered from older full-resolution phases.
$alphaAnchor='    resolved.a=r7.a;'
$alphaPos=$text.IndexOf($alphaAnchor,[StringComparison]::Ordinal)
if($alphaPos -lt 0){throw 'R14 final alpha anchor missing'}
$mfsr=@'
    // PTAR R14 MFSR1 ACCUM1080:
    // R13 exhausts the useful single-frame/source-history neighbourhood. R14
    // changes topology: reproject the previous 1080p reconstruction and carry
    // only its high-frequency residual. Low-frequency colour/geometry always
    // comes from the current R13 result.
    float2 prevUv=uv+motion/gSizes.xy;
    float2 prevUvClamped=saturate(prevUv);
    float accumInside=
        step(0.0f,prevUv.x)*step(prevUv.x,1.0f)*
        step(0.0f,prevUv.y)*step(prevUv.y,1.0f);
    float4 accumPrev=tex2Dlod(gAccumHistory,float4(prevUvClamped,0.0f,0.0f));
    float prevAccumConfidence=saturate(accumPrev.a*gAccumControl.x);

    // Clip previous full-resolution history against current evidence. R7 and
    // final R13 are included to protect genuine reconstruction detail.
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

    // historyBilinear is the previous 720p source aligned to this current pixel.
    // Subtracting it isolates information that did not exist in low resolution.
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

    // Preserve already-recovered detail after motion stops, while complementary
    // phases increase the carry weight. Disocclusion/photo mismatch kills it.
    float carryTrust=
        accumInside*prevAccumConfidence*trust*
        accumPhotoTrust*clipTrust;
    float carryWeight=carryTrust*lerp(
        0.46f,0.78f,saturate(phaseDiversity));
    resolved.rgb+=transportedResidual*carryWeight;

    // Alpha is private confidence in the 1080p ping-pong history.
    float retainedConfidence=
        prevAccumConfidence*accumInside*accumPhotoTrust*clipTrust*0.985f;
    float newPhaseEvidence=saturate(phaseDiversity)*trust;
    float accumConfidence=saturate(
        max(0.16f,retainedConfidence)+0.30f*newPhaseEvidence);
'@
$text=$text.Insert($alphaPos,$mfsr)

$alphaPos=$text.IndexOf($alphaAnchor,[StringComparison]::Ordinal)
if($alphaPos -lt 0){throw 'R14 alpha replacement anchor missing'}
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
    if($text.IndexOf($token,[StringComparison]::Ordinal) -lt 0){throw "R14 final token missing: $token"}
}
# Verify that R14 really sits after the legacy temporal envelope.
$envPos=$text.IndexOf('resolved=clamp(',[StringComparison]::Ordinal)
$r14Pos=$text.IndexOf('// PTAR R14 MFSR1 ACCUM1080:',[StringComparison]::Ordinal)
if($envPos -lt 0 -or $r14Pos -le $envPos){throw 'R14 accumulation was not placed after final R13 envelope'}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R14_MFSR1_ACCUM1080_PATCH=PASS'
Write-Host 'R14_HISTORY_DOMAIN=PRIVATE_1920X1080_RECONSTRUCTION'
Write-Host 'R14_HISTORY_TRANSPORT=REPROJECTED_HIGH_FREQUENCY_RESIDUAL'
Write-Host 'R14_INSERTION=AFTER_R13_FAILSOFT_ENVELOPE'
Write-Host 'R14_CONFIDENCE_STORAGE=ACCUM_ALPHA'
Write-Host 'R14_SOURCE_HISTORY=R13_ONE_FRAME_PLUS_MULTI_FRAME_1080P_RESIDUAL'
Write-Host 'R14_ZERO_MOTION_POLICY=KEEP_PREVIOUSLY_RECOVERED_VALID_DETAIL'
Write-Host 'R14_FG_SCOPE=UNCHANGED_R13_FG_OFF_ONLY'
