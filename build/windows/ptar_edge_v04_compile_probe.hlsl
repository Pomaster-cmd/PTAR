#include "../../src/hlsl/ptar_edge_v04_step_weno_adapter.hlsli"

cbuffer PTARCompileProbeConstants : register(b0)
{
    float4 gEdgeBaseline;
    float4 gFm1;
    float4 gF0;
    float4 gF1;
    float4 gF2;
    float4 gLum0123;
    float  gEdgeConfidence;
    float  gEpsilon;
    float  gPhaseSelector;
    float  gPad0;
};

struct PSIn
{
    float4 position : SV_Position;
};

float4 main(PSIn input) : SV_Target
{
    if (gPhaseSelector < 0.5f)
    {
        return PTAREdgeV04Phase13(
            gEdgeBaseline, gEdgeConfidence,
            gFm1, gF0, gF1, gF2,
            gLum0123.x, gLum0123.y, gLum0123.z, gLum0123.w,
            gEpsilon);
    }

    return PTAREdgeV04Phase23(
        gEdgeBaseline, gEdgeConfidence,
        gFm1, gF0, gF1, gF2,
        gLum0123.x, gLum0123.y, gLum0123.z, gLum0123.w,
        gEpsilon);
}
