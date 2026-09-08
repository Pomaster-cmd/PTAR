// PTAR-NG MoE v02 - LAB18 Frozen Texture Admission
// Direct3D 11 / Shader Model 5.0 / Windows 8.1 target.
//
// Frozen after LAB18 A+B selection and before LAB19. LAB19 then passed the
// independent post-freeze validation without retuning this candidate.
//
// Architecture:
//   - MoE v01 SF5 is the safe base.
//   - LAB07 shape-preserving MC Hermite is admitted conservatively.
//   - Admission uses only signals already available from the shared footprint.
//
// Frozen admission gate:
//   0.50 * low_coherence * balanced_outer_support * central_support
//   low_coherence          = 1-sat((coherence-0.75)/0.18)
//   balanced_outer_support =   sat((outer_balance-0.10)/0.15)
//   central_support        =   sat((central-0.10)/0.15)
//
// Texture contract:
//   exactly 1 GatherGreen + 4 SampleLevel
//   no UAV, no intermediate texture, exact x1.5 only.
//
// No NIS. No frame-generation/NVENC logic in this spatial shader.

Texture2D<float4> gSource : register(t0);
SamplerState gLinearClamp : register(s0);

cbuffer PTARMoeNGV02Constants : register(b0)
{
    float2 gInputSize;
    float2 gOutputSize;
};

struct PSIn { float4 position : SV_Position; };

float2 TexelCenterUV(float2 texelPos)
{
    return (texelPos + 0.5f) / gInputSize;
}

float4 K185_13(float4 fm1, float4 f0, float4 f1, float4 f2)
{
    return
        (-0.274074074074074f) * fm1 +
        ( 0.877777777777778f) * f0  +
        ( 0.533333333333333f) * f1  +
        (-0.137037037037037f) * f2;
}

float4 K185_23(float4 fm1, float4 f0, float4 f1, float4 f2)
{
    return
        (-0.137037037037037f) * fm1 +
        ( 0.533333333333333f) * f0  +
        ( 0.877777777777778f) * f1  +
        (-0.274074074074074f) * f2;
}

float4 MCSlope(float4 a, float4 b)
{
    float4 avg = 0.5f * (a + b);
    float4 lim = 2.0f * min(abs(a), abs(b));
    float4 limited = clamp(avg, -lim, lim);
    return limited * step(0.0f, a * b);
}

float4 Hermite13(float4 f0, float4 f1, float4 m0, float4 m1)
{
    float4 h = (20.0f*f0 + 4.0f*m0 + 7.0f*f1 - 2.0f*m1) * (1.0f/27.0f);
    return clamp(h, min(f0,f1), max(f0,f1));
}

float4 Hermite23(float4 f0, float4 f1, float4 m0, float4 m1)
{
    float4 h = (7.0f*f0 + 2.0f*m0 + 20.0f*f1 - 4.0f*m1) * (1.0f/27.0f);
    return clamp(h, min(f0,f1), max(f0,f1));
}

