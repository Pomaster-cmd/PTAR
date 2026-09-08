// PTAR-NG MoE v02 - LAB07 Always-On MC Native Detail
// Direct3D 11 / Shader Model 5.0 / Windows 8.1 target.
//
// LAB07 removes the curvature router after a cost-aware non-leaky A+B
// architecture selection showed always-on MC quality-equivalent to LAB06
// relative routing. Crop C was evaluated only after the architecture choice.
// The selected directional segment is reconstructed directly with a
// shape-preserving monotonized-central (MC) cubic Hermite expert.
//
// Texture contract preserved:
//     1 GatherGreen + 4 SampleLevel
// No UAV. No intermediate texture. Exact x1.5 only.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PTARMoeNGV02Constants : register(b0)
{
    float2 gInputSize;
    float2 gOutputSize;
};

struct PSIn { float4 position : SV_Position; };

float2 TexelCenterUV(float2 texelPos)
{
    return (texelPos+0.5f)/gInputSize;
}

float4 MCSlope(float4 a,float4 b)
{
    // Exact MC limiter in a cheaper algebraic form:
    // minmod((a+b)/2,2a,2b).
    float4 avg=0.5f*(a+b);
    float4 lim=2.0f*min(abs(a),abs(b));
    float4 limited=clamp(avg,-lim,lim);
    return limited*step(0.0f,a*b);
}

float4 Hermite13(float4 f0,float4 f1,float4 m0,float4 m1)
{
    // t=1/3: h00=20/27, h10=4/27, h01=7/27, h11=-2/27.
    float4 h=(20.0f*f0 + 4.0f*m0 + 7.0f*f1 - 2.0f*m1)*(1.0f/27.0f);
    return clamp(h,min(f0,f1),max(f0,f1));
}

float4 Hermite23(float4 f0,float4 f1,float4 m0,float4 m1)
{
    // t=2/3: h00=7/27, h10=2/27, h01=20/27, h11=-4/27.
    float4 h=(7.0f*f0 + 2.0f*m0 + 20.0f*f1 - 4.0f*m1)*(1.0f/27.0f);
    return clamp(h,min(f0,f1),max(f0,f1));
}

float4 main(PSIn input) : SV_Target
{
    uint2 outPix=(uint2)input.position.xy;
    float2 srcPos=float2(outPix)*(2.0f/3.0f);
    float2 srcFloor=floor(srcPos);

    // Same gather orientation contract as validated MoE v01/LAB06.
    float2 gatherUV=(srcFloor+1.0f)/gInputSize;
    float4 g=gSource.GatherGreen(gLinearClamp,gatherUV);

    float tl=g.w;
    float tr=g.z;
    float bl=g.x;
    float br=g.y;
    float gx=(tr+br)-(tl+bl);
    float gy=(bl+br)-(tl+tr);
    bool useX=abs(gx)>=abs(gy);

    float2 axis=useX?float2(1.0f,0.0f):float2(0.0f,1.0f);
    float2 basePos=useX
        ? float2(srcFloor.x,srcPos.y)
        : float2(srcPos.x,srcFloor.y);

    float4 fm1=gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos-axis),0.0f);
    float4 f0 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos),0.0f);
    float4 f1 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+axis),0.0f);
    float4 f2 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+2.0f*axis),0.0f);

    uint phaseIndex=useX?(outPix.x%3u):(outPix.y%3u);
    if(phaseIndex==0u)
        return f0;

    float4 d0=f0-fm1;
    float4 d1=f1-f0;
    float4 d2=f2-f1;
    float4 m0=MCSlope(d0,d1);
    float4 m1=MCSlope(d1,d2);

    return (phaseIndex==1u)
        ? Hermite23(f0,f1,m0,m1)
        : Hermite13(f0,f1,m0,m1);
}
