// PTAR D3D9 Q720 BICUBIC16 R5 NATIVEFIT1
// Objective: move 1280x720 -> 1920x1080 closer to native 1080p while
// preserving the clean Bicubic16 reconstruction and avoiding SPEC5-style
// coarse pixels / halos.
//
// R3 uses a uniform cubic-vs-bilinear residual gain. Field captures show a
// different error profile: low/mid local-contrast detail remains too soft,
// while already-strong edges are close to (or locally stronger than) native.
// R5 therefore adapts both residual gain and overshoot margin from the luma
// range of the same central 2x2 source samples. No extra texture fetches.

sampler2D gSource : register(s0);
float4 gSizes : register(c0); // xy=source size, zw=fitted destination size

float2 PtClampIndex(float2 p)
{
    return clamp(p,float2(0,0),gSizes.xy-1.0f);
}

float4 PtLoad(float2 p)
{
    float2 q=PtClampIndex(p);
    return tex2Dlod(gSource,float4((q+0.5f)/gSizes.xy,0,0));
}

float4 PtCatmullWeights(float f)
{
    float f2=f*f;
    float f3=f2*f;
    return float4(
        -0.5f*f + f2 - 0.5f*f3,
        1.0f - 2.5f*f2 + 1.5f*f3,
        0.5f*f + 2.0f*f2 - 1.5f*f3,
        -0.5f*f2 + 0.5f*f3);
}

float PtLuma(float4 c)
{
    return dot(c.rgb,float3(0.2126f,0.7152f,0.0722f));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 p=uv*gSizes.xy-0.5f;
    float2 q=floor(p);
    float2 f=p-q;
    float4 wx=PtCatmullWeights(f.x);
    float4 wy=PtCatmullWeights(f.y);

    float4 s00=PtLoad(q+float2(-1,-1));
    float4 s10=PtLoad(q+float2( 0,-1));
    float4 s20=PtLoad(q+float2( 1,-1));
    float4 s30=PtLoad(q+float2( 2,-1));
    float4 s01=PtLoad(q+float2(-1, 0));
    float4 s11=PtLoad(q+float2( 0, 0));
    float4 s21=PtLoad(q+float2( 1, 0));
    float4 s31=PtLoad(q+float2( 2, 0));
    float4 s02=PtLoad(q+float2(-1, 1));
    float4 s12=PtLoad(q+float2( 0, 1));
    float4 s22=PtLoad(q+float2( 1, 1));
    float4 s32=PtLoad(q+float2( 2, 1));
    float4 s03=PtLoad(q+float2(-1, 2));
    float4 s13=PtLoad(q+float2( 0, 2));
    float4 s23=PtLoad(q+float2( 1, 2));
    float4 s33=PtLoad(q+float2( 2, 2));

    float4 r0=s00*wx.x+s10*wx.y+s20*wx.z+s30*wx.w;
    float4 r1=s01*wx.x+s11*wx.y+s21*wx.z+s31*wx.w;
    float4 r2=s02*wx.x+s12*wx.y+s22*wx.z+s32*wx.w;
    float4 r3=s03*wx.x+s13*wx.y+s23*wx.z+s33*wx.w;
    float4 cubic=r0*wy.x+r1*wy.y+r2*wy.z+r3*wy.w;

    // Exact bilinear reconstruction from the four central samples.
    float4 bx=lerp(s11,s21,f.x);
    float4 by=lerp(s12,s22,f.x);
    float4 bilinear=lerp(bx,by,f.y);

    float l11=PtLuma(s11);
    float l21=PtLuma(s21);
    float l12=PtLuma(s12);
    float l22=PtLuma(s22);
    float lmin=min(min(l11,l21),min(l12,l22));
    float lmax=max(max(l11,l21),max(l12,l22));
    float lrange=lmax-lmin;

    // NATIVEFIT1: low/mid-contrast structures receive more residual recovery;
    // strong edges taper back to a conservative gain. Transition range is
    // 0.03..0.18 luma in linear pass space.
    float edge=saturate((lrange-0.03f)*6.6666667f);
    float detailGain=lerp(1.85f,1.00f,edge);
    float clampMargin=lerp(0.18f,0.07f,edge);

    float4 raw=cubic+detailGain*(cubic-bilinear);
    float4 localMin=min(min(s11,s21),min(s12,s22));
    float4 localMax=max(max(s11,s21),max(s12,s22));
    float4 localRange=localMax-localMin;
    float4 outv=clamp(raw,localMin-clampMargin*localRange,localMax+clampMargin*localRange);
    return saturate(outv);
}
