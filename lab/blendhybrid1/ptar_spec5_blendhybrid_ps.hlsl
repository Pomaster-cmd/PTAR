// Experimental SPEC5 hybrid blend laboratory shader.
// No extra texture taps: reuses SPEC5's central bilinear and existing R/L/B/T.
// Compile with PTAR_HYBRID_VARIANT:
// 0 BASE, 1 B12, 2 B18, 3 B20,
// 4 H08_A08 (8..16%), 5 H10_A08 (10..18%),
// 6 H12_A06 (12..18%), 7 H12_A08 (12..20%).

#ifndef PTAR_HYBRID_VARIANT
#define PTAR_HYBRID_VARIANT 0
#endif

sampler2D gSource : register(s0);
float4 gSizes : register(c0);

float PtLuma(float3 c){return dot(c,float3(0.299f,0.587f,0.114f));}
float2 PtClampIndex(float2 p){return clamp(p,float2(0,0),gSizes.xy-1.0f);}
float4 PtLoad(float2 p){float2 q=PtClampIndex(p);return tex2Dlod(gSource,float4((q+0.5f)/gSizes.xy,0,0));}

float4 PtU(float2 p,out float4 bilinearBase)
{
    const float sharp=2.88f;
    float2 q=floor(p),f=p-q;
    float4 c=PtLoad(q),d=PtLoad(q+float2(1,0)),e=PtLoad(q+float2(0,1)),h=PtLoad(q+float2(1,1));
    float4 x=lerp(c,d,f.x),y=lerp(e,h,f.x),b=lerp(x,y,f.y);bilinearBase=b;
    float E=saturate((abs(PtLuma(d.rgb)-PtLuma(c.rgb))+abs(PtLuma(h.rgb)-PtLuma(e.rgb))+abs(PtLuma(e.rgb)-PtLuma(c.rgb))+abs(PtLuma(h.rgb)-PtLuma(d.rgb))*1.0f)*1.6f);
    float3 lo=min(min(c.rgb,d.rgb),min(e.rgb,h.rgb)),hi=max(max(c.rgb,d.rgb),max(e.rgb,h.rgb));
    float2 s=f*f*(3.0f-2.0f*f);float3 r=lerp(lerp(c.rgb,d.rgb,s.x),lerp(e.rgb,h.rgb,s.x),s.y);
    float g=saturate((sharp-1.0f)*(0.35f+0.75f*E))*1.15f;
    return float4(clamp(b.rgb+(r-b.rgb)*g,lo,hi),b.a);
}

float PtAliasRisk(float x,float y,float hp)
{
    float edge=saturate((max(x,y)-0.018f)*8.0f);
    float thin=saturate((hp-0.0015f)*55.0f);
    float anis=abs(x-y)/(x+y+0.01f);
    float diagonalPreference=0.35f+0.65f*(1.0f-anis);
    return saturate(edge*thin*diagonalPreference);
}

float4 main(float2 uv:TEXCOORD0):COLOR0
{
    float2 p=uv*gSizes.xy-0.5f,o=2.0f*gSizes.xy/gSizes.zw;
    float4 baseBil,j0,j1,j2,j3;
    float4 c=PtU(p,baseBil),R=PtU(p+float2(o.x,0),j0),L=PtU(p-float2(o.x,0),j1),B=PtU(p+float2(0,o.y),j2),T=PtU(p-float2(0,o.y),j3);
    float3 n=(R.rgb+L.rgb+B.rgb+T.rgb)*0.25f;
    float x=abs(PtLuma(R.rgb)-PtLuma(L.rgb)),y=abs(PtLuma(B.rgb)-PtLuma(T.rgb)),hp=abs(PtLuma(c.rgb)-PtLuma(n));
    float detail=saturate((hp-0.0008f)*100.0f)*saturate(1.0f-max(x,y)/0.15f)*(1.0f-abs(x-y)/(x+y+0.01f));
    float4 spec=float4(saturate(c.rgb+(0.40f+0.25f*detail)*(c.rgb-n)),c.a);
    float risk=PtAliasRisk(x,y,hp);
    float w=0.0f;
#if PTAR_HYBRID_VARIANT == 1
    w=0.12f;
#elif PTAR_HYBRID_VARIANT == 2
    w=0.18f;
#elif PTAR_HYBRID_VARIANT == 3
    w=0.20f;
#elif PTAR_HYBRID_VARIANT == 4
    w=0.08f+0.08f*risk;
#elif PTAR_HYBRID_VARIANT == 5
    w=0.10f+0.08f*risk;
#elif PTAR_HYBRID_VARIANT == 6
    w=0.12f+0.06f*risk;
#elif PTAR_HYBRID_VARIANT == 7
    w=0.12f+0.08f*risk;
#endif
    return float4(saturate(lerp(spec,baseBil,w).rgb),spec.a);
}
