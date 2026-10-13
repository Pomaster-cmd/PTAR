// PTAR D3D9 Q720 BICUBIC16 R7 EDGECUBIC1
// Objective: improve native-1080p resemblance after R6 showed that adding
// source-space high-pass mainly strengthened existing edges instead of
// restoring the missing spatial structure.
//
// R7 removes the explicit high-pass term. It changes the interpolation kernel
// itself: each axis uses a phase-aware Keys cubic, sharpened at the 1.5x
// subpixel phases but relaxed toward Catmull-Rom when the local gradient
// crosses a strong edge on that axis. This is reconstruction, not post-sharpen.
// 16 texture reads, one pass, no temporal/FG changes.

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

float PtLuma(float4 c)
{
    return dot(c.rgb,float3(0.2126f,0.7152f,0.0722f));
}

float4 PtKeysWeights(float f,float a)
{
    float f2=f*f;
    float f3=f2*f;
    return float4(
        a*(f-2.0f*f2+f3),
        1.0f-(a+3.0f)*f2+(a+2.0f)*f3,
        -a*f+(2.0f*a+3.0f)*f2-(a+2.0f)*f3,
        a*(f2-f3));
}

float PtPhaseClass(float f)
{
    // At exact 1.5x scaling, 4*f*(1-f) is 5/9 on the two side phases and
    // 1.0 at the half phase. Normalize that interval to 0..1.
    float phase=4.0f*f*(1.0f-f);
    return saturate((phase-0.5555556f)*2.25f);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 p=uv*gSizes.xy-0.5f;
    float2 q=floor(p);
    float2 f=p-q;

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

    float l11=PtLuma(s11);
    float l21=PtLuma(s21);
    float l12=PtLuma(s12);
    float l22=PtLuma(s22);

    // Axis gradients: a large X gradient means that an X cubic would cross a
    // strong edge, so only the X kernel is relaxed. The orthogonal kernel can
    // stay sharp and preserve structure along the edge.
    float gx=0.5f*((l21-l11)+(l22-l12));
    float gy=0.5f*((l12-l11)+(l22-l21));
    float edgeX=saturate((abs(gx)-0.02f)*4.0f);
    float edgeY=saturate((abs(gy)-0.02f)*4.0f);

    // Phase-specific reconstruction tension. Side phases receive the sharper
    // kernel; the half phase is slightly more conservative. Strong cross-edge
    // gradients relax toward Catmull-Rom (a=-0.5).
    float baseAx=lerp(-1.35f,-1.20f,PtPhaseClass(f.x));
    float baseAy=lerp(-1.35f,-1.20f,PtPhaseClass(f.y));
    float ax=lerp(baseAx,-0.50f,edgeX);
    float ay=lerp(baseAy,-0.50f,edgeY);

    float4 wx=PtKeysWeights(f.x,ax);
    float4 wy=PtKeysWeights(f.y,ay);

    float4 r0=s00*wx.x+s10*wx.y+s20*wx.z+s30*wx.w;
    float4 r1=s01*wx.x+s11*wx.y+s21*wx.z+s31*wx.w;
    float4 r2=s02*wx.x+s12*wx.y+s22*wx.z+s32*wx.w;
    float4 r3=s03*wx.x+s13*wx.y+s23*wx.z+s33*wx.w;
    float4 cubic=r0*wy.x+r1*wy.y+r2*wy.z+r3*wy.w;

    // Conservative contrast envelope. Unlike R6 this is not a detail boost;
    // it only prevents the sharper reconstruction kernel from forming halos.
    float lmin=min(min(l11,l21),min(l12,l22));
    float lmax=max(max(l11,l21),max(l12,l22));
    float lrange=lmax-lmin;
    float edge=saturate((lrange-0.03f)*6.6666667f);
    float clampMargin=lerp(0.12f,0.04f,edge);

    float4 localMin=min(min(s11,s21),min(s12,s22));
    float4 localMax=max(max(s11,s21),max(s12,s22));
    float4 localRange=localMax-localMin;
    float4 outv=clamp(
        cubic,
        localMin-clampMargin*localRange,
        localMax+clampMargin*localRange);
    return saturate(outv);
}
