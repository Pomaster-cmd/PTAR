// PTAR EDGE-NG v03 K185
// New NG code; NOT historical EDGE v03.
// Direct3D 11 / Shader Model 5.0.
// Logical texture path: 1 GatherGreen + 4 SampleLevel.
//
// Orientation: EDGE-NG v02 G5.
// Reconstruction: fixed-phase Keys-style cubic convolution, a=-1.85.
// K185 was selected by the permanent Protocol-B EDGE_CORE campaign.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PTAREdgeNGV03Constants : register(b0)
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

    // UV chosen so GatherGreen covers [floor, floor+1] in both dimensions.
    float2 gatherUV=(srcFloor+1.0f)/gInputSize;
    float4 g=gSource.GatherGreen(gLinearClamp,gatherUV);

    // gather4 ordering: x=lower-left, y=lower-right,
    // z=upper-right, w=upper-left (texture-v sign is irrelevant after abs()).
    float gx=(g.y+g.z)-(g.x+g.w);
    float gy=(g.x+g.y)-(g.w+g.z);
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
    if(phaseIndex==0u) return f0;
    if(phaseIndex==2u) return K185_13(fm1,f0,f1,f2);
    return K185_23(fm1,f0,f1,f2);
}
