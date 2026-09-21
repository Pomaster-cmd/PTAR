// PTAR x86/D3D9 UNIVERSAL SPATIAL
// Generalized edge-oriented reconstruction for arbitrary low-resolution source
// sizes. Exact x1.5 remains on the validated specialized MoE shader.
//
// c0.xy = source size
// c0.zw = fitted destination content size (inside the native output target)

sampler2D gSource : register(s0);
float4 gSizes : register(c0);

float2 TexelCenterUV(float2 p)
{
    p=clamp(p,float2(0.0,0.0),gSizes.xy-1.0);
    return (p+0.5)/gSizes.xy;
}

float4 SampleTexel(float2 p)
{
    return tex2D(gSource,TexelCenterUV(p));
}

float Luma(float3 c)
{
    return dot(c,float3(0.2126,0.7152,0.0722));
}

float4 Cubic4(float4 fm1,float4 f0,float4 f1,float4 f2,float t)
{
    float t2=t*t;
    float t3=t2*t;

    float w0=-0.5*t+t2-0.5*t3;
    float w1=1.0-2.5*t2+1.5*t3;
    float w2=0.5*t+2.0*t2-1.5*t3;
    float w3=-0.5*t2+0.5*t3;

    return fm1*w0+f0*w1+f1*w2+f2*w3;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 dst=max(gSizes.zw,float2(1.0,1.0));
    float2 srcPos=(uv*dst+0.5)*(gSizes.xy/dst)-0.5;
    float2 base=floor(srcPos);
    float2 fracPos=frac(srcPos);

    float tl=Luma(SampleTexel(base+float2(0,0)).rgb);
    float tr=Luma(SampleTexel(base+float2(1,0)).rgb);
    float bl=Luma(SampleTexel(base+float2(0,1)).rgb);
    float br=Luma(SampleTexel(base+float2(1,1)).rgb);

    float gx=(tr+br)-(tl+bl);
    float gy=(bl+br)-(tl+tr);
    float agx=abs(gx);
    float agy=abs(gy);
    float useX=step(agy,agx);

    // Interpolate along the dominant local edge axis while keeping the
    // orthogonal coordinate on the game-selected source raster.
    float2 axis=float2(useX,1.0-useX);
    float phase=lerp(fracPos.y,fracPos.x,useX);
    float2 anchor=base;

    float4 fm1=SampleTexel(anchor-axis);
    float4 f0 =SampleTexel(anchor);
    float4 f1 =SampleTexel(anchor+axis);
    float4 f2 =SampleTexel(anchor+2.0*axis);

    float4 cubic=Cubic4(fm1,f0,f1,f2,phase);
    float4 linear=lerp(f0,f1,phase);

    // Preserve the fail-soft behavior used by PTAR: clamp high-order
    // excursions to the local source envelope, then blend according to edge
    // confidence instead of forcing the edge reconstruction everywhere.
    float4 lo=min(min(fm1,f0),min(f1,f2));
    float4 hi=max(max(fm1,f0),max(f1,f2));
    cubic=clamp(cubic,lo,hi);

    float localRange=max(max(tl,tr),max(bl,br))-min(min(tl,tr),min(bl,br));
    float coherence=abs(agx-agy)/(agx+agy+1.0e-6);
    float diagonal=0.5*abs((tl+br)-(tr+bl));
    float gradient=0.5*max(agx,agy);

    float rangeConf=saturate((localRange-0.01)/0.10);
    float coherenceConf=saturate((coherence-0.20)/0.50);
    float diagRatio=diagonal/(gradient+1.0e-6);
    float axisConf=1.0-saturate((diagRatio-0.08)/0.55);
    float trust=rangeConf*coherenceConf*axisConf;

    float4 outColor=lerp(linear,cubic,0.72*trust);
    return saturate(outColor);
}
