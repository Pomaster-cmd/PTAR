// PTAR R8 TSR1 - temporal super-resolution resolve for exact 720p -> 1080p.
//
// R7 remains the exact single-frame fallback. When trustworthy fractional
// source motion exists, TSR1 treats current and reprojected history texels as
// two irregular sample lattices of the same scene. A narrow normalized kernel
// reconstructs from the union of those samples and is blended with R7 in
// proportion to subpixel phase diversity and motion confidence.
//
// s0 = current 720p source
// s1 = previous 720p source
// s2 = subpixel current->history source motion
// c0.xy = source dimensions, c0.zw = fitted destination dimensions

sampler2D gCurrent : register(s0);
sampler2D gHistory : register(s1);
sampler2D gMotion  : register(s2);
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

float4 KeysWeights(float f,float a)
{
    float f2=f*f;
    float f3=f2*f;
    return float4(
        a*(f-2.0f*f2+f3),
        1.0f-(a+3.0f)*f2+(a+2.0f)*f3,
        -a*f+(2.0f*a+3.0f)*f2-(a+2.0f)*f3,
        a*(f2-f3));
}

float PhaseClass(float f)
{
    float phase=4.0f*f*(1.0f-f);
    return saturate((phase-0.5555556f)*2.25f);
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

    // Exact R7 EDGECUBIC1 current-frame reconstruction.
    float4 s00=LoadCurrent(q+float2(-1,-1));
    float4 s10=LoadCurrent(q+float2( 0,-1));
    float4 s20=LoadCurrent(q+float2( 1,-1));
    float4 s30=LoadCurrent(q+float2( 2,-1));
    float4 s01=LoadCurrent(q+float2(-1, 0));
    float4 s11=LoadCurrent(q+float2( 0, 0));
    float4 s21=LoadCurrent(q+float2( 1, 0));
    float4 s31=LoadCurrent(q+float2( 2, 0));
    float4 s02=LoadCurrent(q+float2(-1, 1));
    float4 s12=LoadCurrent(q+float2( 0, 1));
    float4 s22=LoadCurrent(q+float2( 1, 1));
    float4 s32=LoadCurrent(q+float2( 2, 1));
    float4 s03=LoadCurrent(q+float2(-1, 2));
    float4 s13=LoadCurrent(q+float2( 0, 2));
    float4 s23=LoadCurrent(q+float2( 1, 2));
    float4 s33=LoadCurrent(q+float2( 2, 2));

    float l11=Luma(s11);
    float l21=Luma(s21);
    float l12=Luma(s12);
    float l22=Luma(s22);

    float gx=0.5f*((l21-l11)+(l22-l12));
    float gy=0.5f*((l12-l11)+(l22-l21));
    float edgeX=saturate((abs(gx)-0.02f)*4.0f);
    float edgeY=saturate((abs(gy)-0.02f)*4.0f);

    float baseAx=lerp(-1.35f,-1.20f,PhaseClass(f.x));
    float baseAy=lerp(-1.35f,-1.20f,PhaseClass(f.y));
    float ax=lerp(baseAx,-0.50f,edgeX);
    float ay=lerp(baseAy,-0.50f,edgeY);
    float4 wx=KeysWeights(f.x,ax);
    float4 wy=KeysWeights(f.y,ay);

    float4 r0=s00*wx.x+s10*wx.y+s20*wx.z+s30*wx.w;
    float4 r1=s01*wx.x+s11*wx.y+s21*wx.z+s31*wx.w;
    float4 r2=s02*wx.x+s12*wx.y+s22*wx.z+s32*wx.w;
    float4 r3=s03*wx.x+s13*wx.y+s23*wx.z+s33*wx.w;
    float4 r7=r0*wy.x+r1*wy.y+r2*wy.z+r3*wy.w;

    float lmin=min(min(l11,l21),min(l12,l22));
    float lmax=max(max(l11,l21),max(l12,l22));
    float lrange=lmax-lmin;
    float edge=saturate((lrange-0.03f)*6.6666667f);
    float clampMargin=lerp(0.12f,0.04f,edge);
    float4 currentMin=min(min(s11,s21),min(s12,s22));
    float4 currentMax=max(max(s11,s21),max(s12,s22));
    float4 currentRange=currentMax-currentMin;
    r7=clamp(r7,currentMin-clampMargin*currentRange,currentMax+clampMargin*currentRange);
    r7=saturate(r7);

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

    // Normalized reconstruction from the union of the current and history
    // source lattices. Current 2x2 samples are reused from R7, so temporal SR
    // adds only four history texture reads plus one motion read.
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

    // Synthetic native-reference sweeps show the useful temporal contribution
    // tracks subpixel phase diversity closely: one-axis half-pixel motion wants
    // roughly 50%, two-axis half-pixel motion can use the full union estimate.
    float tsrBlend=saturate(phaseDiversity)*trust;
    float4 resolved=lerp(r7,unionSample,tsrBlend);

    // Fail-soft temporal envelope. The union estimate is a convex combination
    // of real source samples; this final combined envelope only rejects motion
    // mismatches/disocclusion excursions while still permitting history detail.
    float4 histMin=min(min(h11,h21),min(h12,h22));
    float4 histMax=max(max(h11,h21),max(h12,h22));
    float4 combinedMin=min(currentMin,histMin);
    float4 combinedMax=max(currentMax,histMax);
    float4 combinedRange=combinedMax-combinedMin;
    resolved=clamp(resolved,combinedMin-0.03f*combinedRange,combinedMax+0.03f*combinedRange);
    resolved.a=r7.a;
    return saturate(resolved);
}
