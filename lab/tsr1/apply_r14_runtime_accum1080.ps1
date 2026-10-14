$ErrorActionPreference='Stop'

$headerPath='compat/x86_d3d9/ptar_tsr1_runtime.h'
$inlPath='compat/x86_d3d9/ptar_tsr1_runtime.inl'
$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'

function Replace-One([string]$text,[string]$old,[string]$new,[string]$label)
{
    $p=$text.IndexOf($old,[StringComparison]::Ordinal)
    if($p -lt 0){throw "R14 runtime anchor missing: $label"}
    $p2=$text.IndexOf($old,$p+$old.Length,[StringComparison]::Ordinal)
    if($p2 -ge 0){throw "R14 runtime anchor not unique: $label"}
    return $text.Substring(0,$p)+$new+$text.Substring($p+$old.Length)
}

# ---------------------------------------------------------------------------
# State declaration: two private full-resolution accumulation surfaces and one
# restoration shader. These resources exist only inside the FG-OFF TSR path.
# ---------------------------------------------------------------------------
$h=[IO.File]::ReadAllText((Resolve-Path $headerPath)) -replace "`r`n","`n"
if($h.IndexOf('R14 MFSR1 ACCUM1080 STATE',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R14 runtime header already patched'
}
$h=Replace-One $h @'
    IDirect3DPixelShader9* resolveShader;
'@ @'
    IDirect3DPixelShader9* resolveShader;
    IDirect3DPixelShader9* restoreShader; // R14 MFSR1 ACCUM1080 STATE
'@ 'restore shader field'
$h=Replace-One $h @'
    IDirect3DTexture9* subpixelTexture;
    IDirect3DSurface9* subpixelSurface;
'@ @'
    IDirect3DTexture9* subpixelTexture;
    IDirect3DSurface9* subpixelSurface;

    IDirect3DTexture9* accumTexture[2];
    IDirect3DSurface9* accumSurface[2];
'@ 'accum resources'
$h=Replace-One $h @'
    bool initialized;
    bool historyValid;
'@ @'
    bool initialized;
    bool historyValid;
    bool accumValid;
    UINT accumReadIndex;
'@ 'accum state'
$oldDecl=@'
static HRESULT PtTsr1Resolve(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* currentSource,
    IDirect3DSurface9* resolvedTarget);
'@
$newDecl=@'
static HRESULT PtTsr1Resolve(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* currentSource,
    IDirect3DTexture9* currentR7,
    IDirect3DSurface9* resolvedTarget);
'@
$h=Replace-One $h $oldDecl $newDecl 'resolve declaration'

# ---------------------------------------------------------------------------
# Implementation: preserve the qualified source-motion chain, then render the
# R14 resolve into A/B A8R8G8B8 1080p accumulation surfaces. Alpha carries
# confidence. A separate bounded restoration pass writes the visible target.
# ---------------------------------------------------------------------------
$i=[IO.File]::ReadAllText((Resolve-Path $inlPath)) -replace "`r`n","`n"
if($i.IndexOf('R14_MFSR1_RUNTIME',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R14 runtime implementation already patched'
}
$i=Replace-One $i @'
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.initialized=false;
'@ @'
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.accumValid=false;
    g_ptarTsr1.accumReadIndex=0;
    g_ptarTsr1.initialized=false;
'@ 'release state reset'
$i=Replace-One $i @'
    if(g_ptarTsr1.subpixelSurface){g_ptarTsr1.subpixelSurface->Release();g_ptarTsr1.subpixelSurface=0;}
'@ @'
    for(UINT r14i=0;r14i<2;++r14i)
    {
        if(g_ptarTsr1.accumSurface[r14i]){g_ptarTsr1.accumSurface[r14i]->Release();g_ptarTsr1.accumSurface[r14i]=0;}
        if(g_ptarTsr1.accumTexture[r14i]){g_ptarTsr1.accumTexture[r14i]->Release();g_ptarTsr1.accumTexture[r14i]=0;}
    }
    if(g_ptarTsr1.subpixelSurface){g_ptarTsr1.subpixelSurface->Release();g_ptarTsr1.subpixelSurface=0;}
'@ 'release accum resources'
$i=Replace-One $i @'
    if(g_ptarTsr1.resolveShader){g_ptarTsr1.resolveShader->Release();g_ptarTsr1.resolveShader=0;}
'@ @'
    if(g_ptarTsr1.restoreShader){g_ptarTsr1.restoreShader->Release();g_ptarTsr1.restoreShader=0;}
    if(g_ptarTsr1.resolveShader){g_ptarTsr1.resolveShader->Release();g_ptarTsr1.resolveShader=0;}
'@ 'release restore shader'
$i=Replace-One $i @'
    g_ptarTsr1.historyValid=false;
}

static HRESULT PtTsr1Initialize(
'@ @'
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.accumValid=false;
    g_ptarTsr1.accumReadIndex=0;
}

static HRESULT PtTsr1Initialize(
'@ 'invalidate accumulation'
$i=Replace-One $i @'
    hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarTsr1ResolvePs,&g_ptarTsr1.resolveShader);
    if(FAILED(hr) || !g_ptarTsr1.resolveShader) goto fail;
'@ @'
    hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarTsr1ResolvePs,&g_ptarTsr1.resolveShader);
    if(FAILED(hr) || !g_ptarTsr1.resolveShader) goto fail;

    hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarR14RestorePs,&g_ptarTsr1.restoreShader);
    if(FAILED(hr) || !g_ptarTsr1.restoreShader) goto fail;
