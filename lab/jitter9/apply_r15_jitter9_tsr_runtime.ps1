$ErrorActionPreference='Stop'

$headerPath='compat/x86_d3d9/ptar_tsr1_runtime.h'
$inlPath='compat/x86_d3d9/ptar_tsr1_runtime.inl'
$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$utf8=New-Object System.Text.UTF8Encoding($false)

function Replace-One([string]$text,[string]$old,[string]$new,[string]$label)
{
    $p=$text.IndexOf($old,[StringComparison]::Ordinal)
    if($p -lt 0){throw "R15 JITTER9 anchor missing: $label"}
    $p2=$text.IndexOf($old,$p+$old.Length,[StringComparison]::Ordinal)
    if($p2 -ge 0){throw "R15 JITTER9 anchor not unique: $label"}
    return $text.Substring(0,$p)+$new+$text.Substring($p+$old.Length)
}

# ---------------------------------------------------------------------------
# Runtime state declaration.
# ---------------------------------------------------------------------------
$h=[IO.File]::ReadAllText((Resolve-Path $headerPath)) -replace "`r`n","`n"
if($h.IndexOf('R15 JITTER9 PHASE-EXACT STATE',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 JITTER9 runtime header already patched' }

$h=Replace-One $h '    IDirect3DSurface9* subpixelSurface;' @'
    IDirect3DSurface9* subpixelSurface;

    // R15 JITTER9 PHASE-EXACT STATE
    IDirect3DTexture9* jitterAccumTexture[2];
    IDirect3DSurface9* jitterAccumSurface[2];
'@ 'accum resources'

$h=Replace-One $h '    bool historyValid;' @'
    bool historyValid;
    bool jitterAccumValid;
    UINT jitterAccumReadIndex;
    UINT jitterPhaseClassX;
    UINT jitterPhaseClassY;
    float jitterCurrentX;
    float jitterCurrentY;
    float jitterPreviousX;
    float jitterPreviousY;
'@ 'accum state'

# Normalize the declaration regardless of whether the checkout still carries
# the pre-R8 3-argument declaration or the current four-argument implementation.
$declPattern='(?s)static HRESULT PtTsr1Resolve\(\s*IDirect3DDevice9\* dev,\s*IDirect3DTexture9\* currentSource,.*?IDirect3DSurface9\* resolvedTarget\s*\);'
$declMatches=[regex]::Matches($h,$declPattern)
if($declMatches.Count -ne 1){throw "R15 resolve declaration count=$($declMatches.Count)"}
$h=[regex]::Replace($h,$declPattern,@'
static HRESULT PtTsr1Resolve(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* currentSource,
    IDirect3DTexture9* currentR7,
    IDirect3DSurface9* resolvedTarget);
static void PtTsr1SetJitter9Phase(
    UINT phaseX,UINT phaseY,
    float currentX,float currentY,
    float previousX,float previousY);
'@,1)

# ---------------------------------------------------------------------------
# Runtime implementation.
# ---------------------------------------------------------------------------
$i=[IO.File]::ReadAllText((Resolve-Path $inlPath)) -replace "`r`n","`n"
if($i.IndexOf('R15_JITTER9_PHASE_EXACT_RUNTIME',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 JITTER9 runtime implementation already patched' }

# Lifetime: invalidate/release private 1080p lattice surfaces.
$i=Replace-One $i "    g_ptarTsr1.historyValid=false;`n    g_ptarTsr1.initialized=false;" @'
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.jitterAccumValid=false;
    g_ptarTsr1.jitterAccumReadIndex=0;
    g_ptarTsr1.initialized=false;
'@ 'release state reset'

$i=Replace-One $i '    if(g_ptarTsr1.subpixelSurface){g_ptarTsr1.subpixelSurface->Release();g_ptarTsr1.subpixelSurface=0;}' @'
    for(UINT r15i=0;r15i<2;++r15i)
    {
        if(g_ptarTsr1.jitterAccumSurface[r15i])
        {
            g_ptarTsr1.jitterAccumSurface[r15i]->Release();
            g_ptarTsr1.jitterAccumSurface[r15i]=0;
        }
        if(g_ptarTsr1.jitterAccumTexture[r15i])
        {
            g_ptarTsr1.jitterAccumTexture[r15i]->Release();
            g_ptarTsr1.jitterAccumTexture[r15i]=0;
        }
    }
    if(g_ptarTsr1.subpixelSurface){g_ptarTsr1.subpixelSurface->Release();g_ptarTsr1.subpixelSurface=0;}
'@ 'release phase accumulation'

$invalidatePattern='(?s)static void PtTsr1InvalidateHistory\(\)\s*\{.*?\n\}'
$invalidateMatches=[regex]::Matches($i,$invalidatePattern)
if($invalidateMatches.Count -ne 1){throw "R15 invalidate function count=$($invalidateMatches.Count)"}
$i=[regex]::Replace($i,$invalidatePattern,@'
static void PtTsr1InvalidateHistory()
{
    // Generated frames never enter JITTER9. An FG interval invalidates both
    // source motion history and the private full-resolution phase lattice.
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.jitterAccumValid=false;
    g_ptarTsr1.jitterAccumReadIndex=0;
}

static void PtTsr1SetJitter9Phase(
    UINT phaseX,UINT phaseY,
    float currentX,float currentY,
    float previousX,float previousY)
{
    g_ptarTsr1.jitterPhaseClassX=phaseX%3u;
    g_ptarTsr1.jitterPhaseClassY=phaseY%3u;
    g_ptarTsr1.jitterCurrentX=currentX;
    g_ptarTsr1.jitterCurrentY=currentY;
    g_ptarTsr1.jitterPreviousX=previousX;
    g_ptarTsr1.jitterPreviousY=previousY;
}
'@,1)

# R13's resolve bytecode is deliberately replaced here. R15 no longer tries to
# infer missing frequencies from the same raster; its resolve is the 9-phase
# physical-sample accumulator.
$resolveShaderPattern='(?s)hr=dev->CreatePixelShader\(\s*\(const DWORD\*\)g_ptarTsr1ResolvePs,&g_ptarTsr1\.resolveShader\);\s*if\(FAILED\(hr\) \|\| !g_ptarTsr1\.resolveShader\) goto fail;'
$resolveShaderMatches=[regex]::Matches($i,$resolveShaderPattern)
if($resolveShaderMatches.Count -ne 1){throw "R15 resolve shader creation count=$($resolveShaderMatches.Count)"}
$i=[regex]::Replace($i,$resolveShaderPattern,@'
hr=dev->CreatePixelShader(
        (const DWORD*)g_ptarR15Jitter9AccumPs,&g_ptarTsr1.resolveShader);
    if(FAILED(hr) || !g_ptarTsr1.resolveShader) goto fail;
'@,1)

$subpixelCreate="    hr=CreateRenderTexture(`n        dev,g_ptarTsr1.fineW,g_ptarTsr1.fineH,format,`n        &g_ptarTsr1.subpixelTexture,&g_ptarTsr1.subpixelSurface);`n    if(FAILED(hr)) goto fail;"
$accumCreate=@'

    // Two 1080p phase-lattice surfaces. Alpha stores per-pixel validity.
    for(UINT r15i=0;r15i<2;++r15i)
    {
        hr=CreateRenderTexture(
            dev,outputW,outputH,D3DFMT_A8R8G8B8,
            &g_ptarTsr1.jitterAccumTexture[r15i],
            &g_ptarTsr1.jitterAccumSurface[r15i]);
        if(FAILED(hr)) goto fail;
        hr=dev->ColorFill(g_ptarTsr1.jitterAccumSurface[r15i],0,0x00000000u);
        if(FAILED(hr)) goto fail;
    }
'@
$i=Replace-One $i $subpixelCreate ($subpixelCreate+$accumCreate) 'create phase accumulation surfaces'

$i=Replace-One $i "    g_ptarTsr1.historyValid=false;`n    g_ptarTsr1.initialized=true;" @'
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.jitterAccumValid=false;
    g_ptarTsr1.jitterAccumReadIndex=0;
    g_ptarTsr1.jitterPhaseClassX=0;
    g_ptarTsr1.jitterPhaseClassY=0;
    g_ptarTsr1.jitterCurrentX=0.0f;
    g_ptarTsr1.jitterCurrentY=0.0f;
    g_ptarTsr1.jitterPreviousX=0.0f;
    g_ptarTsr1.jitterPreviousY=0.0f;
    g_ptarTsr1.initialized=true;
'@ 'initialize phase state'

# Replace whole resolve implementation; keep existing ME coarse/refine/subpixel.
$resolveStart=$i.IndexOf('static HRESULT PtTsr1Resolve(',[StringComparison]::Ordinal)
$storeStart=$i.IndexOf('static HRESULT PtTsr1StoreSource(',$resolveStart,[StringComparison]::Ordinal)
if($resolveStart -lt 0 -or $storeStart -lt 0 -or $storeStart -le $resolveStart)
{ throw 'R15 resolve function boundaries missing' }

$newResolve=@'
// R15_JITTER9_PHASE_EXACT_RUNTIME
static HRESULT PtTsr1Resolve(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* currentSource,
    IDirect3DTexture9* currentR7,
    IDirect3DSurface9* resolvedTarget)
{
    if(!dev || !currentSource || !currentR7 || !resolvedTarget || !PtTsr1CanResolve())
        return S_FALSE;

    float source[4]={
        (float)g_ptarTsr1.sourceW,(float)g_ptarTsr1.sourceH,
        1.0f/(float)g_ptarTsr1.sourceW,1.0f/(float)g_ptarTsr1.sourceH};

    PtDiagStage("R15_JITTER9_ME_COARSE");
    HRESULT hr=DrawFullscreenPass(
        dev,g_ptarTsr1.coarseSurface,g_ptarTsr1.coarseW,g_ptarTsr1.coarseH,
        g_ptarTsr1.coarseShader,g_ptarTsr1.sourceHistoryTexture,currentSource,0,source);
    if(FAILED(hr)) goto fail;

    PtDiagStage("R15_JITTER9_ME_REFINE");
    hr=DrawFullscreenPass(
        dev,g_ptarTsr1.fineSurface,g_ptarTsr1.fineW,g_ptarTsr1.fineH,
        g_ptarTsr1.refineShader,g_ptarTsr1.sourceHistoryTexture,currentSource,
        g_ptarTsr1.coarseTexture,source);
    if(FAILED(hr)) goto fail;

    PtDiagStage("R15_JITTER9_ME_SUBPIXEL");
    hr=DrawFullscreenPass(
        dev,g_ptarTsr1.subpixelSurface,g_ptarTsr1.fineW,g_ptarTsr1.fineH,
        g_ptarTsr1.subpixelShader,g_ptarTsr1.sourceHistoryTexture,currentSource,
        g_ptarTsr1.fineTexture,source);
    if(FAILED(hr)) goto fail;

    const UINT writeIndex=g_ptarTsr1.jitterAccumValid?
        (1u-g_ptarTsr1.jitterAccumReadIndex):0u;
    const UINT previousIndex=g_ptarTsr1.jitterAccumValid?
        g_ptarTsr1.jitterAccumReadIndex:1u;

    float sizes[4]={
        (float)g_ptarTsr1.sourceW,(float)g_ptarTsr1.sourceH,
        (float)g_ptarTsr1.outputW,(float)g_ptarTsr1.outputH};
    float phase[4]={
        (float)g_ptarTsr1.jitterPhaseClassX,
        (float)g_ptarTsr1.jitterPhaseClassY,
        g_ptarTsr1.jitterCurrentX,
        g_ptarTsr1.jitterCurrentY};
    float previous[4]={
        g_ptarTsr1.jitterPreviousX,
        g_ptarTsr1.jitterPreviousY,
        g_ptarTsr1.jitterAccumValid?1.0f:0.0f,
        0.0f};

    // s3 is the subpixel current->previous motion field. DrawFullscreenPass owns
    // s0..s2; binding s3 explicitly keeps the generic fullscreen helper frozen.
    dev->SetSamplerState(3,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(3,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    dev->SetSamplerState(3,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(3,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(3,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(3,D3DSAMP_SRGBTEXTURE,FALSE);
    hr=dev->SetTexture(3,g_ptarTsr1.subpixelTexture);
    if(FAILED(hr)) goto fail;
    hr=dev->SetPixelShaderConstantF(1,phase,1);
    if(FAILED(hr)) goto fail;
    hr=dev->SetPixelShaderConstantF(2,previous,1);
    if(FAILED(hr)) goto fail;

    PtDiagStage("R15_JITTER9_PHASE_ACCUM");
    hr=DrawFullscreenPass(
        dev,g_ptarTsr1.jitterAccumSurface[writeIndex],
        g_ptarTsr1.outputW,g_ptarTsr1.outputH,
        g_ptarTsr1.resolveShader,
        currentSource,
        g_ptarTsr1.jitterAccumTexture[previousIndex],
        currentR7,
        sizes);
    dev->SetTexture(3,0);
    if(FAILED(hr)) goto fail;

    // No post-sharpening is allowed after a matching JITTER9 phase: those
    // pixels are already physical samples at the target 1080p lattice.
    PtDiagStage("R15_JITTER9_PRESENT_COPY");
    hr=dev->StretchRect(
        g_ptarTsr1.jitterAccumSurface[writeIndex],0,
        resolvedTarget,0,D3DTEXF_NONE);
    if(FAILED(hr)) goto fail;

    g_ptarTsr1.jitterAccumReadIndex=writeIndex;
    g_ptarTsr1.jitterAccumValid=true;
    return S_OK;

fail:
    dev->SetTexture(3,0);
    PtDiagLogA("R15_JITTER9_RESOLVE_FAIL hr=0x%08lX fallback=R7",(unsigned long)hr);
    return hr;
}

'@
$i=$i.Substring(0,$resolveStart)+$newResolve+$i.Substring($storeStart)

# ---------------------------------------------------------------------------
# Proxy include for the new bytecode. Other R13/FG includes remain intact.
# ---------------------------------------------------------------------------
$p=[IO.File]::ReadAllText((Resolve-Path $proxyPath)) -replace "`r`n","`n"
if($p.IndexOf('ptar_r15_jitter9_accum_ps_bytecode.h',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 JITTER9 proxy bytecode include already present' }
$p=Replace-One $p '#include "ptar_tsr1_resolve_ps_bytecode.h"' @'
#include "ptar_tsr1_resolve_ps_bytecode.h"
#include "ptar_r15_jitter9_accum_ps_bytecode.h"
'@ 'R15 accumulator bytecode include'

foreach($token in @(
    'R15 JITTER9 PHASE-EXACT STATE',
    'jitterAccumTexture[2]',
    'PtTsr1SetJitter9Phase'))
{
    if($h.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    { throw "R15 header token missing: $token" }
}
foreach($token in @(
    'R15_JITTER9_PHASE_EXACT_RUNTIME',
    'g_ptarR15Jitter9AccumPs',
    'R15_JITTER9_PHASE_ACCUM',
    'jitterAccumReadIndex=writeIndex'))
{
    if($i.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    { throw "R15 implementation token missing: $token" }
}

[IO.File]::WriteAllText((Resolve-Path $headerPath),$h,$utf8)
[IO.File]::WriteAllText((Resolve-Path $inlPath),$i,$utf8)
[IO.File]::WriteAllText((Resolve-Path $proxyPath),$p,$utf8)
Write-Host 'R15_JITTER9_TSR_RUNTIME_PATCH=PASS'
Write-Host 'R15_ACCUM=2_X_1920X1080_A8R8G8B8'
Write-Host 'R15_POST_SHARPEN=NONE'
Write-Host 'R15_GENERATED_FRAME_HISTORY=FORBIDDEN'
