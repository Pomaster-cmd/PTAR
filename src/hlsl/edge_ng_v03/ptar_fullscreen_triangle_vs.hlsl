struct VSOut { float4 position : SV_Position; };
VSOut main(uint id : SV_VertexID)
{
    VSOut o;
    float2 p=float2((id<<1)&2,id&2);
    o.position=float4(p*float2(2.0f,-2.0f)+float2(-1.0f,1.0f),0.0f,1.0f);
    return o;
}