'@ 'create restore shader'
$i=Replace-One $i @'
    hr=CreateRenderTexture(
        dev,g_ptarTsr1.fineW,g_ptarTsr1.fineH,format,
        &g_ptarTsr1.subpixelTexture,&g_ptarTsr1.subpixelSurface);
    if(FAILED(hr)) goto fail;

    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.initialized=true;
'@ @'
    hr=CreateRenderTexture(
        dev,g_ptarTsr1.fineW,g_ptarTsr1.fineH,format,
        &g_ptarTsr1.subpixelTexture,&g_ptarTsr1.subpixelSurface);
    if(FAILED(hr)) goto fail;

    // R14 uses alpha as private confidence storage regardless of the game's
    // backbuffer format. GTX 960M supports A8R8G8B8 render targets natively.
    for(UINT r14i=0;r14i<2;++r14i)
    {
        hr=CreateRenderTexture(
            dev,outputW,outputH,D3DFMT_A8R8G8B8,
            &g_ptarTsr1.accumTexture[r14i],
            &g_ptarTsr1.accumSurface[r14i]);
        if(FAILED(hr)) goto fail;
    }

    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.accumValid=false;
    g_ptarTsr1.accumReadIndex=0;
    g_ptarTsr1.initialized=true;
'@ 'create accumulation surfaces'
$i=Replace-One $i @'
        "TSR1_INIT PASS src=%ux%u out=%ux%u motion=%ux%u>%ux%u",
'@ @'
        "R14_MFSR1_RUNTIME INIT PASS src=%ux%u out=%ux%u motion=%ux%u>%ux%u accum=A8R8G8B8x2",
'@ 'R14 init log marker'

