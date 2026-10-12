// PTAR D3D9 FULLQUALITY1 - Q720 SPEC5-B12 production specialization
// This is the exact AALAB1 shader source with PTAR_VARIANT fixed to 2 by
// default, so production DXBC can be required to match the selected B12 lab
// candidate bit-for-bit. Unused experimental branches are removed by FXC
// preprocessing/optimization and are never reachable in production.

#ifndef PTAR_VARIANT
#define PTAR_VARIANT 2
#endif

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

// Intentional fractional linear source sample for experimental AA branches.
// This function is dead-code eliminated for production PTAR_VARIANT=2.
float4 PtSampleLinear(float2 p)
{
    float2 q=clamp(p,float2(-0.5f,-0.5f),gSizes.xy-float2(0.5f,0.5f));
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

float PtAliasRisk(float x,float y,float hp)
{
    float edge=saturate((max(x,y)-0.018f)*8.0f);
    float thin=saturate((hp-0.0015f)*55.0f);
    float anis=abs(x-y)/(x+y+0.01f);
    float diagonalPreference=0.35f+0.65f*(1.0f-anis);
    return saturate(edge*thin*diagonalPreference);
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
    float gxSigned=PtLuma(R.rgb)-PtLuma(L.rgb);
    float gySigned=PtLuma(B.rgb)-PtLuma(T.rgb);
    float x=abs(gxSigned);
    float y=abs(gySigned);
    float hp=abs(PtLuma(c.rgb)-PtLuma(n));

    float detail=saturate((hp-0.0008f)*100.0f)
                *saturate(1.0f-max(x,y)/0.15f)
                *(1.0f-abs(x-y)/(x+y+0.01f));

    float4 spec=float4(
        saturate(c.rgb+(0.40f+0.25f*detail)*(c.rgb-n)),
        c.a);

    float risk=PtAliasRisk(x,y,hp);
    float4 outv=spec;

#if PTAR_VARIANT == 1
    outv=lerp(spec,baseBil,0.08f);
#elif PTAR_VARIANT == 2
    outv=lerp(spec,baseBil,0.12f);
#elif PTAR_VARIANT == 3
    outv=lerp(spec,baseBil,risk*0.20f);
#elif PTAR_VARIANT == 4
    float chooseVertical=step(y,x);
    float4 aaHorizontal=0.5f*(L+R);
    float4 aaVertical=0.5f*(T+B);
    float4 aaTarget=lerp(aaHorizontal,aaVertical,chooseVertical);
    outv=lerp(spec,aaTarget,risk*0.15f);
#elif PTAR_VARIANT == 5 || PTAR_VARIANT == 6 || PTAR_VARIANT == 8
    float2 grad=float2(gxSigned,gySigned);
    float invLen=rsqrt(dot(grad,grad)+1.0e-6f);
    float2 tangent=float2(-grad.y,grad.x)*invLen;
    float2 aaOffset=tangent*0.45f;
    float4 aaTarget=0.5f*(PtSampleLinear(p-aaOffset)+PtSampleLinear(p+aaOffset));
  #if PTAR_VARIANT == 5
    outv=lerp(spec,aaTarget,risk*0.15f);
  #elif PTAR_VARIANT == 6
    outv=lerp(spec,aaTarget,risk*0.15f);
    outv=lerp(outv,baseBil,risk*0.10f);
  #else
    outv=lerp(spec,aaTarget,risk*0.22f);
    outv=lerp(outv,baseBil,risk*0.12f);
  #endif
#elif PTAR_VARIANT == 7
    float2 grad=float2(gxSigned,gySigned);
    float invLen=rsqrt(dot(grad,grad)+1.0e-6f);
    float2 tangent=float2(-grad.y,grad.x)*invLen;
    float2 a=tangent*0.35f;
    float2 b=tangent*0.75f;
    float4 aaTarget=
        0.30f*PtSampleLinear(p-a)+
        0.30f*PtSampleLinear(p+a)+
        0.20f*PtSampleLinear(p-b)+
        0.20f*PtSampleLinear(p+b);
    outv=lerp(spec,aaTarget,risk*0.18f);
    outv=lerp(outv,baseBil,risk*0.10f);
#endif

    return float4(saturate(outv.rgb),outv.a);
}
