// PTAR-NG MoE v02 - LAB25 Provably Redundant Shape-Clamp Removal
// Direct3D 11 / Shader Model 5.0 / Windows 8.1 target.
//
// Starting point: hardware-selected LAB22 phase-fused MC.
// Purpose: remove the final clamp(h,min(f0,f1),max(f0,f1)) only after proving
// that the MC slope bounds make it unreachable for exact x1.5 phases 1/3,2/3.
//
// For a monotone interval d=f1-f0, the existing MC slope limiter guarantees
// normalized endpoint slopes alpha=m0/d and beta=m1/d are in [0,2].
// Phase t=1/3: (h-f0)/d = (7 + 4*alpha - 2*beta)/27 in [3/27,15/27].
// Phase t=2/3: (h-f0)/d = (20 + 2*alpha - 4*beta)/27 in [12/27,24/27].
// Both are strictly inside [0,1]. For d=0 both slopes are zero and h=f0=f1.
// Therefore the final shape clamp is algebraically redundant under the retained
// MCSlope contract. Admission still requires direct bit-exact WARP equivalence.
//
// Texture contract preserved: 1 GatherGreen + 4 SampleLevel, 0 UAV.

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

    uint phaseIndex=useX?(outPix.x%3u):(outPix.y%3u);
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
