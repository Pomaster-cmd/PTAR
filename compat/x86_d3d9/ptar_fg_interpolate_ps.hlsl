sampler2D gPrev   : register(s0);
sampler2D gCurr   : register(s1);
sampler2D gMotion : register(s2);

// c0 = outputWidth, outputHeight, invOutputWidth, invOutputHeight
float4 gOutput : register(c0);

float Luma(float3 c)
{
    return dot(c,float3(0.2126,0.7152,0.0722));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 motionSample=tex2D(gMotion,uv);
    float2 motion=(motionSample.rg-0.5)*64.0;
    float confidence=motionSample.b;

    float2 halfMotion=0.5*motion*gOutput.zw;

    // Motion stores PREV-CURR displacement. At the midpoint we warp the
    // previous frame forward and the current frame backward symmetrically.
    float4 a=tex2D(gPrev,uv+halfMotion);
    float4 b=tex2D(gCurr,uv-halfMotion);

    float4 p0=tex2D(gPrev,uv);
    float4 c0=tex2D(gCurr,uv);
    float4 realBlend=0.5*(p0+c0);

    float warpedDiff=abs(Luma(a.rgb)-Luma(b.rgb));
    float photoTrust=saturate(1.0-warpedDiff/0.35);

    // TDETAIL3-style trust compression: never allow motion confidence to
    // suppress the warped contribution below 80% of its photometric trust.
    float motionTrust=0.8+0.2*confidence;
    float trust=photoTrust*motionTrust;

    float4 generated=lerp(realBlend,0.5*(a+b),trust);

    // FUSEDDETAIL-style local stabilizer: estimate a small endpoint detail
    // budget and only move GENERATED back toward the unwarped real blend.
    float lp0=Luma(p0.rgb);
    float lp1=Luma(tex2D(gPrev,uv+float2(gOutput.z,0)).rgb);
    float lc0=Luma(c0.rgb);
    float lc1=Luma(tex2D(gCurr,uv+float2(gOutput.z,0)).rgb);

    float localRange=max(abs(lp1-lp0),abs(lc1-lc0));
    float deviation=abs(Luma(generated.rgb)-Luma(realBlend.rgb));
    float z=min(1.0,(localRange*0.76+0.01)/(deviation+0.0001));
    generated=lerp(realBlend,generated,0.30+0.70*z);
    generated.a=realBlend.a;
    return generated;
}
