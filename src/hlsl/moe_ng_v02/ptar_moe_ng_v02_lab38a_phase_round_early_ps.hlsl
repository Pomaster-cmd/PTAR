// PTAR-NG MoE v02 - LAB38A phase ROUND_NE scheduling probe
// Frozen baseline: hardware-selected LAB37B. Exact x1.5 only. ABI unchanged.
// Moves phaseHigh production immediately after the phase-zero branch while
// preserving reconstruction, texture issue order and numerical expressions.
Texture2D<float4> gSource:register(t0);SamplerState gLinearClamp:register(s0);
cbuffer PTARMoeNGV02Constants:register(b0){float2 gInvInputSize;float2 gHalfInvInputSize;};
struct PSIn{float4 position:SV_Position;};
float4 MCSlope2(float4 a,float4 b){float4 sum=a+b;float4 lim=4.0f*min(abs(a),abs(b));float4 limited=clamp(sum,-lim,lim);return (a*b>=0.0f)?limited:0.0f;}
float4 main(PSIn input):SV_Target{
 float2 outPix=floor(input.position.xy),srcPos=outPix*(2.0f/3.0f),srcFloor=floor(srcPos);
 float4 g=gSource.GatherGreen(gLinearClamp,mad(srcFloor,gInvInputSize,gInvInputSize));
 float2 diag=g.yz-g.wx;bool useX=(diag.x*diag.y)>=0.0f;
 float2 basePos=useX?float2(srcFloor.x,srcPos.y):float2(srcPos.x,srcFloor.y);float2 baseUV=mad(basePos,gInvInputSize,gHalfInvInputSize);
 float2 axisUV=float2(useX?gInvInputSize.x:0.0f,useX?0.0f:gInvInputSize.y);
 float4 fm1=gSource.SampleLevel(gLinearClamp,baseUV-axisUV,0.0f),f0=gSource.SampleLevel(gLinearClamp,baseUV,0.0f);
 float phaseFrac=frac(useX?srcPos.x:srcPos.y);if(phaseFrac==0.0f)return f0;
 float phaseHigh=round(phaseFrac);
 float4 f1=gSource.SampleLevel(gLinearClamp,baseUV+axisUV,0.0f),f2=gSource.SampleLevel(gLinearClamp,baseUV+2.0f*axisUV,0.0f);
 float4 d0=f0-fm1,d1=f1-f0,d2=f2-f1,m0x2=MCSlope2(d0,d1),m1x2=MCSlope2(d1,d2);
 float4 coeff=mad(phaseHigh,float4(-13.0f,-1.0f,13.0f,-1.0f),float4(20.0f,2.0f,7.0f,-1.0f));
 return (coeff.x*f0+coeff.y*m0x2+coeff.z*f1+coeff.w*m1x2)*(1.0f/27.0f);
}
