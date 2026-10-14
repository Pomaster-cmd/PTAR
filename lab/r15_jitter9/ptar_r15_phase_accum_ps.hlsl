// PTAR R15 TRUE720 JITTER9 - calibrated D3D9 phase-exact accumulator.
//
// This shader consumes the deliberate REAL-frame raster jitter implemented by
// apply_r15_game_sampling_runtime.ps1.  It is intentionally FG-OFF only.
// Generated frames never enter this reservoir.
//
// s0 = current deliberately-jittered 1280x720 REAL source
// s1 = previous private 1920x1080 phase reservoir (A = validity)
// s2 = current 1920x1080 R7/R13 spatial fallback generated from this source
// s3 = current->previous subpixel source-motion/confidence field
// c0 = sourceW,sourceH,outputW,outputH
// c1 = outputResidueX,outputResidueY,currentJitterX,currentJitterY
// c2 = previousJitterX,previousJitterY,historyValid,unused
//
// Calibrated D3D9 integer-raster-center ownership at exact 3:2:
//   phase class 0: jitter -1/3 source px -> output residue 1 mod 3
//   phase class 1: jitter  0             -> output residue 0 mod 3
//   phase class 2: jitter +1/3 source px -> output residue 2 mod 3
// The owned pixel is copied from one physical 720p source texel, never averaged.

sampler2D gCurrent      : register(s0);
sampler2D gAccumHistory : register(s1);
sampler2D gFallback     : register(s2);
sampler2D gMotion       : register(s3);

float4 gSizes    : register(c0);
float4 gPhase    : register(c1);
float4 gPrevious : register(c2);

static const float PTAR_TSR_MOTION_ZERO=0.5019607843137255f;

float PtLuma(float3 c)
{
    return dot(c,float3(0.2126f,0.7152f,0.0722f));
}

float PtModulo3Match(float p,float residue)
{
    float r=frac((p-residue)/3.0f);
    float d=min(r,1.0f-r);
    return 1.0f-step(0.0005f,d);
}

float PtSourceOffsetForResidue(float residue)
{
    // residue 0: out=3k   -> source=2k
    // residue 1: out=3k+1 -> source=2k+1
    // residue 2: out=3k+2 -> source=2k+1
    return 2.5f*residue-1.5f*residue*residue;
}

float4 PtLoadOwnedSource(float2 outPixel)
{
    float2 residue=gPhase.xy;
    float2 sourceOffset=float2(
        PtSourceOffsetForResidue(residue.x),
        PtSourceOffsetForResidue(residue.y));
    float2 sourceIndex=(2.0f*outPixel+sourceOffset)/3.0f;
    sourceIndex=clamp(sourceIndex,float2(0.0f,0.0f),gSizes.xy-1.0f);

    // DrawFullscreenPass configures LINEAR sampling for s0..s2, but an exact
    // texel-centre coordinate is still an exact texel measurement.  LOD0 is
    // explicit so the measured owned sample is not reconstructed spatially.
    float2 sourceUv=(sourceIndex+0.5f)/gSizes.xy;
    return tex2Dlod(gCurrent,float4(sourceUv,0.0f,0.0f));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 outPixel=floor(uv*gSizes.zw);
    float owned=
        PtModulo3Match(outPixel.x,gPhase.x)*
        PtModulo3Match(outPixel.y,gPhase.y);

    float4 directSample=PtLoadOwnedSource(outPixel);

    // current R7/R13 is reconstructed from a raster whose scene samples were
    // displaced by currentJitter.  Sampling the reconstruction at -jitter
    // removes that known displacement for fail-soft pixels while the 9-phase
    // physical reservoir fills.
    float2 fallbackUv=saturate(uv-gPhase.zw/gSizes.xy);
    float4 fallback=tex2Dlod(gFallback,float4(fallbackUv,0.0f,0.0f));

    float4 motionSample=tex2D(gMotion,uv);
    float2 measuredMotion=(motionSample.rg-PTAR_TSR_MOTION_ZERO)*64.0f;
    float motionConfidence=saturate(motionSample.b);

    // The TSR motion field maps current source coordinates to the previous
    // source frame.  Deliberate jitter contributes (jCurrent-jPrevious) to that
    // vector.  Remove it before reprojecting the de-jittered 1080p reservoir.
    float2 knownJitterMotion=gPhase.zw-gPrevious.xy;
    float2 sceneMotion=measuredMotion-knownJitterMotion;
    float sceneMotionLength=length(sceneMotion);
    float2 historyUv=uv+sceneMotion/gSizes.xy;
    float inside=
        step(0.0f,historyUv.x)*step(historyUv.x,1.0f)*
        step(0.0f,historyUv.y)*step(historyUv.y,1.0f);
    float4 history=tex2Dlod(
        gAccumHistory,float4(saturate(historyUv),0.0f,0.0f));

    // Conservative rejection: keep exact static samples, but prefer current
    // de-jittered reconstruction when motion or photometric agreement becomes
    // unreliable.  This prevents a nine-frame reservoir from turning into a
    // nine-frame ghost trail.
    float photoError=abs(PtLuma(history.rgb)-PtLuma(fallback.rgb));
    float photoTrust=saturate(1.0f-photoError/0.075f);
    float moving=saturate((sceneMotionLength-0.035f)*4.5f);
    float motionTrust=lerp(1.0f,motionConfidence,moving);
    float largeMotionReject=1.0f-saturate((sceneMotionLength-0.70f)*1.45f);
    float historyTrust=
        saturate(history.a)*saturate(gPrevious.z)*inside*
        photoTrust*motionTrust*largeMotionReject;

    float3 retained=lerp(fallback.rgb,history.rgb,historyTrust);
    float retainedValid=historyTrust;

    // Owned pixels are authoritative current-frame measurements.  They replace
    // history instead of averaging with it, preserving weave, thin diagonals,
    // foliage and microtexture that R12/R13 tended to smooth.
    float3 outRgb=lerp(retained,directSample.rgb,owned);
    float outValid=max(retainedValid,owned);
    return float4(saturate(outRgb),saturate(outValid));
}
