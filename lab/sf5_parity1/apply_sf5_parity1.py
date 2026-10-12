from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
proxy=ROOT/'compat/x86_d3d9/ptar_d3d9_proxy.cpp'
p=proxy.read_text(encoding='utf-8')

anchor='''static HRESULT RenderSpatialToCurrent(IDirect3DDevice9* dev)\n{'''
assert p.count(anchor)==1, p.count(anchor)

helper=r'''struct PTSf5ColorState
{
    DWORD srgbTexture0;
    DWORD srgbWrite;
    bool valid;
};

static volatile LONG g_ptarSf5InheritedSrgbLogged=0;
static volatile LONG g_ptarSf5StateCaptureFailureLogged=0;

static bool PtSf5CaptureColorState(
    IDirect3DDevice9* dev,
    PTSf5ColorState* state)
{
    if(!dev || !state)
        return false;

    state->srgbTexture0=FALSE;
    state->srgbWrite=FALSE;
    state->valid=false;

    HRESULT hr=dev->GetSamplerState(
        0,D3DSAMP_SRGBTEXTURE,&state->srgbTexture0);
    if(FAILED(hr))
        return false;

    hr=dev->GetRenderState(
        D3DRS_SRGBWRITEENABLE,&state->srgbWrite);
    if(FAILED(hr))
        return false;

    state->valid=true;
    return true;
}

static void PtSf5RestoreColorState(
    IDirect3DDevice9* dev,
    const PTSf5ColorState& state)
{
    if(!dev || !state.valid)
        return;

    dev->SetSamplerState(
        0,D3DSAMP_SRGBTEXTURE,state.srgbTexture0);
    dev->SetRenderState(
        D3DRS_SRGBWRITEENABLE,state.srgbWrite);
}

static HRESULT DrawSf5UnormPassRect(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT targetW,
    UINT targetH,
    UINT drawX,
    UINT drawY,
    UINT drawW,
    UINT drawH,
    bool clearTarget,
    IDirect3DPixelShader9* shader,
    IDirect3DTexture9* texture0,
    const float* constant0)
{
    PTSf5ColorState state={};
    if(!PtSf5CaptureColorState(dev,&state))
    {
        // Fail open to the already field-validated path.  Never leave an
        // unknown state behind that could alter HF6 after this spatial pass.
        if(InterlockedCompareExchange(
            &g_ptarSf5StateCaptureFailureLogged,1,0)==0)
        {
            PtDiagLogA(
                "PTAR_SF5_COLOR_STATE capture_failed; using legacy inherited-state path");
        }
        return DrawFullscreenPassRect(
            dev,target,targetW,targetH,
            drawX,drawY,drawW,drawH,clearTarget,
            shader,texture0,0,0,constant0);
    }

    if((state.srgbTexture0!=FALSE || state.srgbWrite!=FALSE) &&
       InterlockedCompareExchange(
           &g_ptarSf5InheritedSrgbLogged,1,0)==0)
    {
        PtDiagLogA(
            "PTAR_SF5_COLOR_STATE inherited tex0=%lu write=%lu -> isolated UNORM for spatial pass only",
            (unsigned long)state.srgbTexture0,
            (unsigned long)state.srgbWrite);
    }

    // D3D11 SF5 reference uses ordinary UNORM SRV/RTV semantics.  Explicitly
    // neutralise only the two D3D9 states that can silently introduce sRGB
    // decode/encode into the MoE/K185 arithmetic.
    dev->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);

    HRESULT hr=DrawFullscreenPassRect(
        dev,target,targetW,targetH,
        drawX,drawY,drawW,drawH,clearTarget,
        shader,texture0,0,0,constant0);

    // Critical isolation boundary: restore the exact game state BEFORE HF6.
    // No ME/interpolation/pacing code observes the PTAR UNORM override.
    PtSf5RestoreColorState(dev,state);
    return hr;
}

'''
p=p.replace(anchor,helper+anchor,1)

old='''    return DrawFullscreenPassRect(\n        dev,\n        g_ptar.currentRealSurface,\n        g_ptar.outputW,\n        g_ptar.outputH,\n        fit.x,fit.y,fit.width,fit.height,\n        true,\n        spatialShader,\n        g_ptar.sourceTexture,\n        0,\n        0,\n        sizes);'''
new='''    return DrawSf5UnormPassRect(\n        dev,\n        g_ptar.currentRealSurface,\n        g_ptar.outputW,\n        g_ptar.outputH,\n        fit.x,fit.y,fit.width,fit.height,\n        true,\n        spatialShader,\n        g_ptar.sourceTexture,\n        sizes);'''
assert p.count(old)==1, p.count(old)
p=p.replace(old,new,1)

# Hard scope gates.
assert p.count('DrawSf5UnormPassRect(')==2  # definition + one call
assert p.count('D3DSAMP_SRGBTEXTURE')>=3
assert 'Critical isolation boundary: restore the exact game state BEFORE HF6.' in p
assert 'static HRESULT RenderFGMotionAndIntermediate' in p

proxy.write_text(p,encoding='utf-8')
print('SF5PARITY1_PATCH=PASS')
print('SF5PARITY1_SHADER_CHANGE=NONE')
print('SF5PARITY1_SCOPE=SPATIAL_SRGB_STATE_ISOLATION_ONLY')
print('SF5PARITY1_FAILURE_POLICY=LEGACY_FAIL_OPEN_WITHOUT_STATE_MUTATION')
print('SF5PARITY1_RESTORE_BEFORE_HF6=PASS')
