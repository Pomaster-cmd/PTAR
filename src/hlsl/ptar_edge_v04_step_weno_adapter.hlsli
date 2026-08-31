#ifndef PTAR_EDGE_V04_STEP_WENO_ADAPTER_HLSLI
#define PTAR_EDGE_V04_STEP_WENO_ADAPTER_HLSLI

#include "ptar_step_weno_lite.hlsli"

// PTAR EDGE v04 experimental adapter.
// EDGE v03 remains the authority for direction/orientation, four directional
// samples, luminance, existing EDGE confidence, and baseline EDGE output.
//
// Compile-time modes:
//   0 = EDGE v03 baseline pass-through
//   1 = SW1
//   2 = SW2
//   3 = SW3
#ifndef PTAR_EDGE_V04_STEP_MODE
#define PTAR_EDGE_V04_STEP_MODE 0
#endif

#if (PTAR_EDGE_V04_STEP_MODE < 0) || (PTAR_EDGE_V04_STEP_MODE > 3)
#error PTAR_EDGE_V04_STEP_MODE must be 0, 1, 2 or 3
#endif

float4 PTAREdgeV04Phase13(
    float4 edgeV03Baseline, float edgeConfidence,
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2, float epsilon)
{
#if PTAR_EDGE_V04_STEP_MODE == 0
    return edgeV03Baseline;
#elif PTAR_EDGE_V04_STEP_MODE == 1
    return PTARStepSW1_13(fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
#elif PTAR_EDGE_V04_STEP_MODE == 2
    return PTARStepSW2_13(fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
#else
    return PTARStepSW3_13(
        edgeV03Baseline, edgeConfidence,
        fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
#endif
}

float4 PTAREdgeV04Phase23(
    float4 edgeV03Baseline, float edgeConfidence,
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2, float epsilon)
{
#if PTAR_EDGE_V04_STEP_MODE == 0
    return edgeV03Baseline;
#elif PTAR_EDGE_V04_STEP_MODE == 1
    return PTARStepSW1_23(fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
#elif PTAR_EDGE_V04_STEP_MODE == 2
    return PTARStepSW2_23(fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
#else
    return PTARStepSW3_23(
        edgeV03Baseline, edgeConfidence,
        fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
#endif
}

#endif
