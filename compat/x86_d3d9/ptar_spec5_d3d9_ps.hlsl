// PTAR D3D9 Q720 BICUBIC16 NEWREFS1
// Objective: make real 1280x720 game output visually closer to native 1920x1080.
// 16-tap separable Catmull-Rom reconstruction (Keys a=-0.5).
// Mild detail recovery uses the already available cubic-vs-bilinear residual.
// No extra texture fetches, no additional pass, no explicit post-AA.

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

    // Exact bilinear reconstruction from the four central samples. This costs
    // no additional source reads and provides a stable low-frequency anchor.
    float4 bx=lerp(s11,s21,f.x);
    float4 by=lerp(s12,s22,f.x);
    float4 bilinear=lerp(bx,by,f.y);

    // Empirical real-game calibration from aligned 720p PTAR / native 1080p
    // references. 0.60 recovers detail while keeping edge energy below the
    // over-sharpened SPEC5/NativeVisual2 field output.
    float4 outv=cubic+0.60f*(cubic-bilinear);
    return saturate(outv);
}
