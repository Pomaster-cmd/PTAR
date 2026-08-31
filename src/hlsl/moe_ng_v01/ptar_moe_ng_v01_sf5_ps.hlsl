// PTAR-NG MoE v01 SF5
// NEW NG code. NOT historical MoE v07 / NATURAL v02 / RASTER v03.
//
// Direct3D 11 / Shader Model 5.0 / Windows 8.1 target.
// Shared texture path for the COMPLETE MoE:
//     1 GatherGreen + 4 SampleLevel
//
// Experts all reuse the same samples:
// EDGE    = EDGE-NG v03 K185
// RASTER  = RASTER-NG v01 MCLAMP
// NATURAL = NATURAL-NG v01 N70
// Router  = ROUTER-NG v01 R1
//
// No NIS. No UAV. No intermediate texture.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PTARMoeNGV01Constants : register(b0)
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

float4 main(PSIn input) : SV_Target
{
    uint2 outPix=(uint2)input.position.xy;
    float2 srcPos=float2(outPix)*(2.0f/3.0f);
    float2 srcFloor=floor(srcPos);

    // gather4 ordering used by the validated K185 path:
    // x=lower-left, y=lower-right, z=upper-right, w=upper-left.
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

    // Complete MoE color footprint: exactly four color samples.
    float4 fm1=gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos-axis),0.0f);
    float4 f0 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos),0.0f);
    float4 f1 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+axis),0.0f);
    float4 f2 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+2.0f*axis),0.0f);

    uint phaseIndex=useX?(outPix.x%3u):(outPix.y%3u);
    if(phaseIndex==0u)
        return f0;

    // EDGE expert: hardware-validated K185.
    float4 edge=(phaseIndex==2u)
        ? K185_13(fm1,f0,f1,f2)
        : K185_23(fm1,f0,f1,f2);

    // RASTER expert: monotone K185 suppresses cubic ringing at hard steps.
    float4 raster=clamp(edge,min(f0,f1),max(f0,f1));

    // Full bilinear result from the same directional samples.
    float phaseFrac=(phaseIndex==1u)?(2.0f/3.0f):(1.0f/3.0f);
    float4 bilinear=lerp(f0,f1,phaseFrac);

    // NATURAL expert: conservative blend; no additional texture fetch.
    float4 natural=lerp(bilinear,raster,0.70f);

    // ROUTER-NG v01 R1 soft convex weights. All metrics come from the gather
    // already used for K185 orientation, so routing adds ALU only.
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

    return natural*naturalWeight + edge*edgeWeight + raster*rasterWeight;
}
