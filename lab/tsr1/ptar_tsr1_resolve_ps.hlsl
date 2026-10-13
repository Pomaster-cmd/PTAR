// PTAR R8 TSR1 - temporal super-resolution resolve for exact 720p -> 1080p.
//
// R7 remains the exact single-frame fallback. The already-qualified R7 output
// is supplied on s3 so TSR1 does not recompute the 16-tap spatial reconstruction
// a second time at 1080p. Current source texels are still sampled directly for
// the irregular current/history union and temporal safety envelope.
//
// s0 = current 720p source
// s1 = previous 720p source
// s2 = subpixel current->history source motion
// s3 = already-rendered exact R7 1080p current frame (POINT sampled)
// c0.xy = source dimensions, c0.zw = fitted destination dimensions

sampler2D gCurrent : register(s0);
sampler2D gHistory : register(s1);
sampler2D gMotion  : register(s2);
sampler2D gR7      : register(s3);
float4 gSizes : register(c0);

static const float PTAR_TSR_MOTION_ZERO=0.5019607843137255f; // 128/255

float2 ClampIndex(float2 p)
{
    return clamp(p,float2(0.0f,0.0f),gSizes.xy-1.0f);
}

float4 LoadCurrent(float2 p)
{
    float2 q=ClampIndex(p);
    return tex2Dlod(gCurrent,float4((q+0.5f)/gSizes.xy,0.0f,0.0f));
}

float4 LoadHistory(float2 p)
{
    float2 q=ClampIndex(p);
    return tex2Dlod(gHistory,float4((q+0.5f)/gSizes.xy,0.0f,0.0f));
}

float Luma(float4 c)
{
    return dot(c.rgb,float3(0.2126f,0.7152f,0.0722f));
}

float GaussianWeight(float2 d)
{
    // sigma=0.25 source pixel. This deliberately behaves much closer to an
    // irregular-sample reconstruction than to another smoothing filter.
    return exp2(-11.5415603f*dot(d,d));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 p=uv*gSizes.xy-0.5f;
    float2 q=floor(p);
    float2 f=p-q;

    // The exact R7 result has already been produced by RenderSpatialToCurrent.
    // POINT sampling at the same 1080p pixel grid is a byte-stable reuse path.
    float4 r7=tex2Dlod(gR7,float4(uv,0.0f,0.0f));

    // Only the central 2x2 current source samples are needed by TSR1 itself.
    float4 s11=LoadCurrent(q+float2(0,0));
    float4 s21=LoadCurrent(q+float2(1,0));
    float4 s12=LoadCurrent(q+float2(0,1));
    float4 s22=LoadCurrent(q+float2(1,1));

    float l11=Luma(s11);
    float l21=Luma(s21);
    float l12=Luma(s12);
    float l22=Luma(s22);
    float lmin=min(min(l11,l21),min(l12,l22));
    float lmax=max(max(l11,l21),max(l12,l22));
    float lrange=lmax-lmin;
    float4 currentMin=min(min(s11,s21),min(s12,s22));
    float4 currentMax=max(max(s11,s21),max(s12,s22));

    float4 motionSample=tex2D(gMotion,uv);
    float2 motion=(motionSample.rg-PTAR_TSR_MOTION_ZERO)*64.0f;
    float confidence=saturate(motionSample.b);

    // Integer-pixel history carries essentially the same sample phase as the
    // current lattice. Only the fractional component can add spatial samples.
    float2 mf=frac(abs(motion));
    float2 phaseAxis=4.0f*mf*(1.0f-mf);
    float phaseDiversity=0.5f*(phaseAxis.x+phaseAxis.y);

    float2 hp=p+motion;
    float2 hq=floor(hp);
    float2 hf=hp-hq;

    float4 h11=LoadHistory(hq+float2(0,0));
    float4 h21=LoadHistory(hq+float2(1,0));
    float4 h12=LoadHistory(hq+float2(0,1));
    float4 h22=LoadHistory(hq+float2(1,1));

    float4 historyBilinear=lerp(lerp(h11,h21,hf.x),lerp(h12,h22,hf.x),hf.y);
    float photoError=abs(Luma(historyBilinear)-Luma(r7));
    float photoBudget=0.10f+0.75f*lrange;
    float photoTrust=saturate(1.0f-photoError/max(photoBudget,0.001f));

    float inside=
        step(0.5f,hp.x)*step(hp.x,gSizes.x-1.5f)*
        step(0.5f,hp.y)*step(hp.y,gSizes.y-1.5f);
    float trust=confidence*photoTrust*inside;

    float wc00=GaussianWeight(float2(-f.x,-f.y));
    float wc10=GaussianWeight(float2(1.0f-f.x,-f.y));
    float wc01=GaussianWeight(float2(-f.x,1.0f-f.y));
    float wc11=GaussianWeight(float2(1.0f-f.x,1.0f-f.y));

    float wh00=GaussianWeight(float2(-hf.x,-hf.y));
    float wh10=GaussianWeight(float2(1.0f-hf.x,-hf.y));
    float wh01=GaussianWeight(float2(-hf.x,1.0f-hf.y));
    float wh11=GaussianWeight(float2(1.0f-hf.x,1.0f-hf.y));

    float currentWeight=wc00+wc10+wc01+wc11;
    float historyWeight=wh00+wh10+wh01+wh11;
    float totalWeight=max(currentWeight+historyWeight,0.00001f);

    float4 unionSample=(
        s11*wc00+s21*wc10+s12*wc01+s22*wc11+
        h11*wh00+h21*wh10+h12*wh01+h22*wh11)/totalWeight;

    float tsrBlend=saturate(phaseDiversity)*trust;
    float4 resolved=lerp(r7,unionSample,tsrBlend);

    // Fail-soft temporal envelope. Include R7 itself in the admissible range so
    // tsrBlend==0 is bit-stable after quantization.
    float4 histMin=min(min(h11,h21),min(h12,h22));
    float4 histMax=max(max(h11,h21),max(h12,h22));
    float4 combinedMin=min(min(currentMin,histMin),r7);
    float4 combinedMax=max(max(currentMax,histMax),r7);
    float4 combinedRange=combinedMax-combinedMin;
    resolved=clamp(resolved,combinedMin-0.03f*combinedRange,combinedMax+0.03f*combinedRange);
    resolved.a=r7.a;
    return saturate(resolved);
}
