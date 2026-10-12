// PTAR D3D9 FULLQUALITY1
// Shader Model 3 port of the validated D3D11 QSTAGE4DIAG2 shader.
// Reference DXBC SHA256: a69d6a66fc3c8a472c6ec72e0cfe3246e2e6b9a3c81aee1208b047850d4c51db
// Reference hardware status: GTX 960M visual + GPU AVG/P99 ~0.7 ms, ADOPTED.
//
// Topology equivalence:
//   D3D11: 1 GatherGreen + 6 SampleLevel
//   D3D9:  4 gather-equivalent center reads + 6 colour reads
// No extra pass, UAV, resource binding, wait or synchronization.

sampler2D gSource : register(s0);

// c0.xy = source size, c0.zw = fitted output size
float4 gSizes : register(c0);

float2 PtUv(float2 p)
{
    return (p + 0.5f) / gSizes.xy;
}

float4 K130_13(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    return
        (-0.356296296296296f)*fm1 +
        ( 0.941111111111111f)*f0  +
        ( 0.593333333333333f)*f1  +
        (-0.178148148148148f)*f2;
}

float4 K130_23(float4 fm1,float4 f0,float4 f1,float4 f2)
{
    return
        (-0.178148148148148f)*fm1 +
        ( 0.593333333333333f)*f0  +
        ( 0.941111111111111f)*f1  +
        (-0.356296296296296f)*f2;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 outPix=floor(uv*gSizes.zw);
    outPix=min(outPix,gSizes.zw-1.0f);

    float2 srcPos=outPix*(2.0f/3.0f);
    float2 srcFloor=floor(srcPos);

    // GatherGreen-equivalent 2x2 footprint. Component arrangement is expressed
    // directly as TL/TR/BL/BR, then the D3D11 cardinal/diagonal equations are
    // reproduced explicitly.
    float tl=tex2Dlod(gSource,float4(PtUv(srcFloor+float2(0,0)),0,0)).g;
    float tr=tex2Dlod(gSource,float4(PtUv(srcFloor+float2(1,0)),0,0)).g;
    float bl=tex2Dlod(gSource,float4(PtUv(srcFloor+float2(0,1)),0,0)).g;
    float br=tex2Dlod(gSource,float4(PtUv(srcFloor+float2(1,1)),0,0)).g;

    float gx=(tr+br)-(tl+bl);
    float gy=(bl+br)-(tl+tr);
    float agx=abs(gx);
    float agy=abs(gy);
    float useX=step(agy,agx);

    // Exact QSTAGE4 base coordinate: integer on reconstruction axis,
    // fractional on the orthogonal axis.
    float2 basePos=float2(
        lerp(srcPos.x,srcFloor.x,useX),
        lerp(srcFloor.y,srcPos.y,useX));

    float4 f0=tex2Dlod(gSource,float4(PtUv(basePos),0,0));

    float phaseCoord=lerp(outPix.y,outPix.x,useX);
    float phaseIndex=phaseCoord-floor(phaseCoord/3.0f)*3.0f;
    if(phaseIndex<0.5f)
        return f0;

    // QSTAGE4 selected diagonal microdetail pair. D3D11 chooses whichever
    // gather diagonal has the stronger absolute contrast.
    float diagA=tl-br;
    float diagB=tr-bl;
    float selectDiagB=step(abs(diagA),abs(diagB));
    float2 diagMinus=lerp(float2(-1, 1),float2(-1,-1),selectDiagB);
    float2 diagPlus =-diagMinus;

    float cardinal=max(agx,agy);
    float diagSep=abs(abs(diagB)-abs(diagA));
    float diagGate=saturate((diagSep/(cardinal+1.0e-6f)-0.10f)*2.5f);

    float4 d0=tex2Dlod(gSource,float4(PtUv(basePos+diagMinus),0,0));
    float4 d1=tex2Dlod(gSource,float4(PtUv(basePos+diagPlus ),0,0));
    float4 diagResidual=f0-0.5f*(d0+d1);

    float2 axis=float2(useX,1.0f-useX);
    float4 fm1=tex2Dlod(gSource,float4(PtUv(basePos-axis),0,0));
    float4 f1 =tex2Dlod(gSource,float4(PtUv(basePos+axis),0,0));
    float4 f2 =tex2Dlod(gSource,float4(PtUv(basePos+2.0f*axis),0,0));

    float phase2=step(1.5f,phaseIndex);
    float4 edge=lerp(
        K130_23(fm1,f0,f1,f2),
        K130_13(fm1,f0,f1,f2),
        phase2);

    float4 raster=clamp(edge,min(f0,f1),max(f0,f1));
    float phaseFrac=lerp(2.0f/3.0f,1.0f/3.0f,phase2);
    float4 bilinear=lerp(f0,f1,phaseFrac);

    // QSTAGE1/QSTAGE4 NATURAL-NG N90.
    float4 natural=lerp(bilinear,raster,0.90f);

    // Exact QSTAGE1 router as recovered from the validated DXBC.
    float localRange=max(max(tl,tr),max(bl,br))-min(min(tl,tr),min(bl,br));
    float gradient=0.5f*cardinal;
    float diagonal=0.5f*abs((tr+bl)-(tl+br));
    float coherence=abs(agx-agy)/(agx+agy+1.0e-6f);

    float rangeConf=saturate((localRange-0.01f)*10.0f);
    float coherenceConf=saturate((coherence-0.25f)*2.2222222222f);
    float diagRatio=diagonal/(gradient+1.0e-6f);
    float axisConf=1.0f-saturate((diagRatio-0.08f)*2.2222222222f);
    float rasterWeight=rangeConf*coherenceConf*axisConf;

    float edgeBase=saturate((gradient-0.02f)*10.0f);
    float nonRaster=1.0f-rasterWeight;
    float edgeWeight=nonRaster*edgeBase;
    float naturalWeight=nonRaster*(1.0f-edgeBase);

    // QSTAGE4DIAG2: detail is injected only into the natural expert and only
    // where the existing strong-edge term is low. This is exactly the adopted
    // 0.125 residual path from the D3D11 DXBC.
    natural += diagResidual*(1.0f-edgeBase)*diagGate*naturalWeight*0.125f;

    return natural*naturalWeight + edge*edgeWeight + raster*rasterWeight;
}
