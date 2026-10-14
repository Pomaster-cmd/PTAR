// PTAR R15 TRUE720 JITTER9 - exact 1080p sample reservoir
//
// s0 = current REAL 1280x720 frame rendered with deliberate projection jitter
// s1 = previous private 1920x1080 reservoir (A = validity)
// c0 = (sourceW,sourceH,outputW,outputH)
// c1 = (jitterX,jitterY,phaseClassX,phaseClassY)
//
// For exact 3:2 scaling, one parity subset of a deliberately jittered 720p
// frame lands exactly on one output residue class modulo three. The 3x3
// Cartesian phase cycle therefore owns every 1080p pixel exactly once.
// No interpolation is used for an owned sample.
//
// Source addressing is derived from the output residue class rather than from
// floating-point inversion of the jitter. This makes ownership byte-exact and
// leaves the raster-jitter convention free to be calibrated independently.

sampler2D gCurrent : register(s0);
sampler2D gPrevious : register(s1);
float4 gSizes : register(c0);
float4 gPhase : register(c1);

float PtModulo3Match(float p,float phaseClass)
{
    float r=frac((p-phaseClass)/3.0f);
    float d=min(r,1.0f-r);
    return 1.0f-step(0.0005f,d);
}

float PtSourceOffsetForResidue(float phaseClass)
{
    // phase 0: p=3k   -> s=2k     -> offset  0
    // phase 1: p=3k+1 -> s=2k+1   -> offset +1
    // phase 2: p=3k+2 -> s=2k+1   -> offset -1
    return 2.5f*phaseClass-1.5f*phaseClass*phaseClass;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 outPixel=floor(uv*gSizes.zw);

    float matchX=PtModulo3Match(outPixel.x,gPhase.z);
    float matchY=PtModulo3Match(outPixel.y,gPhase.w);
    float owned=matchX*matchY;

    float2 phaseOffset=float2(
        PtSourceOffsetForResidue(gPhase.z),
        PtSourceOffsetForResidue(gPhase.w));
    float2 sourceIndex=(2.0f*outPixel+phaseOffset)/3.0f;
    float2 sourceUv=(sourceIndex+0.5f)/gSizes.xy;

    // POINT/LOD0 is mandatory: an owned output pixel is a measured source
    // sample, not a reconstruction from neighbouring source texels.
    float4 measured=tex2Dlod(gCurrent,float4(sourceUv,0.0f,0.0f));
    float4 previous=tex2Dlod(gPrevious,float4(uv,0.0f,0.0f));

    float3 rgb=lerp(previous.rgb,measured.rgb,owned);
    float validity=max(previous.a,owned);
    return float4(rgb,validity);
}
