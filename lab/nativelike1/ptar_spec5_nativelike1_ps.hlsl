// PTAR D3D9 SPEC5 NATIVELIKE1 experimental shader
// Goal: maximize resemblance to native 1080p, not alias suppression.
// Candidate selected from exact-alignment real-scene search:
//   base gain 0.53, detail gain 0.125, central bilinear blend 0.045.
// This is LAB ONLY and is not wired into the runtime.

sampler2D gSource : register(s0);
float4 gSizes : register(c0);

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

float4 PtU(float2 p,out float4 bilinearBase)
{
    const float sharp=2.88f;
    float2 q=floor(p);
    float2 f=p-q;

    float4 c=PtLoad(q);
    float4 d=PtLoad(q+float2(1,0));
    float4 e=PtLoad(q+float2(0,1));
    float4 h=PtLoad(q+float2(1,1));

    float4 x=lerp(c,d,f.x);
    float4 y=lerp(e,h,f.x);
    float4 b=lerp(x,y,f.y);
    bilinearBase=b;

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
    float2 p=uv*gSizes.xy-0.5f;
    float2 o=2.0f*gSizes.xy/gSizes.zw;

    float4 baseBil;
    float4 dummyR,dummyL,dummyB,dummyT;
    float4 c=PtU(p,baseBil);
    float4 R=PtU(p+float2(o.x,0),dummyR);
    float4 L=PtU(p-float2(o.x,0),dummyL);
    float4 B=PtU(p+float2(0,o.y),dummyB);
    float4 T=PtU(p-float2(0,o.y),dummyT);

    float3 n=(R.rgb+L.rgb+B.rgb+T.rgb)*0.25f;
    float x=abs(PtLuma(R.rgb)-PtLuma(L.rgb));
    float y=abs(PtLuma(B.rgb)-PtLuma(T.rgb));
    float hp=abs(PtLuma(c.rgb)-PtLuma(n));

    float detail=saturate((hp-0.0008f)*100.0f)
                *saturate(1.0f-max(x,y)/0.15f)
                *(1.0f-abs(x-y)/(x+y+0.01f));

    float4 spec=float4(
        saturate(c.rgb+(0.53f+0.125f*detail)*(c.rgb-n)),
        c.a);

    float4 outv=lerp(spec,baseBil,0.045f);
    return float4(saturate(outv.rgb),outv.a);
}
