// PTAR D3D9 FULLQUALITY1 - HQ900 specialization
// Exact low/sharp<=1 branch of the adaptive D3D11/Inquisitor source.
// Fixed profile: Sharp=0.96 for nonexact 1.0<scale<=1.5 geometry.
// Specialization removes the unused SPEC5 branch on SM3.

sampler2D gSource : register(s0);
float4 gSizes : register(c0); // xy=source size, zw=fitted destination size

float PtLuma(float3 c)
{
    return dot(c,float3(0.299f,0.587f,0.114f));
}

float2 PtClampIndex(float2 p)
{
    return clamp(p,float2(0,0),gSizes.xy-1.0f);
}

float4 PtLoad(float2 p)
{
    float2 q=PtClampIndex(p);
    return tex2Dlod(gSource,float4((q+0.5f)/gSizes.xy,0,0));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    const float sharp=0.96f;
    float2 p=uv*gSizes.xy-0.5f;
    float2 q=floor(p);
    float2 f=p-q;

    float4 c=PtLoad(q);
    float4 d=PtLoad(q+float2(1,0));
    float4 e=PtLoad(q+float2(0,1));
    float4 h=PtLoad(q+float2(1,1));

    float4 x=lerp(c,d,f.x);
    float4 y=lerp(e,h,f.x);
    float4 b=lerp(x,y,f.y);

    float E=saturate((
        abs(PtLuma(d.rgb)-PtLuma(c.rgb))+
        abs(PtLuma(h.rgb)-PtLuma(e.rgb))+
        abs(PtLuma(e.rgb)-PtLuma(c.rgb))+
        abs(PtLuma(h.rgb)-PtLuma(d.rgb)))*1.6f);

    float3 lo=min(min(c.rgb,d.rgb),min(e.rgb,h.rgb));
    float3 hi=max(max(c.rgb,d.rgb),max(e.rgb,h.rgb));
    float2 nearest=PtClampIndex(floor(p+0.5f));
    float3 N=PtLoad(nearest).rgb;

    return float4(
        clamp(b.rgb+(N-b.rgb)*(sharp*(0.12f+0.38f*E)),lo,hi),
        b.a);
}
