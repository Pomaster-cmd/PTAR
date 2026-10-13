// PTAR R8 TSR1 - subpixel source-motion refinement.
// Input base motion comes from the existing coarse/refine estimator but is
// interpreted in SOURCE pixels. One Lucas-Kanade correction recovers the
// fractional phase needed by temporal super-resolution.

sampler2D gPrev : register(s0);
sampler2D gCurr : register(s1);
sampler2D gBase : register(s2);

// c0 = sourceWidth, sourceHeight, invSourceWidth, invSourceHeight
float4 gSource : register(c0);
static const float PTAR_TSR_MOTION_ZERO=0.5019607843137255f; // 128/255

float Luma(float3 c)
{
    return dot(c,float3(0.2126f,0.7152f,0.0722f));
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float4 baseSample=tex2D(gBase,uv);
    float2 base=(baseSample.rg-0.5f)*64.0f;

    // The legacy RGBA8 motion encoding cannot represent exact zero because
    // 0.5 lies between byte codes 127 and 128. The old estimator only emits
    // multiples of four pixels, so snap its decoded result back to that lattice
    // before deriving a fractional correction. TSR1 then uses a byte-exact
    // 128/255 zero point for its own motion output.
    base=floor(base/4.0f+0.5f)*4.0f;
    float baseConfidence=saturate(baseSample.b);

    float current=Luma(tex2D(gCurr,uv).rgb);
    float2 baseUv=uv+base*gSource.zw;

    float p=Luma(tex2D(gPrev,baseUv).rgb);
    float pxm=Luma(tex2D(gPrev,baseUv-float2(gSource.z,0.0f)).rgb);
    float pxp=Luma(tex2D(gPrev,baseUv+float2(gSource.z,0.0f)).rgb);
    float pym=Luma(tex2D(gPrev,baseUv-float2(0.0f,gSource.w)).rgb);
    float pyp=Luma(tex2D(gPrev,baseUv+float2(0.0f,gSource.w)).rgb);

    float2 gradient=0.5f*float2(pxp-pxm,pyp-pym);
    float gradient2=dot(gradient,gradient);
    float residual=current-p;

    // Brightness-constancy correction in source-pixel units. The regularizer
    // prevents flat regions from inventing motion; +/-2 px bridges the maximum
    // quantization gap left by the existing 4-pixel refine lattice.
    float2 delta=residual*gradient/(gradient2+0.0015f);
    delta=clamp(delta,float2(-2.0f,-2.0f),float2(2.0f,2.0f));

    float2 refined=base+delta;
    float refinedPhoto=Luma(tex2D(gPrev,uv+refined*gSource.zw).rgb);
    float photoError=abs(refinedPhoto-current);

    float photoTrust=saturate(1.0f-photoError/0.18f);
    float textureTrust=saturate((sqrt(gradient2)-0.003f)*28.0f);
    float confidence=baseConfidence*photoTrust*lerp(0.20f,1.0f,textureTrust);

    float2 encoded=saturate(refined/64.0f+PTAR_TSR_MOTION_ZERO);
    return float4(encoded,confidence,1.0f);
}
