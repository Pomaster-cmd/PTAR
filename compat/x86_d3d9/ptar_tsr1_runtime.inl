// PTAR R8 TSR1 runtime implementation.
// Included after CreateRenderTexture/DrawFullscreenPass are defined.

static void PtTsr1Release()
{
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.initialized=false;

    if(g_ptarTsr1.subpixelSurface){g_ptarTsr1.subpixelSurface->Release();g_ptarTsr1.subpixelSurface=0;}
    if(g_ptarTsr1.subpixelTexture){g_ptarTsr1.subpixelTexture->Release();g_ptarTsr1.subpixelTexture=0;}
    if(g_ptarTsr1.fineSurface){g_ptarTsr1.fineSurface->Release();g_ptarTsr1.fineSurface=0;}
    if(g_ptarTsr1.fineTexture){g_ptarTsr1.fineTexture->Release();g_ptarTsr1.fineTexture=0;}
    if(g_ptarTsr1.coarseSurface){g_ptarTsr1.coarseSurface->Release();g_ptarTsr1.coarseSurface=0;}
    if(g_ptarTsr1.coarseTexture){g_ptarTsr1.coarseTexture->Release();g_ptarTsr1.coarseTexture=0;}
    if(g_ptarTsr1.sourceHistorySurface){g_ptarTsr1.sourceHistorySurface->Release();g_ptarTsr1.sourceHistorySurface=0;}
    if(g_ptarTsr1.sourceHistoryTexture){g_ptarTsr1.sourceHistoryTexture->Release();g_ptarTsr1.sourceHistoryTexture=0;}

    if(g_ptarTsr1.resolveShader){g_ptarTsr1.resolveShader->Release();g_ptarTsr1.resolveShader=0;}
    if(g_ptarTsr1.subpixelShader){g_ptarTsr1.subpixelShader->Release();g_ptarTsr1.subpixelShader=0;}
    if(g_ptarTsr1.refineShader){g_ptarTsr1.refineShader->Release();g_ptarTsr1.refineShader=0;}
    if(g_ptarTsr1.coarseShader){g_ptarTsr1.coarseShader->Release();g_ptarTsr1.coarseShader=0;}

    g_ptarTsr1.sourceW=g_ptarTsr1.sourceH=0;
    g_ptarTsr1.outputW=g_ptarTsr1.outputH=0;
    g_ptarTsr1.coarseW=g_ptarTsr1.coarseH=0;
    g_ptarTsr1.fineW=g_ptarTsr1.fineH=0;
    g_ptarTsr1.format=D3DFMT_UNKNOWN;
}

static HRESULT PtTsr1Initialize(
    IDirect3DDevice9* dev,
    UINT sourceW,UINT sourceH,
    UINT outputW,UINT outputH,
    D3DFORMAT format)
{
    PtTsr1Release();
    if(!dev || !sourceW || !sourceH || !outputW || !outputH)
        return D3DERR_INVALIDCALL;

    g_ptarTsr1.sourceW=sourceW;
    g_ptarTsr1.sourceH=sourceH;
    g_ptarTsr1.outputW=outputW;
    g_ptarTsr1.outputH=outputH;
    g_ptarTsr1.coarseW=(sourceW+3u)/4u;
    g_ptarTsr1.coarseH=(sourceH+3u)/4u;
    g_ptarTsr1.fineW=(sourceW+1u)/2u;
    g_ptarTsr1.fineH=(sourceH+1u)/2u;
    g_ptarTsr1.format=format;

    HRESULT hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarFgMeCoarsePs,&g_ptarTsr1.coarseShader);
    if(FAILED(hr) || !g_ptarTsr1.coarseShader) goto fail;

    hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarFgMeRefinePs,&g_ptarTsr1.refineShader);
    if(FAILED(hr) || !g_ptarTsr1.refineShader) goto fail;

    hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarTsr1MotionSubpixelPs,&g_ptarTsr1.subpixelShader);
    if(FAILED(hr) || !g_ptarTsr1.subpixelShader) goto fail;

    hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarTsr1ResolvePs,&g_ptarTsr1.resolveShader);
    if(FAILED(hr) || !g_ptarTsr1.resolveShader) goto fail;

    hr=CreateRenderTexture(
        dev,sourceW,sourceH,format,
        &g_ptarTsr1.sourceHistoryTexture,
        &g_ptarTsr1.sourceHistorySurface);
    if(FAILED(hr)) goto fail;

    hr=CreateRenderTexture(
        dev,g_ptarTsr1.coarseW,g_ptarTsr1.coarseH,format,
        &g_ptarTsr1.coarseTexture,&g_ptarTsr1.coarseSurface);
    if(FAILED(hr)) goto fail;

    hr=CreateRenderTexture(
        dev,g_ptarTsr1.fineW,g_ptarTsr1.fineH,format,
        &g_ptarTsr1.fineTexture,&g_ptarTsr1.fineSurface);
    if(FAILED(hr)) goto fail;

    hr=CreateRenderTexture(
        dev,g_ptarTsr1.fineW,g_ptarTsr1.fineH,format,
        &g_ptarTsr1.subpixelTexture,&g_ptarTsr1.subpixelSurface);
    if(FAILED(hr)) goto fail;

    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.initialized=true;
    PtDiagLogA(
        "TSR1_INIT PASS src=%ux%u out=%ux%u motion=%ux%u>%ux%u",
        sourceW,sourceH,outputW,outputH,
        g_ptarTsr1.coarseW,g_ptarTsr1.coarseH,
        g_ptarTsr1.fineW,g_ptarTsr1.fineH);
    return S_OK;

