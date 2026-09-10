// PTAR-NG MoE v02 - LAB30B fractional phase decode
// Baseline: hardware-selected LAB29. Exact x1.5 only.
// Experiment: derive the 0/1/2 output phase from the already-computed srcPos
// fractional coordinate, removing integer modulo/udiv from the phase path.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);
cbuffer PTARMoeNGV02Constants : register(b0) { float2 gInputSize; float2 gOutputSize; };
struct PSIn { float4 position : SV_Position; };

float4 MCSlope2(float4 a,float4 b)
{
    float4 sum=a+b;
    float4 lim=4.0f*min(abs(a),abs(b));
    float4 limited=clamp(sum,-lim,lim);
    return limited*step(0.0f,a*b);
}

float4 main(PSIn input) : SV_Target
{
    uint2 outPix=(uint2)input.position.xy;
    float2 srcPos=float2(outPix)*(2.0f/3.0f);
    float2 srcFloor=floor(srcPos);
    float2 gatherUV=(srcFloor+1.0f)/gInputSize;
    float4 g=gSource.GatherGreen(gLinearClamp,gatherUV);
    float tl=g.w,tr=g.z,bl=g.x,br=g.y;
    float gx=(tr+br)-(tl+bl);
    float gy=(bl+br)-(tl+tr);
    bool useX=abs(gx)>=abs(gy);
    float2 basePos=useX?float2(srcFloor.x,srcPos.y):float2(srcPos.x,srcFloor.y);
    float2 axis=useX?float2(1.0f,0.0f):float2(0.0f,1.0f);
    float2 baseUV=(basePos+0.5f)/gInputSize;
    float2 axisUV=axis/gInputSize;
    float4 fm1=gSource.SampleLevel(gLinearClamp,baseUV-axisUV,0.0f);
    float4 f0=gSource.SampleLevel(gLinearClamp,baseUV,0.0f);

    float phaseFrac=frac(useX?srcPos.x:srcPos.y);
    if(phaseFrac==0.0f) return f0;

    float4 f1=gSource.SampleLevel(gLinearClamp,baseUV+axisUV,0.0f);
    float4 f2=gSource.SampleLevel(gLinearClamp,baseUV+2.0f*axisUV,0.0f);
    float4 d0=f0-fm1,d1=f1-f0,d2=f2-f1;
    float4 m0x2=MCSlope2(d0,d1);
    float4 m1x2=MCSlope2(d1,d2);
    float4 coeff=(phaseFrac>0.5f)?float4(7.0f,1.0f,20.0f,-2.0f):float4(20.0f,2.0f,7.0f,-1.0f);
    return (coeff.x*f0+coeff.y*m0x2+coeff.z*f1+coeff.w*m1x2)*(1.0f/27.0f);
}
