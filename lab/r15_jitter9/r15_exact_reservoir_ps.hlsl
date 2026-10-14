// PTAR R15 TRUE720 JITTER9 - exact 1080p sample reservoir
//
// s0 = current REAL 1280x720 frame rendered with deliberate projection jitter
// s1 = previous private 1920x1080 reservoir (A = validity)
// c0 = (sourceW,sourceH,outputW,outputH)
// c1 = (jitterX,jitterY,phaseClassX,phaseClassY)
//
// For exact 3:2 scaling, one parity subset of a deliberately jittered 720p
// frame lands exactly on one output residue class modulo three.  The 3x3
// Cartesian phase cycle therefore owns every 1080p pixel exactly once.
// No interpolation is used for an owned sample.

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

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 outPixel=floor(uv*gSizes.zw);
    float2 outCenter=outPixel+0.5f;

    float matchX=PtModulo3Match(outPixel.x,gPhase.z);
    float matchY=PtModulo3Match(outPixel.y,gPhase.w);
    float owned=matchX*matchY;

    // Invert the exact 3:2 pixel-center mapping:
    // (pOut+0.5)/1920 = (pSrc+0.5+jitter)/1280
    float2 sourceIndex=outCenter*(gSizes.xy/gSizes.zw)-0.5f-gPhase.xy;
    float2 sourceUv=(sourceIndex+0.5f)/gSizes.xy;

    // POINT/LOD0 is mandatory: an owned output pixel is a measured source
    // sample, not a reconstruction from neighbouring source texels.
    float4 measured=tex2Dlod(gCurrent,float4(sourceUv,0.0f,0.0f));
    float4 previous=tex2Dlod(gPrevious,float4(uv,0.0f,0.0f));

    float3 rgb=lerp(previous.rgb,measured.rgb,owned);
    float validity=max(previous.a,owned);
    return float4(rgb,validity);
}
