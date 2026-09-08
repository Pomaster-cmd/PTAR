// PTAR-NG MoE v02 - LAB05-MC Native Detail
// Direct3D 11 / Shader Model 5.0 / Windows 8.1 target.
//
// This is the first v02 expert that does NOT scale the v01 K185 correction.
// It reconstructs the selected direction with a shape-preserving
// monotonized-central (MC) cubic Hermite segment, then routes from directional
// bilinear using a cheap curvature product gate.
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

float Luma709(float4 c)
{
    return dot(c.rgb,float3(0.2126f,0.7152f,0.0722f));
}

float4 MCSlope(float4 a,float4 b)
{
    // Exact MC limiter in a cheaper algebraic form:
    // minmod((a+b)/2,2a,2b).
    // Opposite signs are masked to zero; when signs agree, clamping the
    // centered slope to +/-2*min(|a|,|b|) is algebraically identical.
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

    // LAB05 selected on A+B crops from BOTH B-GRID and V1_A;
    // crop C remained untouched until after selection.
    float gRel=saturate(relCurv*(1.0f/0.60f));
    float gAbs=saturate(absCurv*(1.0f/0.16f));
    return gRel*gAbs;
}

float4 main(PSIn input) : SV_Target
{
    uint2 outPix=(uint2)input.position.xy;
    float2 srcPos=float2(outPix)*(2.0f/3.0f);
    float2 srcFloor=floor(srcPos);

    // Same gather orientation contract as validated MoE v01.
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

    float phaseFrac=(phaseIndex==1u)?(2.0f/3.0f):(1.0f/3.0f);
    float4 bilinear=lerp(f0,f1,phaseFrac);

    float4 d0=f0-fm1;
    float4 d1=f1-f0;
    float4 d2=f2-f1;
    float4 m0=MCSlope(d0,d1);
    float4 m1=MCSlope(d1,d2);

    float4 mc=(phaseIndex==1u)
        ? Hermite23(f0,f1,m0,m1)
        : Hermite13(f0,f1,m0,m1);

    float detailGate=NativeDetailGate(fm1,f0,f1,f2);
    return lerp(bilinear,mc,detailGate);
}
