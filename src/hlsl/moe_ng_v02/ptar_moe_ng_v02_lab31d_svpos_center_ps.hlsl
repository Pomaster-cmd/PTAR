// PTAR-NG MoE v02 - LAB31D Direct SV_Position Center Decode
// Baseline: hardware-selected LAB30C. Exact x1.5 only.
// Experimental codegen probe: on the single-sample fullscreen pass, pixel
// centers are n+0.5, so subtract 0.5 in float space instead of uint<->float
// conversion. This candidate is admissible only if WARP remains bit-exact.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);
cbuffer PTARMoeNGV02Constants : register(b0) { float2 gInputSize; float2 gOutputSize; };
struct PSIn { float4 position : SV_Position; };

float4 MCSlope2(float4 a,float4 b)
{
    float4 sum=a+b;
    float4 lim=4.0f*min(abs(a),abs(b));
    float4 limited=clamp(sum,-lim,lim);
    return (a*b>=0.0f)?limited:0.0f;
}

float4 main(PSIn input) : SV_Target
{
    float2 srcPos=(input.position.xy-0.5f)*(2.0f/3.0f);
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
