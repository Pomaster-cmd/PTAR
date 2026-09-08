// Minimal full-screen triangle vertex shader for LAB05 D3D11 WARP runtime validation.
struct VSOut
{
    float4 pos : SV_Position;
};

VSOut main(uint id : SV_VertexID)
{
    float2 p;
    if (id == 0u)      p = float2(-1.0f, -1.0f);
    else if (id == 1u) p = float2(-1.0f,  3.0f);
    else               p = float2( 3.0f, -1.0f);

    VSOut o;
    o.pos = float4(p, 0.0f, 1.0f);
    return o;
}
