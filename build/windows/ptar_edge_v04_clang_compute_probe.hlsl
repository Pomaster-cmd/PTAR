#include "../../src/hlsl/ptar_edge_v04_step_weno_adapter.hlsli"

[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float4 base=(float4)0.5f;
    float4 fm1=(float4)0.1f;
    float4 f0=(float4)0.3f;
    float4 f1=(float4)0.7f;
    float4 f2=(float4)0.9f;
    float4 a=PTAREdgeV04Phase13(base,0.75f,fm1,f0,f1,f2,0.1f,0.3f,0.7f,0.9f,1e-3f);
    float4 b=PTAREdgeV04Phase23(base,0.75f,fm1,f0,f1,f2,0.1f,0.3f,0.7f,0.9f,1e-3f);
    // Keep both computations semantically live in the AST/codegen path.
    if (tid.x == 0xFFFFFFFFu) { float4 c=a+b; }
}
