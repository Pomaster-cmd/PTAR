// PTAR X86/D3D9 SPATIAL1
// Shader Model 3 port of PTAR-NG MoE v01 SF5.
// D3D9 has no GatherGreen, so the orientation gather is reproduced with
// four center samples, followed by the four shared color samples.
// First compatibility target: correctness/launch on legacy x86 D3D9 games.

sampler2D gSource : register(s0);

// c0.xy = source size, c0.zw = output size
float4 gSizes : register(c0);

float4 K185_13(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    return
        (-0.274074074074074f)*fm1 +
        ( 0.877777777777778f)*f0  +
        ( 0.533333333333333f)*f1  +
        (-0.137037037037037f)*f2;
}

float4 K185_23(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    return
        (-0.137037037037037f)*fm1 +
        ( 0.533333333333333f)*f0  +
        ( 0.877777777777778f)*f1  +
        (-0.274074074074074f)*f2;
}

float2 TexelCenterUV(float2 texelPos)
{
    return (texelPos + 0.5f) / gSizes.xy;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 outPix = floor(uv * gSizes.zw);
    float2 srcPos = outPix * (2.0f/3.0f);
    float2 srcFloor = floor(srcPos);

    float tl = tex2D(gSource,TexelCenterUV(srcFloor + float2(0.0f,0.0f))).g;
    float tr = tex2D(gSource,TexelCenterUV(srcFloor + float2(1.0f,0.0f))).g;
    float bl = tex2D(gSource,TexelCenterUV(srcFloor + float2(0.0f,1.0f))).g;
    float br = tex2D(gSource,TexelCenterUV(srcFloor + float2(1.0f,1.0f))).g;

    float gx=(tr+br)-(tl+bl);
    float gy=(bl+br)-(tl+tr);
    float agx=abs(gx);
    float agy=abs(gy);

    float useX=step(agy,agx);
    float2 axis=float2(useX,1.0f-useX);
    float2 basePos=float2(
        lerp(srcPos.x,srcFloor.x,useX),
        lerp(srcFloor.y,srcPos.y,useX));

    float4 fm1=tex2D(gSource,TexelCenterUV(basePos-axis));
    float4 f0 =tex2D(gSource,TexelCenterUV(basePos));
    float4 f1 =tex2D(gSource,TexelCenterUV(basePos+axis));
    float4 f2 =tex2D(gSource,TexelCenterUV(basePos+2.0f*axis));

    float phaseCoord=lerp(outPix.y,outPix.x,useX);
    float phaseIndex=phaseCoord-floor(phaseCoord/3.0f)*3.0f;
    if(phaseIndex<0.5f)
        return f0;

    float phase2=step(1.5f,phaseIndex);
    float4 edge=lerp(
        K185_23(fm1,f0,f1,f2),
        K185_13(fm1,f0,f1,f2),
        phase2);

    float4 raster=clamp(edge,min(f0,f1),max(f0,f1));
    float phaseFrac=lerp(2.0f/3.0f,1.0f/3.0f,phase2);
    float4 bilinear=lerp(f0,f1,phaseFrac);
    float4 natural=lerp(bilinear,raster,0.70f);

    float localRange=max(max(tl,tr),max(bl,br))-min(min(tl,tr),min(bl,br));
    float gradient=0.5f*max(agx,agy);
    float diagonal=0.5f*abs((tl+br)-(tr+bl));
    float coherence=abs(agx-agy)/(agx+agy+1.0e-6f);

    float rangeConf=saturate((localRange-0.01f)/0.10f);
    float coherenceConf=saturate((coherence-0.25f)/0.45f);
    float diagRatio=diagonal/(gradient+1.0e-6f);
    float axisConf=1.0f-saturate((diagRatio-0.08f)/0.45f);
    float rasterWeight=rangeConf*coherenceConf*axisConf;

    float edgeBase=saturate((gradient-0.02f)/0.10f);
    float edgeWeight=(1.0f-rasterWeight)*edgeBase;
    float naturalWeight=1.0f-rasterWeight-edgeWeight;

    return natural*naturalWeight + edge*edgeWeight + raster*rasterWeight;
}
