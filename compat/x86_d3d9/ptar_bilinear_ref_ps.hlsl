sampler2D gSource : register(s0);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    return tex2D(gSource,uv);
}