$resolveStart=$i.IndexOf('static HRESULT PtTsr1Resolve(',[StringComparison]::Ordinal)
$storeStart=$i.IndexOf('static HRESULT PtTsr1StoreSource(',$resolveStart,[StringComparison]::Ordinal)
if($resolveStart -lt 0 -or $storeStart -lt 0 -or $storeStart -le $resolveStart)
{
    throw 'R14 resolve function boundaries missing'
}
$newResolve=@'
static HRESULT PtTsr1Resolve(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* currentSource,
    IDirect3DTexture9* currentR7,
    IDirect3DSurface9* resolvedTarget)
{
    if(!dev || !currentSource || !currentR7 || !resolvedTarget || !PtTsr1CanResolve())
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

    const UINT writeIndex=g_ptarTsr1.accumValid?
        (1u-g_ptarTsr1.accumReadIndex):0u;
    const UINT previousIndex=g_ptarTsr1.accumValid?
        g_ptarTsr1.accumReadIndex:1u;

    PtDiagStage("R14_MFSR1_ACCUM_RESOLVE");
    dev->SetSamplerState(3,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    dev->SetSamplerState(3,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    dev->SetSamplerState(3,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(3,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(3,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(3,D3DSAMP_SRGBTEXTURE,FALSE);
    hr=dev->SetTexture(3,currentR7);
    if(FAILED(hr)) goto fail;

    dev->SetSamplerState(4,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(4,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(4,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(4,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(4,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,FALSE);
    hr=dev->SetTexture(4,g_ptarTsr1.accumTexture[previousIndex]);
    if(FAILED(hr)) goto fail;

    {
        float accumControl[4]={g_ptarTsr1.accumValid?1.0f:0.0f,0.0f,0.0f,0.0f};
        hr=dev->SetPixelShaderConstantF(1,accumControl,1);
        if(FAILED(hr)) goto fail;
    }

    hr=DrawFullscreenPass(
        dev,
        g_ptarTsr1.accumSurface[writeIndex],
        g_ptarTsr1.outputW,g_ptarTsr1.outputH,
        g_ptarTsr1.resolveShader,
        currentSource,g_ptarTsr1.sourceHistoryTexture,
        g_ptarTsr1.subpixelTexture,sizes);
    dev->SetTexture(3,0);
    dev->SetTexture(4,0);
    if(FAILED(hr)) goto fail;

    // Bounded confidence-aware restoration. This is deliberately a separate
    // 1080p pass so the accumulated signal can be enhanced without feeding
    // sharpened display pixels back into the low-resolution motion estimator.
    PtDiagStage("R14_MFSR1_RESTORE1080");
    {
        float restoreInfo[4]={
            (float)g_ptarTsr1.outputW,
            (float)g_ptarTsr1.outputH,
            1.0f/(float)g_ptarTsr1.outputW,
            1.0f/(float)g_ptarTsr1.outputH};
        hr=DrawFullscreenPass(
            dev,resolvedTarget,
            g_ptarTsr1.outputW,g_ptarTsr1.outputH,
            g_ptarTsr1.restoreShader,
            g_ptarTsr1.accumTexture[writeIndex],0,0,restoreInfo);
        if(FAILED(hr)) goto fail;
    }

    g_ptarTsr1.accumReadIndex=writeIndex;
    g_ptarTsr1.accumValid=true;
    return S_OK;

fail:
    dev->SetTexture(3,0);
    dev->SetTexture(4,0);
    PtDiagLogA("R14_MFSR1_RESOLVE_FAIL hr=0x%08lX fallback=R7",(unsigned long)hr);
    return hr;
}

'@
$i=$i.Substring(0,$resolveStart)+$newResolve+$i.Substring($storeStart)

# ---------------------------------------------------------------------------
# Proxy include for the restoration bytecode. R8 core already inserted the TSR
# bytecode includes; R14 extends that block without touching FG includes/code.
# ---------------------------------------------------------------------------
$p=[IO.File]::ReadAllText((Resolve-Path $proxyPath)) -replace "`r`n","`n"
if($p.IndexOf('ptar_r14_restore_ps_bytecode.h',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R14 restore bytecode include already present'
}
$p=Replace-One $p @'
#include "ptar_tsr1_resolve_ps_bytecode.h"
'@ @'
#include "ptar_tsr1_resolve_ps_bytecode.h"
#include "ptar_r14_restore_ps_bytecode.h"
'@ 'proxy restore include'

foreach($token in @(
    'R14 MFSR1 ACCUM1080 STATE',
    'accumTexture[2]',
    'accumValid',
    'g_ptarR14RestorePs'))
{
    if(($h+$i).IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R14 runtime final token missing: $token"
    }
}
foreach($token in @(
    'R14_MFSR1_RUNTIME',
    'R14_MFSR1_ACCUM_RESOLVE',
    'R14_MFSR1_RESTORE1080',
    'accumReadIndex=writeIndex'))
{
    if($i.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R14 implementation final token missing: $token"
    }
}
if($p.IndexOf('ptar_r14_restore_ps_bytecode.h',[StringComparison]::Ordinal) -lt 0)
{
    throw 'R14 proxy restore include final token missing'
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $headerPath),$h,$utf8)
[IO.File]::WriteAllText((Resolve-Path $inlPath),$i,$utf8)
[IO.File]::WriteAllText((Resolve-Path $proxyPath),$p,$utf8)

Write-Host 'R14_MFSR1_RUNTIME_PATCH=PASS'
Write-Host 'R14_ACCUM_SURFACES=2_X_1920X1080_A8R8G8B8'
Write-Host 'R14_ACCUM_TOPOLOGY=PING_PONG_PRIVATE_HISTORY'
Write-Host 'R14_RESTORE_PASS=ONE_BOUNDED_1080P_PASS'
Write-Host 'R14_FG_CORE_CHANGE=NONE'
