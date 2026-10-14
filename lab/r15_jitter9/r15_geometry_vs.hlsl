struct VSIn
{
    float4 position : POSITION0;
    float4 color : COLOR0;
};

struct VSOut
{
    float4 position : POSITION0;
    float4 color : COLOR0;
};

VSOut main(VSIn input)
{
    VSOut output;
    output.position=input.position;
    output.color=input.color;
    return output;
}
