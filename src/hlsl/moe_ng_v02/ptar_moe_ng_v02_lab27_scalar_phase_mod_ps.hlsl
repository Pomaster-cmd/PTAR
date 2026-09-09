// PTAR-NG MoE v02 - LAB27 Scalar Phase Modulo
// Direct3D 11 / Shader Model 5.0 / Windows 8.1 target.
//
// Starting point: hardware-selected LAB25.
// Purpose: preserve LAB25 output exactly while avoiding a vector modulo-by-3
// on both output coordinates. LAB25 source expresses the phase as a ternary
// between (x % 3) and (y % 3); FXC emits one udiv operating on .xy and then
// selects the relevant remainder. LAB27 selects the scalar coordinate first,
// then computes a single scalar modulo-by-3.
//
// Texture contract unchanged: 1 GatherGreen + 4 SampleLevel worst case, 0 UAV.
// This is an ALU/phase-address optimization only; hardware gain is not assumed.

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
    float4 avg=0.5f*(a+b);
    float4 lim=2.0f*min(abs(a),abs(b));
    float4 limited=clamp(avg,-lim,lim);
    return limited*step(0.0f,a*b);
}

float4 main(PSIn input) : SV_Target
{
    uint2 outPix=(uint2)input.position.xy;
    float2 srcPos=float2(outPix)*(2.0f/3.0f);
    float2 srcFloor=floor(srcPos);

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

    uint phaseCoord=useX?outPix.x:outPix.y;
    uint phaseIndex=phaseCoord%3u;
    if(phaseIndex==0u)
        return f0;

    float4 d0=f0-fm1;
    float4 d1=f1-f0;
    float4 d2=f2-f1;
    float4 m0=MCSlope(d0,d1);
    float4 m1=MCSlope(d1,d2);

    float4 coeff=(phaseIndex==1u)
        ? float4(7.0f,2.0f,20.0f,-4.0f)
        : float4(20.0f,4.0f,7.0f,-2.0f);

    return (coeff.x*f0 + coeff.y*m0 + coeff.z*f1 + coeff.w*m1)*(1.0f/27.0f);
}
