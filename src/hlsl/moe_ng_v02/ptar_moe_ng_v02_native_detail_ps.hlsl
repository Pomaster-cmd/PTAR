// PTAR-NG MoE v02 Native Detail Recovery - LAB02-CHEAP-PRODUCT
// Direct3D 11 / Shader Model 5.0 / Windows 8.1 target.
//
// Architectural constraint preserved from PTAR-NG MoE v01:
//     1 GatherGreen + 4 SampleLevel
// No extra texture fetch, no UAV, no intermediate texture.
//
// v02 keeps the complete v01 MoE and adds a bounded detail gate that suppresses
// v01 on smooth low-curvature signal while retaining it on genuine local structure.
// Gate inputs are derived only from the four color samples already fetched by v01.
//
// LAB02-CHEAP-PRODUCT selection from the 42-case B-GRID sweep:
//   relative curvature  : 0 -> 0.90
//   absolute curvature  : 0.01 -> 0.24
//   fusion              : gRel * gAbs
// This removes the earlier sqrt/fractional-power path while retaining >=97.5%
// of the v01 structural gain on the tuning corpus.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PTARMoeNGV02Constants : register(b0)
{
    float2 gInputSize;
    float2 gOutputSize;
};

struct PSIn { float4 position : SV_Position; };

float4 K185_13(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    return
        (-0.274074074074074f)*fm1 +
        ( 0.877777777777778f)*f0  +
        ( 0.533333333333333f)*f1  +
        (-0.137037037037037f)*f2;
}

float4 K185_23(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    return
        (-0.137037037037037f)*fm1 +
        ( 0.533333333333333f)*f0  +
        ( 0.877777777777778f)*f1  +
        (-0.274074074074074f)*f2;
}

float2 TexelCenterUV(float2 texelPos)
{
    return (texelPos+0.5f)/gInputSize;
}

float Luma709(float4 c)
{
    return dot(c.rgb,float3(0.2126f,0.7152f,0.0722f));
}

float NativeDetailGate(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    float ym1=Luma709(fm1);
    float y0 =Luma709(f0);
    float y1 =Luma709(f1);
    float y2 =Luma709(f2);

    float c0=abs(ym1-2.0f*y0+y1);
    float c1=abs(y0-2.0f*y1+y2);
    float absCurv=max(c0,c1);

    float localSlope=max(abs(y0-ym1),max(abs(y1-y0),abs(y2-y1)));
    float relCurv=absCurv/(localSlope+1.0e-6f);

    // Cheap product gate selected by the product-only Pareto sweep.
    float gRel=saturate(relCurv*(1.0f/0.90f));
    float gAbs=saturate((absCurv-0.01f)*(1.0f/(0.24f-0.01f)));
    return gRel*gAbs;
}

float4 main(PSIn input) : SV_Target
{
    uint2 outPix=(uint2)input.position.xy;
    float2 srcPos=float2(outPix)*(2.0f/3.0f);
    float2 srcFloor=floor(srcPos);

    // Same validated gather orientation contract as MoE v01.
    float2 gatherUV=(srcFloor+1.0f)/gInputSize;
    float4 g=gSource.GatherGreen(gLinearClamp,gatherUV);

    float tl=g.w;
    float tr=g.z;
    float bl=g.x;
    float br=g.y;

    float gx=(tr+br)-(tl+bl);
    float gy=(bl+br)-(tl+tr);
    float agx=abs(gx);
    float agy=abs(gy);
    bool useX=agx>=agy;

    float2 axis=useX?float2(1.0f,0.0f):float2(0.0f,1.0f);
    float2 basePos=useX
        ? float2(srcFloor.x,srcPos.y)
        : float2(srcPos.x,srcFloor.y);

    // Texture footprint remains exactly four color samples.
    float4 fm1=gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos-axis),0.0f);
    float4 f0 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos),0.0f);
    float4 f1 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+axis),0.0f);
    float4 f2 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+2.0f*axis),0.0f);

    uint phaseIndex=useX?(outPix.x%3u):(outPix.y%3u);
    if(phaseIndex==0u)
        return f0;

    float4 edge=(phaseIndex==2u)
        ? K185_13(fm1,f0,f1,f2)
        : K185_23(fm1,f0,f1,f2);

    float4 raster=clamp(edge,min(f0,f1),max(f0,f1));

    float phaseFrac=(phaseIndex==1u)?(2.0f/3.0f):(1.0f/3.0f);
    float4 bilinear=lerp(f0,f1,phaseFrac);
    float4 natural=lerp(bilinear,raster,0.70f);

    // ROUTER-NG v01 R1 is intentionally unchanged.
    float localRange=max(max(tl,tr),max(bl,br))-min(min(tl,tr),min(bl,br));
    float gradient=0.5f*max(agx,agy);
    float diagonal=0.5f*abs((tl+br)-(tr+bl));
    float coherence=abs(agx-agy)/(agx+agy+1.0e-6f);

    float rangeConf=saturate((localRange-0.01f)/0.10f);
    float coherenceConf=saturate((coherence-0.25f)/0.45f);
    float diagRatio=diagonal/(gradient+1.0e-6f);
    float axisConf=1.0f-saturate((diagRatio-0.08f)/0.45f);
    float rasterWeight=rangeConf*coherenceConf*axisConf;

    float edgeBase=saturate((gradient-0.02f)/0.10f);
    float edgeWeight=(1.0f-rasterWeight)*edgeBase;
    float naturalWeight=1.0f-rasterWeight-edgeWeight;

    float4 moeV01=natural*naturalWeight + edge*edgeWeight + raster*rasterWeight;

    // Native Detail Recovery: on smooth signal fall back toward the directional
    // bilinear path; on genuine local structure recover the full v01 MoE response.
    float detailGate=NativeDetailGate(fm1,f0,f1,f2);
    return lerp(bilinear,moeV01,detailGate);
}
