// PTAR EDGE-NG v02 G5 runtime candidate
// Direct3D 11 / Shader Model 5.0
// Logical texture path per pixel: 1 GatherGreen + 4 SampleLevel.
// This is new NG code, not historical EDGE v03.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PTAREdgeNGV02Constants : register(b0)
{
    float2 gInputSize;
    float2 gOutputSize;
};

struct PSIn { float4 position : SV_Position; };

float4 Cubic13(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    float4 qL=(-fm1+8.0f*f0+2.0f*f1)/9.0f;
    float4 qR=(5.0f*f0+5.0f*f1-f2)/9.0f;
    return (5.0f*qL+4.0f*qR)/9.0f;
}
float4 Cubic23(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    float4 qL=(-fm1+5.0f*f0+5.0f*f1)/9.0f;
    float4 qR=(2.0f*f0+8.0f*f1-f2)/9.0f;
    return (4.0f*qL+5.0f*qR)/9.0f;
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

    // Query exactly the floor/floor+1 2x2 cell. The gather register order is
    // x=lower-left, y=lower-right, z=upper-right, w=upper-left in gather4 terms.
    // With texture-v increasing downward, only the gy sign changes; abs(gy) is used.
    float2 gatherUV=(srcFloor+1.0f)/gInputSize;
    float4 g=gSource.GatherGreen(gLinearClamp,gatherUV);
    float gx=(g.y+g.z)-(g.x+g.w);
    float gy=(g.x+g.y)-(g.w+g.z);
    bool useX=abs(gx)>=abs(gy);

    float2 axis=useX?float2(1.0f,0.0f):float2(0.0f,1.0f);
    float2 basePos=useX?float2(srcFloor.x,srcPos.y):float2(srcPos.x,srcFloor.y);

    float4 fm1=gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos-axis),0.0f);
    float4 f0 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos),0.0f);
    float4 f1 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+axis),0.0f);
    float4 f2 =gSource.SampleLevel(gLinearClamp,TexelCenterUV(basePos+2.0f*axis),0.0f);

    uint phaseIndex=useX?(outPix.x%3u):(outPix.y%3u);
    if(phaseIndex==0u) return f0;
    if(phaseIndex==2u) return Cubic13(fm1,f0,f1,f2);
    return Cubic23(fm1,f0,f1,f2);
}
