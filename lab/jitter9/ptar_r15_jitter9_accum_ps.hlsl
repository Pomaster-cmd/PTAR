// PTAR R15 JITTER9 SUPERRESOLVE - exact 3:2 phase accumulator.
//
// s0 = current deliberately-jittered 1280x720 source (POINT)
// s1 = previous private 1920x1080 accumulation (LINEAR for moving reprojection)
// s2 = current R7/R13 1920x1080 fallback (LINEAR, de-jittered here)
// s3 = current->previous source motion/confidence, same encoding as TSR1
// c0 = sourceW,sourceH,outputW,outputH
// c1 = phaseClassX,phaseClassY,currentJitterX,currentJitterY (source pixels)
// c2 = previousJitterX,previousJitterY,historyValid,unused
//
// At exact 3:2 scaling, output classes x%3/y%3 correspond to source-space
// sample fractions {5/6,1/2,1/6}.  The matching frame overwrites that output
// pixel with a DIRECT 720p raster sample. No sharpening is applied to a direct
// sample: after nine static phases every output pixel is a physical scene
// sample at the 1080p target lattice rather than an interpolated estimate.

sampler2D gCurrent : register(s0);
sampler2D gAccumHistory : register(s1);
sampler2D gFallback : register(s2);
sampler2D gMotion : register(s3);
float4 gSizes : register(c0);
float4 gPhase : register(c1);
float4 gPrevious : register(c2);

static const float PTAR_TSR_MOTION_ZERO=0.5019607843137255f;

float PtLuma(float3 c)
{
    return dot(c,float3(0.2126f,0.7152f,0.0722f));
}

float2 PtClampSourceIndex(float2 p)
{
    return clamp(p,float2(0.0f,0.0f),gSizes.xy-1.0f);
}

float4 PtLoadCurrentPoint(float2 sourceIndex)
{
    float2 q=PtClampSourceIndex(sourceIndex);
    return tex2Dlod(gCurrent,float4((q+0.5f)/gSizes.xy,0.0f,0.0f));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 outPixel=floor(uv*gSizes.zw);
    float2 phaseClass=outPixel-floor(outPixel/3.0f)*3.0f;
    float phaseMatchX=1.0f-step(0.25f,abs(phaseClass.x-gPhase.x));
    float phaseMatchY=1.0f-step(0.25f,abs(phaseClass.y-gPhase.y));
    float phaseMatch=phaseMatchX*phaseMatchY;

    // Exact output-pixel centre expressed in source-pixel-centre coordinates.
    float2 p=(outPixel+0.5f)*(gSizes.xy/gSizes.zw)-0.5f;
    float2 nearestSource=floor(p+0.5f);
    float4 directSample=PtLoadCurrentPoint(nearestSource);

    // Current R7/R13 was generated from the jittered frame. Sampling it at a
    // positive current-jitter offset removes the deliberate raster translation
    // before it is used as the fail-soft image while the 9-phase lattice fills.
    float2 fallbackUv=saturate(uv+gPhase.zw/gSizes.xy);
    float4 fallback=tex2Dlod(gFallback,float4(fallbackUv,0.0f,0.0f));

    float4 motionSample=tex2D(gMotion,uv);
    float2 measuredMotion=(motionSample.rg-PTAR_TSR_MOTION_ZERO)*64.0f;
    float motionConfidence=saturate(motionSample.b);

    // Static jitter alone appears to the old motion estimator as prev-current.
    // Remove that known component before reprojecting the already de-jittered
    // 1080p accumulation. What remains is scene/camera motion only.
    float2 knownJitterMotion=gPrevious.xy-gPhase.zw;
    float2 sceneMotion=measuredMotion-knownJitterMotion;
    float sceneMotionLength=length(sceneMotion);
    float2 historyUv=uv+sceneMotion/gSizes.xy;
    float inside=
        step(0.0f,historyUv.x)*step(historyUv.x,1.0f)*
        step(0.0f,historyUv.y)*step(historyUv.y,1.0f);
    float4 history=tex2Dlod(gAccumHistory,float4(saturate(historyUv),0.0f,0.0f));

    // Static pixels keep exact accumulated samples. During real motion, require
    // motion confidence and photometric agreement; otherwise fail immediately
    // to the de-jittered current reconstruction and rebuild after motion stops.
    float photoError=abs(PtLuma(history.rgb)-PtLuma(fallback.rgb));
    float photoTrust=saturate(1.0f-photoError/0.085f);
    float moving=saturate((sceneMotionLength-0.04f)*4.0f);
    float motionTrust=lerp(1.0f,motionConfidence,moving);
    float largeMotionReject=1.0f-saturate((sceneMotionLength-0.80f)*1.25f);
    float historyTrust=
        saturate(history.a)*saturate(gPrevious.z)*inside*
        photoTrust*motionTrust*largeMotionReject;

    float3 retained=lerp(fallback.rgb,history.rgb,historyTrust);
    float retainedValid=historyTrust;

    // The matching lattice phase is authoritative and replaces, rather than
    // averages with, history. This is what prevents chair weave and thin edges
    // from being smoothed away over the 9-frame convergence window.
    float3 outRgb=lerp(retained,directSample.rgb,phaseMatch);
    float outValid=max(retainedValid,phaseMatch);
    return float4(saturate(outRgb),saturate(outValid));
}