float Luma709(float4 v)
{
    return dot(v.rgb, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 main(PSIn input) : SV_Target
{
    uint2 outPix = (uint2)input.position.xy;
    float2 srcPos = float2(outPix) * (2.0f/3.0f);
    float2 srcFloor = floor(srcPos);

    // Validated gather ordering: x=LL, y=LR, z=UR, w=UL.
    float2 gatherUV = (srcFloor + 1.0f) / gInputSize;
    float4 g = gSource.GatherGreen(gLinearClamp, gatherUV);

    float tl = g.w;
    float tr = g.z;
    float bl = g.x;
    float br = g.y;

    float gx = (tr + br) - (tl + bl);
    float gy = (bl + br) - (tl + tr);
    float agx = abs(gx);
    float agy = abs(gy);
    bool useX = agx >= agy;

    float2 axis = useX ? float2(1.0f,0.0f) : float2(0.0f,1.0f);
    float2 basePos = useX
        ? float2(srcFloor.x, srcPos.y)
        : float2(srcPos.x, srcFloor.y);

    // Shared complete footprint: exactly four color samples.
    float4 fm1 = gSource.SampleLevel(gLinearClamp, TexelCenterUV(basePos-axis), 0.0f);
    float4 f0  = gSource.SampleLevel(gLinearClamp, TexelCenterUV(basePos), 0.0f);
    float4 f1  = gSource.SampleLevel(gLinearClamp, TexelCenterUV(basePos+axis), 0.0f);
    float4 f2  = gSource.SampleLevel(gLinearClamp, TexelCenterUV(basePos+2.0f*axis), 0.0f);

    uint phaseIndex = useX ? (outPix.x % 3u) : (outPix.y % 3u);
    if (phaseIndex == 0u)
        return f0;

    // Safe base: exact MoE v01 SF5 expert/router equations.
    float4 edge = (phaseIndex == 2u)
        ? K185_13(fm1,f0,f1,f2)
        : K185_23(fm1,f0,f1,f2);
    float4 raster = clamp(edge, min(f0,f1), max(f0,f1));
    float phaseFrac = (phaseIndex == 1u) ? (2.0f/3.0f) : (1.0f/3.0f);
    float4 bilinear = lerp(f0, f1, phaseFrac);
    float4 natural = lerp(bilinear, raster, 0.70f);

    float localRange = max(max(tl,tr),max(bl,br)) - min(min(tl,tr),min(bl,br));
    float gradient = 0.5f * max(agx,agy);
    float diagonal = 0.5f * abs((tl+br)-(tr+bl));
    float coherence = abs(agx-agy) / (agx+agy+1.0e-6f);

    float rangeConf = saturate((localRange-0.01f)/0.10f);
    float coherenceConf = saturate((coherence-0.25f)/0.45f);
    float diagRatio = diagonal/(gradient+1.0e-6f);
    float axisConf = 1.0f-saturate((diagRatio-0.08f)/0.45f);
    float rasterWeight = rangeConf*coherenceConf*axisConf;
    float edgeBase = saturate((gradient-0.02f)/0.10f);
    float edgeWeight = (1.0f-rasterWeight)*edgeBase;
    float naturalWeight = 1.0f-rasterWeight-edgeWeight;
    float4 v01 = natural*naturalWeight + edge*edgeWeight + raster*rasterWeight;

    // Match the validated UNORM v01 base used by LAB18/LAB19 before mixing.
    v01 = saturate(v01);

    // LAB07 MC expert from the same four color samples.
    float4 d0 = f0-fm1;
    float4 d1 = f1-f0;
    float4 d2 = f2-f1;
    float4 m0 = MCSlope(d0,d1);
    float4 m1 = MCSlope(d1,d2);
    float4 mc = (phaseIndex == 1u)
        ? Hermite23(f0,f1,m0,m1)
        : Hermite13(f0,f1,m0,m1);

    // LAB18 frozen admission signals. Coherence is from GatherGreen; support
    // topology uses Rec.709 luma of the already-fetched directional samples.
    float ld0 = Luma709(d0);
    float ld1 = Luma709(d1);
    float ld2 = Luma709(d2);
    float ad0 = abs(ld0);
    float ad1 = abs(ld1);
    float ad2 = abs(ld2);

    float central = ad1 / (ad0+ad1+ad2+1.0e-6f);
    float outerBalance = min(ad0,ad2) / (max(ad0,ad2)+1.0e-6f);
    float lowCoherence = 1.0f-saturate((coherence-0.75f)/0.18f);
    float balancedSupport = saturate((outerBalance-0.10f)/0.15f);
    float centralSupport = saturate((central-0.10f)/0.15f);
    float admission = 0.50f*lowCoherence*balancedSupport*centralSupport;

    return saturate(lerp(v01, mc, admission));
}
