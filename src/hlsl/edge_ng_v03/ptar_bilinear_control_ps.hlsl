Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);
cbuffer Constants : register(b0) { float2 gInputSize; float2 gOutputSize; };
struct PSIn { float4 position : SV_Position; };
float4 main(PSIn input) : SV_Target
{
    uint2 outPix=(uint2)input.position.xy;
    float2 srcPos=float2(outPix)*(gInputSize/gOutputSize);
    float2 uv=(srcPos+0.5f)/gInputSize;
    return gSource.SampleLevel(gLinearClamp,uv,0.0f);
}
