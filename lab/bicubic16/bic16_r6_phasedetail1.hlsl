// PTAR D3D9 Q720 BICUBIC16 R6 PHASEDETAIL1
// Objective: move 1280x720 -> 1920x1080 materially closer to native 1080p
// in microstructure (small text, foliage, chair mesh, ground texture) without
// turning the result into a globally sharpened image.
//
// R5 still leaves substantial high-frequency deficit in field captures.
// R6 keeps the 16-tap Catmull-Rom reconstruction, but adds a source-space
// directional high-pass term reconstructed from the same 4x4 footprint.
// Coherent strong edges suppress sharpening across their gradient direction;
// low/mid-contrast texture keeps more detail. A small subpixel-phase gain
// compensates high-pass attenuation at the 1.5x reconstruction phases.
// No extra texture fetches and no extra fullscreen pass.

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

    // Exact bilinear reconstruction from the central 2x2.
    float4 bx=lerp(s11,s21,f.x);
    float4 by=lerp(s12,s22,f.x);
    float4 bilinear=lerp(bx,by,f.y);

    // Luma structure classification from the same central 2x2.
    float l11=PtLuma(s11);
    float l21=PtLuma(s21);
    float l12=PtLuma(s12);
    float l22=PtLuma(s22);
    float lmin=min(min(l11,l21),min(l12,l22));
    float lmax=max(max(l11,l21),max(l12,l22));
    float lrange=lmax-lmin;
    float edge=saturate((lrange-0.03f)*6.6666667f);

    // Direction of the dominant local gradient. High coherent gradients only
    // attenuate the high-pass component that crosses that edge.
    float gx=0.5f*((l21-l11)+(l22-l12));
    float gy=0.5f*((l12-l11)+(l22-l21));
    float ax=abs(gx);
    float ay=abs(gy);
    float gsum=ax+ay+0.00001f;
    float strong=saturate((gsum-0.02f)*6.25f);
    float attenX=1.0f-0.45f*strong*(ax/gsum);
    float attenY=1.0f-0.45f*strong*(ay/gsum);

    // Source-space second differences for the four central source pixels.
    // All neighbours are already among the 16 Catmull-Rom taps.
    float4 dx11=s11-0.5f*(s01+s21);
    float4 dx21=s21-0.5f*(s11+s31);
    float4 dx12=s12-0.5f*(s02+s22);
    float4 dx22=s22-0.5f*(s12+s32);
    float4 dy11=s11-0.5f*(s10+s12);
    float4 dy21=s21-0.5f*(s20+s22);
    float4 dy12=s12-0.5f*(s11+s13);
    float4 dy22=s22-0.5f*(s21+s23);

    float4 dx0=lerp(dx11,dx21,f.x);
    float4 dx1=lerp(dx12,dx22,f.x);
    float4 dy0=lerp(dy11,dy21,f.x);
    float4 dy1=lerp(dy12,dy22,f.x);
    float4 detailX=lerp(dx0,dx1,f.y);
    float4 detailY=lerp(dy0,dy1,f.y);
    float4 sourceDetail=0.5f*(attenX*detailX+attenY*detailY);

    // At the 1.5x output phases, bilinear interpolation of sourceDetail loses
    // the most amplitude around half phase. Compensate only modestly.
    float phaseX=4.0f*f.x*(1.0f-f.x);
    float phaseY=4.0f*f.y*(1.0f-f.y);
    float phaseGain=1.0f+0.20f*0.5f*(phaseX+phaseY);

    // Keep cubic-vs-bilinear residual conservative; the new source-space term
    // is responsible for most microdetail recovery. Strong edges receive less.
    float residualGain=lerp(0.70f,0.60f,edge);
    float detailGain=lerp(1.00f,0.60f,edge);
    float clampMargin=lerp(0.18f,0.08f,edge);

    float4 raw=cubic
        + residualGain*(cubic-bilinear)
        + detailGain*phaseGain*sourceDetail;

    float4 localMin=min(min(s11,s21),min(s12,s22));
    float4 localMax=max(max(s11,s21),max(s12,s22));
    float4 localRange=localMax-localMin;
    float4 outv=clamp(
        raw,
        localMin-clampMargin*localRange,
        localMax+clampMargin*localRange);
    return saturate(outv);
}