fail:
    PtDiagLogA("TSR1_INIT_FAIL hr=0x%08lX fallback=R7",(unsigned long)hr);
    PtTsr1Release();
    return FAILED(hr)?hr:E_FAIL;
}

static bool PtTsr1CanResolve()
{
    return g_ptarTsr1.initialized && g_ptarTsr1.historyValid;
}

static HRESULT PtTsr1Resolve(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* currentSource,
    IDirect3DSurface9* resolvedTarget)
{
    if(!dev || !currentSource || !resolvedTarget || !PtTsr1CanResolve())
        return S_FALSE;

    float source[4]={
        (float)g_ptarTsr1.sourceW,
        (float)g_ptarTsr1.sourceH,
        1.0f/(float)g_ptarTsr1.sourceW,
        1.0f/(float)g_ptarTsr1.sourceH};

    PtDiagStage("TSR1_ME_COARSE");
    HRESULT hr=DrawFullscreenPass(
        dev,
        g_ptarTsr1.coarseSurface,
        g_ptarTsr1.coarseW,g_ptarTsr1.coarseH,
        g_ptarTsr1.coarseShader,
        g_ptarTsr1.sourceHistoryTexture,currentSource,0,source);
    if(FAILED(hr)) goto fail;

    PtDiagStage("TSR1_ME_REFINE");
    hr=DrawFullscreenPass(
        dev,
        g_ptarTsr1.fineSurface,
        g_ptarTsr1.fineW,g_ptarTsr1.fineH,
        g_ptarTsr1.refineShader,
        g_ptarTsr1.sourceHistoryTexture,currentSource,
        g_ptarTsr1.coarseTexture,source);
    if(FAILED(hr)) goto fail;

    PtDiagStage("TSR1_ME_SUBPIXEL");
    hr=DrawFullscreenPass(
        dev,
        g_ptarTsr1.subpixelSurface,
        g_ptarTsr1.fineW,g_ptarTsr1.fineH,
        g_ptarTsr1.subpixelShader,
        g_ptarTsr1.sourceHistoryTexture,currentSource,
        g_ptarTsr1.fineTexture,source);
    if(FAILED(hr)) goto fail;

    float sizes[4]={
        (float)g_ptarTsr1.sourceW,
        (float)g_ptarTsr1.sourceH,
        (float)g_ptarTsr1.outputW,
        (float)g_ptarTsr1.outputH};

    PtDiagStage("TSR1_RESOLVE");
    hr=DrawFullscreenPass(
        dev,
        resolvedTarget,
        g_ptarTsr1.outputW,g_ptarTsr1.outputH,
        g_ptarTsr1.resolveShader,
        currentSource,g_ptarTsr1.sourceHistoryTexture,
        g_ptarTsr1.subpixelTexture,sizes);
    if(FAILED(hr)) goto fail;

    return S_OK;

fail:
    PtDiagLogA("TSR1_RESOLVE_FAIL hr=0x%08lX fallback=R7",(unsigned long)hr);
    return hr;
}

static HRESULT PtTsr1StoreSource(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* currentSourceSurface,
    IDirect3DTexture9* currentSourceTexture,
    IDirect3DPixelShader9* copyShader)
{
    if(!dev || !g_ptarTsr1.initialized ||
       !currentSourceSurface || !currentSourceTexture || !copyShader)
        return S_FALSE;

    HRESULT hr=dev->StretchRect(
        currentSourceSurface,0,
        g_ptarTsr1.sourceHistorySurface,0,
        D3DTEXF_NONE);

    if(FAILED(hr))
    {
        hr=DrawFullscreenPass(
            dev,
            g_ptarTsr1.sourceHistorySurface,
            g_ptarTsr1.sourceW,g_ptarTsr1.sourceH,
            copyShader,currentSourceTexture,0,0,0);
    }

    if(SUCCEEDED(hr))
        g_ptarTsr1.historyValid=true;
    else
    {
        g_ptarTsr1.historyValid=false;
        PtDiagLogA("TSR1_HISTORY_STORE_FAIL hr=0x%08lX",(unsigned long)hr);
    }
    return hr;
}
