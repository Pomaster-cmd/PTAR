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
    float confidence=saturate(motionSample.b);

    float2 halfMotion=0.5*motion*gOutput.zw;

    float4 p0=tex2D(gPrev,uv);
    float4 c0=tex2D(gCurr,uv);
    float4 realBlend=0.5*(p0+c0);

    // Symmetric midpoint warp. Unlike the D3D11/NVENC production path, the
    // D3D9 backend currently derives motion in shader space; therefore motion
    // confidence must be allowed to reject an unstable vector completely.
    float4 a=tex2D(gPrev,uv+halfMotion);
    float4 b=tex2D(gCurr,uv-halfMotion);

    float warpedDiff=abs(Luma(a.rgb)-Luma(b.rgb));
    float endpointDiff=abs(Luma(p0.rgb)-Luma(c0.rgb));
    float photoTrust=saturate(1.0-warpedDiff/0.24);

    // PRODPORT1: preserve the production fail-soft principle, but do not copy
    // TDETAIL3's 80% trust floor from the NVENC-ME backend. That floor assumes
    // validated NVENC vectors and made noisy D3D9 shader vectors dominate even
    // at confidence=0. Confidence is squared to reject ambiguous matches.
    float vectorTrust=confidence*confidence;
    float temporalTrust=saturate(1.0-endpointDiff/0.55);
    float trust=photoTrust*vectorTrust*temporalTrust;

    float4 warpedBlend=0.5*(a+b);
    float4 generated=lerp(realBlend,warpedBlend,trust);

    // Production-derived FUSEDDETAIL fail-soft clamp. Estimate a compact local
    // endpoint range in both axes and pull only excessive generated excursions
    // back toward the unwarped REAL midpoint. REAL frames remain untouched.
    float lp=Luma(p0.rgb);
    float lc=Luma(c0.rgb);
    float lpX=Luma(tex2D(gPrev,uv+float2(gOutput.z,0)).rgb);
    float lpY=Luma(tex2D(gPrev,uv+float2(0,gOutput.w)).rgb);
    float lcX=Luma(tex2D(gCurr,uv+float2(gOutput.z,0)).rgb);
    float lcY=Luma(tex2D(gCurr,uv+float2(0,gOutput.w)).rgb);

    float localRange=max(
        max(abs(lpX-lp),abs(lpY-lp)),
        max(abs(lcX-lc),abs(lcY-lc)));

    float deviation=abs(Luma(generated.rgb)-Luma(realBlend.rgb));
    float budget=localRange*0.70+0.008;
    float clampTrust=saturate(budget/(deviation+0.0001));

    generated=lerp(realBlend,generated,0.15+0.85*clampTrust);
    generated.a=realBlend.a;
    return generated;
}
