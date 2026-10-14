// PTAR R14 MFSR1 RESTORE1080
// Cheap full-resolution perceptual restoration after multi-frame accumulation.
// The pass never expands beyond the local cross extrema, so useful microcontrast
// can be restored without classic unsharp-mask halos around silhouettes/text.
// s0 = private 1080p accumulation, alpha = accumulation confidence
// c0.xy = output dimensions, c0.zw = reciprocal output dimensions

sampler2D gAccum : register(s0);
float4 gOutputInfo : register(c0);

float PtLuma(float3 c)
{
    return dot(c,float3(0.2126f,0.7152f,0.0722f));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 d=gOutputInfo.zw;
    float4 c=tex2Dlod(gAccum,float4(uv,0.0f,0.0f));
    float3 l=tex2Dlod(gAccum,float4(uv-float2(d.x,0.0f),0.0f,0.0f)).rgb;
    float3 r=tex2Dlod(gAccum,float4(uv+float2(d.x,0.0f),0.0f,0.0f)).rgb;
    float3 u=tex2Dlod(gAccum,float4(uv-float2(0.0f,d.y),0.0f,0.0f)).rgb;
    float3 v=tex2Dlod(gAccum,float4(uv+float2(0.0f,d.y),0.0f,0.0f)).rgb;

    float3 localLo=min(c.rgb,min(min(l,r),min(u,v)));
    float3 localHi=max(c.rgb,max(max(l,r),max(u,v)));
    float3 crossMean=0.25f*(l+r+u+v);
    float3 detail=c.rgb-crossMean;

    float lc=PtLuma(c.rgb);
    float ll=PtLuma(l);
    float lr=PtLuma(r);
    float lu=PtLuma(u);
    float lv=PtLuma(v);
    float lmin=min(lc,min(min(ll,lr),min(lu,lv)));
    float lmax=max(lc,max(max(ll,lr),max(lu,lv)));
    float contrast=lmax-lmin;

    // Fine/medium contrast receives the strongest restoration. Strong edges are
    // deliberately protected because they are where ringing is most visible.
    float strongEdge=saturate((contrast-0.12f)*4.5f);
    float edgeGuard=lerp(1.0f,0.22f,strongEdge);
    float confidence=saturate(c.a);
    float gain=lerp(0.15f,0.42f,confidence)*edgeGuard;

    float3 restored=c.rgb+detail*gain;
    restored=clamp(restored,localLo,localHi);
    return float4(saturate(restored),1.0f);
}
