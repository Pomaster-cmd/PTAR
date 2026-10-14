struct VSIn
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
};

struct VSOut
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
};

VSOut main(VSIn input)
{
    VSOut output;
    output.position=input.position;
    output.texcoord=input.texcoord;
    return output;
}
