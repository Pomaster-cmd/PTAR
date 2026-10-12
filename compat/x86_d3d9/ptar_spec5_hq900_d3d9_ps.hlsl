// PTAR D3D9 FULLQUALITY1
// Shader Model 3 port of the exact adaptive HLSL embedded in the validated
// D3D11/Inquisitor runtime.
//
// c0.xy = source size, c0.zw = fitted destination size
// c1.x  = Sharp (2.88 => Q720 SPEC5 NativeFit40+Detail25,
//                 0.96 => HQ900 nonexact path)
//
// D3D11 source invariants:
//   SPEC5: 20 texture loads, base NativeFit gain 0.40, selective +0.25 detail.
//   HQ900: same generic source, Sharp 0.96 low branch.
// No new pass, UAV, resource binding or synchronization.

sampler2D gSource : register(s0);
float4 gSizes : register(c0);
float4 gQuality : register(c1);

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

// Exact semantic port of the embedded D3D11 U(p) helper. Texture2D.Load is
// reproduced by PtLoad at exact texel centres; bilinear is performed manually.
float4 PtU(float2 p,float sharp)
{
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
    float2 s=f*f*(3.0f-2.0f*f);
    float3 r=lerp(lerp(c.rgb,d.rgb,s.x),lerp(e.rgb,h.rgb,s.x),s.y);
    float g=saturate((sharp-1.0f)*(0.35f+0.75f*E))*1.15f;

    return float4(clamp(b.rgb+(r-b.rgb)*g,lo,hi),b.a);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float sharp=gQuality.x;

    // D3D11 SV_Position mapping:
    //   p=(v.xy-DstOrigin)*SrcSize/DstSize-.5
    // The D3D9 quad already maps local fitted output [0,1] to this draw rect,
    // so uv*SrcSize is the equivalent local pixel-centre transform.
    float2 p=uv*gSizes.xy-0.5f;

    if(sharp>1.0f)
    {
        // Q720 SPEC5 / SPEC4-compatible branch. Exactly 5 calls to PtU =>
        // 20 source loads, matching the D3D11 source topology.
        float2 o=2.0f*gSizes.xy/gSizes.zw;
        float4 c=PtU(p,sharp);
        float4 R=PtU(p+float2(o.x,0),sharp);
        float4 L=PtU(p-float2(o.x,0),sharp);
        float4 B=PtU(p+float2(0,o.y),sharp);
        float4 T=PtU(p-float2(0,o.y),sharp);

        float3 n=(R.rgb+L.rgb+B.rgb+T.rgb)*0.25f;
        float x=abs(PtLuma(R.rgb)-PtLuma(L.rgb));
        float y=abs(PtLuma(B.rgb)-PtLuma(T.rgb));
        float hp=abs(PtLuma(c.rgb)-PtLuma(n));

        // SPEC5 Detail25 selector: low/mid-contrast isotropic texture only.
        float detail=saturate((hp-0.0008f)*100.0f)
                    *saturate(1.0f-max(x,y)/0.15f)
                    *(1.0f-abs(x-y)/(x+y+0.01f));

        return float4(
            saturate(c.rgb+(0.40f+0.25f*detail)*(c.rgb-n)),
            c.a);
    }

    // HQ900 Sharp0.96 / generic adaptive branch, textually equivalent to the
    // embedded D3D11 source.
    float2 q=floor(p);
    float2 f=p-q;
    float4 c=PtLoad(q);
    float4 d=PtLoad(q+float2(1,0));
    float4 e=PtLoad(q+float2(0,1));
    float4 h=PtLoad(q+float2(1,1));
    float4 xx=lerp(c,d,f.x);
    float4 yy=lerp(e,h,f.x);
    float4 b=lerp(xx,yy,f.y);

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
