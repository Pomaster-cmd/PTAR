#ifndef PTAR_STEP_WENO_LITE_HLSLI
#define PTAR_STEP_WENO_LITE_HLSLI

// PTAR STEP-WENO Lite primitive for HLSL / Direct3D 11 integration.
//
// Integration contract:
// - EDGE v03 remains responsible for direction/orientation and sample selection.
// - Caller supplies the luminance values already used by EDGE v03.
// - Caller supplies epsilon; this helper does not invent a project epsilon.
// - SW3 uses the already-existing EDGE confidence; it creates no new gate.
// - The helper contains no texture access, no resource allocation, no extra pass.

static const float PTAR_SW_INV9     = 1.0f / 9.0f;
static const float PTAR_SW_BCURV    = 13.0f / 12.0f;
static const float PTAR_SW_C13_L    = 5.0f / 9.0f;
static const float PTAR_SW_C13_R    = 4.0f / 9.0f;
static const float PTAR_SW_C23_L    = 4.0f / 9.0f;
static const float PTAR_SW_C23_R    = 5.0f / 9.0f;

float2 PTARStepSmoothness(float lm1, float l0, float l1, float l2)
{
    const float dL = l0 - lm1;
    const float dC = l1 - l0;
    const float dR = l2 - l1;
    const float eL = dC - dL;
    const float eR = dR - dC;
    return float2(
        dC * dC + PTAR_SW_BCURV * eL * eL,
        dC * dC + PTAR_SW_BCURV * eR * eR
    );
}

float PTARStepWeightP1(float betaL, float betaR, float cL, float cR, float epsilon)
{
    const float sL = epsilon + betaL;
    const float sR = epsilon + betaR;
    const float aL = cL * sR;
    const float aR = cR * sL;
    return aL * rcp(aL + aR);
}

float PTARStepWeightP2(float betaL, float betaR, float cL, float cR, float epsilon)
{
    const float sL = epsilon + betaL;
    const float sR = epsilon + betaR;
    const float aL = cL * sR * sR;
    const float aR = cR * sL * sL;
    return aL * rcp(aL + aR);
}

void PTARStepCandidates13(
    float4 fm1, float4 f0, float4 f1, float4 f2,
    out float4 qL, out float4 qR)
{
    qL = (-fm1 + 8.0f * f0 + 2.0f * f1) * PTAR_SW_INV9;
    qR = ( 5.0f * f0 + 5.0f * f1 -        f2) * PTAR_SW_INV9;
}

void PTARStepCandidates23(
    float4 fm1, float4 f0, float4 f1, float4 f2,
    out float4 qL, out float4 qR)
{
    qL = (-fm1 + 5.0f * f0 + 5.0f * f1) * PTAR_SW_INV9;
    qR = ( 2.0f * f0 + 8.0f * f1 -        f2) * PTAR_SW_INV9;
}

float4 PTARStepBlend(float4 qL, float4 qR, float wL)
{
    return qR + wL * (qL - qR);
}

float4 PTARStepClampCentral(float4 value, float4 f0, float4 f1)
{
    return clamp(value, min(f0, f1), max(f0, f1));
}

// SW1: p=1, no monotone clamp.
float4 PTARStepSW1_13(
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2,
    float epsilon)
{
    float4 qL, qR;
    PTARStepCandidates13(fm1, f0, f1, f2, qL, qR);
    const float2 beta = PTARStepSmoothness(lm1, l0, l1, l2);
    const float wL = PTARStepWeightP1(beta.x, beta.y, PTAR_SW_C13_L, PTAR_SW_C13_R, epsilon);
    return PTARStepBlend(qL, qR, wL);
}

float4 PTARStepSW1_23(
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2,
    float epsilon)
{
    float4 qL, qR;
    PTARStepCandidates23(fm1, f0, f1, f2, qL, qR);
    const float2 beta = PTARStepSmoothness(lm1, l0, l1, l2);
    const float wL = PTARStepWeightP1(beta.x, beta.y, PTAR_SW_C23_L, PTAR_SW_C23_R, epsilon);
    return PTARStepBlend(qL, qR, wL);
}

// SW2: p=2 + central monotone clamp.
float4 PTARStepSW2_13(
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2,
    float epsilon)
{
    float4 qL, qR;
    PTARStepCandidates13(fm1, f0, f1, f2, qL, qR);
    const float2 beta = PTARStepSmoothness(lm1, l0, l1, l2);
    const float wL = PTARStepWeightP2(beta.x, beta.y, PTAR_SW_C13_L, PTAR_SW_C13_R, epsilon);
    return PTARStepClampCentral(PTARStepBlend(qL, qR, wL), f0, f1);
}

float4 PTARStepSW2_23(
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2,
    float epsilon)
{
    float4 qL, qR;
    PTARStepCandidates23(fm1, f0, f1, f2, qL, qR);
    const float2 beta = PTARStepSmoothness(lm1, l0, l1, l2);
    const float wL = PTARStepWeightP2(beta.x, beta.y, PTAR_SW_C23_L, PTAR_SW_C23_R, epsilon);
    return PTARStepClampCentral(PTARStepBlend(qL, qR, wL), f0, f1);
}

// SW3: SW2 influence modulated only by the existing EDGE confidence.
float4 PTARStepSW3_13(
    float4 edgeBaselineValue, float edgeConfidence,
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2,
    float epsilon)
{
    const float4 sw2 = PTARStepSW2_13(fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
    return lerp(edgeBaselineValue, sw2, saturate(edgeConfidence));
}

float4 PTARStepSW3_23(
    float4 edgeBaselineValue, float edgeConfidence,
    float4 fm1, float4 f0, float4 f1, float4 f2,
    float lm1, float l0, float l1, float l2,
    float epsilon)
{
    const float4 sw2 = PTARStepSW2_23(fm1, f0, f1, f2, lm1, l0, l1, l2, epsilon);
    return lerp(edgeBaselineValue, sw2, saturate(edgeConfidence));
}

#endif
