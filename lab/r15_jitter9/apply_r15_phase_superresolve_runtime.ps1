$ErrorActionPreference='Stop'

$headerPath='compat/x86_d3d9/ptar_tsr1_runtime.h'
$inlPath='compat/x86_d3d9/ptar_tsr1_runtime.inl'
$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$utf8=New-Object System.Text.UTF8Encoding($false)

function Replace-One([string]$text,[string]$old,[string]$new,[string]$label)
{
    $p=$text.IndexOf($old,[StringComparison]::Ordinal)
    if($p -lt 0){throw "R15 phase runtime anchor missing: $label"}
    $p2=$text.IndexOf($old,$p+$old.Length,[StringComparison]::Ordinal)
    if($p2 -ge 0){throw "R15 phase runtime anchor not unique: $label"}
    return $text.Substring(0,$p)+$new+$text.Substring($p+$old.Length)
}

# ---------------------------------------------------------------------------
# R14 already owns exactly two private 1920x1080 accumulation surfaces. Reuse
# them as the R15 phase reservoir rather than allocating another full-res pair.
# ---------------------------------------------------------------------------
$h=[IO.File]::ReadAllText((Resolve-Path $headerPath)) -replace "`r`n","`n"
if($h.IndexOf('R15 TRUE720 PHASE RESERVOIR STATE',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 phase reservoir header already patched' }
if($h.IndexOf('R14 MFSR1 ACCUM1080 STATE',[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 phase reservoir requires R14 runtime state first' }

$h=Replace-One $h '    UINT accumReadIndex;' @'
    UINT accumReadIndex;

    // R15 TRUE720 PHASE RESERVOIR STATE
    UINT jitterResidueX;
    UINT jitterResidueY;
    float jitterCurrentX;
    float jitterCurrentY;
    float jitterPreviousX;
    float jitterPreviousY;
'@ 'R15 phase state fields'

$declPattern='(?s)static HRESULT PtTsr1Resolve\(\s*IDirect3DDevice9\* dev,\s*IDirect3DTexture9\* currentSource,\s*IDirect3DTexture9\* currentR7,\s*IDirect3DSurface9\* resolvedTarget\);'
$declMatches=[regex]::Matches($h,$declPattern)
if($declMatches.Count -ne 1){throw "R15 phase resolve declaration count=$($declMatches.Count)"}
$decl=$declMatches[0].Value
$declPlus=$decl+@'

static void PtTsr1SetJitter9Phase(
    UINT residueX,UINT residueY,
    float currentX,float currentY,
    float previousX,float previousY);
'@
$h=$h.Substring(0,$declMatches[0].Index)+$declPlus+$h.Substring($declMatches[0].Index+$declMatches[0].Length)

# ---------------------------------------------------------------------------
# Replace R14's generic temporal accumulation resolve with deterministic phase
# ownership. Motion estimation remains unchanged and R14's two surfaces are
# reused in place. The R14 restore shader stays allocated as an inert fail-safe
# resource at this lab stage; it is not executed after an exact owned sample.
# ---------------------------------------------------------------------------
$i=[IO.File]::ReadAllText((Resolve-Path $inlPath)) -replace "`r`n","`n"
if($i.IndexOf('R15_TRUE720_PHASE_RESERVOIR_RUNTIME',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 phase reservoir implementation already patched' }
if($i.IndexOf('R14_MFSR1_RUNTIME',[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 phase reservoir requires R14 runtime implementation first' }

$shaderOld='(const DWORD*)g_ptarTsr1ResolvePs,&g_ptarTsr1.resolveShader'
$shaderNew='(const DWORD*)g_ptarR15PhaseAccumPs,&g_ptarTsr1.resolveShader'
$i=Replace-One $i $shaderOld $shaderNew 'phase accumulator shader creation'

$invalidatePattern='(?s)static void PtTsr1InvalidateHistory\(\)\s*\{.*?\n\}'
$invalidateMatches=[regex]::Matches($i,$invalidatePattern)
if($invalidateMatches.Count -ne 1){throw "R15 phase invalidate function count=$($invalidateMatches.Count)"}
$i=[regex]::Replace($i,$invalidatePattern,@'
static void PtTsr1InvalidateHistory()
{
    // Generated frames never enter R15. Any FG interval invalidates both the
    // source-motion history and the private 1080p physical-sample reservoir.
    g_ptarTsr1.historyValid=false;
    g_ptarTsr1.accumValid=false;
    g_ptarTsr1.accumReadIndex=0;
}

static void PtTsr1SetJitter9Phase(
    UINT residueX,UINT residueY,
    float currentX,float currentY,
    float previousX,float previousY)
{
    g_ptarTsr1.jitterResidueX=residueX%3u;
    g_ptarTsr1.jitterResidueY=residueY%3u;
    g_ptarTsr1.jitterCurrentX=currentX;
    g_ptarTsr1.jitterCurrentY=currentY;
    g_ptarTsr1.jitterPreviousX=previousX;
    g_ptarTsr1.jitterPreviousY=previousY;
}
'@,1)

$i=Replace-One $i "    g_ptarTsr1.accumReadIndex=0;`n    g_ptarTsr1.initialized=true;" @'
    g_ptarTsr1.accumReadIndex=0;
    g_ptarTsr1.jitterResidueX=0;
    g_ptarTsr1.jitterResidueY=0;
    g_ptarTsr1.jitterCurrentX=0.0f;
    g_ptarTsr1.jitterCurrentY=0.0f;
    g_ptarTsr1.jitterPreviousX=0.0f;
    g_ptarTsr1.jitterPreviousY=0.0f;
    g_ptarTsr1.initialized=true;
'@ 'initialize calibrated phase state'

$resolveStart=$i.IndexOf('static HRESULT PtTsr1Resolve(',[StringComparison]::Ordinal)
$storeStart=$i.IndexOf('static HRESULT PtTsr1StoreSource(',$resolveStart,[StringComparison]::Ordinal)
if($resolveStart -lt 0 -or $storeStart -lt 0 -or $storeStart -le $resolveStart)
{ throw 'R15 phase resolve function boundaries missing' }

$newResolve=@'
// R15_TRUE720_PHASE_RESERVOIR_RUNTIME
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

    PtDiagStage("R15_PHASE_ME_COARSE");
    HRESULT hr=DrawFullscreenPass(
        dev,g_ptarTsr1.coarseSurface,g_ptarTsr1.coarseW,g_ptarTsr1.coarseH,
        g_ptarTsr1.coarseShader,g_ptarTsr1.sourceHistoryTexture,currentSource,0,source);
    if(FAILED(hr)) goto fail;

    PtDiagStage("R15_PHASE_ME_REFINE");
    hr=DrawFullscreenPass(
        dev,g_ptarTsr1.fineSurface,g_ptarTsr1.fineW,g_ptarTsr1.fineH,
        g_ptarTsr1.refineShader,g_ptarTsr1.sourceHistoryTexture,currentSource,
        g_ptarTsr1.coarseTexture,source);
    if(FAILED(hr)) goto fail;

    PtDiagStage("R15_PHASE_ME_SUBPIXEL");
    hr=DrawFullscreenPass(
        dev,g_ptarTsr1.subpixelSurface,g_ptarTsr1.fineW,g_ptarTsr1.fineH,
        g_ptarTsr1.subpixelShader,g_ptarTsr1.sourceHistoryTexture,currentSource,
        g_ptarTsr1.fineTexture,source);
    if(FAILED(hr)) goto fail;

    const UINT writeIndex=g_ptarTsr1.accumValid?
        (1u-g_ptarTsr1.accumReadIndex):0u;
    const UINT previousIndex=g_ptarTsr1.accumValid?
        g_ptarTsr1.accumReadIndex:1u;

    float sizes[4]={
        (float)g_ptarTsr1.sourceW,(float)g_ptarTsr1.sourceH,
        (float)g_ptarTsr1.outputW,(float)g_ptarTsr1.outputH};
    float phase[4]={
        (float)g_ptarTsr1.jitterResidueX,
        (float)g_ptarTsr1.jitterResidueY,
        g_ptarTsr1.jitterCurrentX,
        g_ptarTsr1.jitterCurrentY};
    float previous[4]={
        g_ptarTsr1.jitterPreviousX,
        g_ptarTsr1.jitterPreviousY,
        g_ptarTsr1.accumValid?1.0f:0.0f,
        0.0f};

    // s3 is the subpixel current->previous motion field. DrawFullscreenPass
    // configures s0..s2 and leaves this explicit binding intact.
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

    PtDiagStage("R15_PHASE_EXACT_ACCUM");
    hr=DrawFullscreenPass(
        dev,g_ptarTsr1.accumSurface[writeIndex],
        g_ptarTsr1.outputW,g_ptarTsr1.outputH,
        g_ptarTsr1.resolveShader,
        currentSource,
        g_ptarTsr1.accumTexture[previousIndex],
        currentR7,
        sizes);
    dev->SetTexture(3,0);
    if(FAILED(hr)) goto fail;

    // Do not sharpen a matching physical sample. Copy the reservoir byte-for-
    // byte into the existing TSR display scratch. R7/R13 remains the fail-soft
    // image for invalid/unowned pixels inside the accumulator shader itself.
    PtDiagStage("R15_PHASE_PRESENT_COPY");
    hr=dev->StretchRect(
        g_ptarTsr1.accumSurface[writeIndex],0,
        resolvedTarget,0,D3DTEXF_NONE);
    if(FAILED(hr)) goto fail;

    g_ptarTsr1.accumReadIndex=writeIndex;
    g_ptarTsr1.accumValid=true;
    return S_OK;

fail:
    dev->SetTexture(3,0);
    PtDiagLogA(
        "R15_PHASE_RESOLVE_FAIL hr=0x%08lX fallback=R7",
        (unsigned long)hr);
    return hr;
}

'@
$i=$i.Substring(0,$resolveStart)+$newResolve+$i.Substring($storeStart)

# ---------------------------------------------------------------------------
# Proxy: include the new bytecode and bridge the calibrated game sampling phase
# into the TSR reservoir immediately before each REAL resolve.
# ---------------------------------------------------------------------------
$p=[IO.File]::ReadAllText((Resolve-Path $proxyPath)) -replace "`r`n","`n"
if($p.IndexOf('ptar_r15_phase_accum_ps_bytecode.h',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 phase accumulator include already present' }
if($p.IndexOf('PTAR R15 TRUE720 JITTER9 RUNTIME',[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 phase reservoir requires calibrated game sampling runtime first' }

$p=Replace-One $p '#include "ptar_r14_restore_ps_bytecode.h"' @'
#include "ptar_r14_restore_ps_bytecode.h"
#include "ptar_r15_phase_accum_ps_bytecode.h"
'@ 'R15 phase bytecode include'

# The sampling-runtime generator has changed whitespace/schedule formatting
# during R15 calibration. Match the function structurally instead of requiring
# byte-identical formatting; still require exactly one function before editing.
$axisPattern='(?s)static float PtR15AxisJitter\(int phaseClass\)\s*\{.*?return\s+0\.0f;\s*\}'
$axisMatches=[regex]::Matches($p,$axisPattern)
if($axisMatches.Count -ne 1)
{ throw "R15 phase runtime PtR15AxisJitter count=$($axisMatches.Count)" }
$axisFunction=$axisMatches[0].Value
$axisPlus=$axisFunction+@'

static UINT PtR15AxisResidue(int phaseClass)
{
    // Calibrated D3D9 integer-raster-center ownership.
    if(phaseClass==0) return 1u;
    if(phaseClass==2) return 2u;
    return 0u;
}
'@
$p=$p.Substring(0,$axisMatches[0].Index)+$axisPlus+$p.Substring($axisMatches[0].Index+$axisMatches[0].Length)

$resolveCall='            HRESULT tsrHr=PtTsr1Resolve('
$phaseBridge=@'
            const UINT r15Phase=g_ptarR15.phaseIndex%9u;
            const UINT r15PreviousPhase=(r15Phase+8u)%9u;
            const int r15Px=g_ptarR15PhaseSchedule[r15Phase][0];
            const int r15Py=g_ptarR15PhaseSchedule[r15Phase][1];
            const int r15PrevPx=g_ptarR15PhaseSchedule[r15PreviousPhase][0];
            const int r15PrevPy=g_ptarR15PhaseSchedule[r15PreviousPhase][1];
            PtTsr1SetJitter9Phase(
                PtR15AxisResidue(r15Px),PtR15AxisResidue(r15Py),
                PtR15AxisJitter(r15Px),PtR15AxisJitter(r15Py),
                PtR15AxisJitter(r15PrevPx),PtR15AxisJitter(r15PrevPy));

            HRESULT tsrHr=PtTsr1Resolve(
'@
$p=Replace-One $p $resolveCall $phaseBridge 'phase bridge before TSR resolve'

foreach($token in @(
    'R15 TRUE720 PHASE RESERVOIR STATE',
    'jitterResidueX',
    'PtTsr1SetJitter9Phase'))
{
    if($h.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    { throw "R15 phase header token missing: $token" }
}
foreach($token in @(
    'R15_TRUE720_PHASE_RESERVOIR_RUNTIME',
    'g_ptarR15PhaseAccumPs',
    'R15_PHASE_EXACT_ACCUM',
    'R15_PHASE_PRESENT_COPY',
    'known'))
{
    if($i.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    { throw "R15 phase implementation token missing: $token" }
}
foreach($token in @(
    'ptar_r15_phase_accum_ps_bytecode.h',
    'PtR15AxisResidue',
    'PtTsr1SetJitter9Phase(',
    'r15PreviousPhase'))
{
    if($p.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    { throw "R15 phase proxy token missing: $token" }
}

# Explicit safety gates: no extra 1080p history pair and no generated-frame SR
# history contamination are allowed in this overlay.
if($h.IndexOf('jitterAccumTexture',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 phase overlay allocated redundant full-resolution history' }
if($p.IndexOf('g_ptar.currentRealTexture=g_ptar.generatedTexture',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 phase overlay contaminates qualified FG history' }

[IO.File]::WriteAllText((Resolve-Path $headerPath),$h,$utf8)
[IO.File]::WriteAllText((Resolve-Path $inlPath),$i,$utf8)
[IO.File]::WriteAllText((Resolve-Path $proxyPath),$p,$utf8)

Write-Host 'R15_PHASE_SUPERRESOLVE_RUNTIME_PATCH=PASS'
Write-Host 'R15_PHASE_RESERVOIR_SURFACES=REUSE_R14_2X_1920X1080_A8R8G8B8'
Write-Host 'R15_PHASE_CLASS_TO_RESIDUE=0_TO_1__1_TO_0__2_TO_2'
Write-Host 'R15_PHASE_HISTORY=REAL_FRAMES_ONLY'
Write-Host 'R15_PHASE_POST_SHARPEN=NONE_ON_OWNED_SAMPLES'
Write-Host 'R15_PHASE_FG_SCOPE=OFF_ONLY'
