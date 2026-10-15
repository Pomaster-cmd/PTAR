$ErrorActionPreference='Stop'

$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$inlPath='compat/x86_d3d9/ptar_tsr1_runtime.inl'
$utf8=New-Object System.Text.UTF8Encoding($false)

function Replace-One([string]$text,[string]$old,[string]$new,[string]$label)
{
    $a=$text.IndexOf($old,[StringComparison]::Ordinal)
    if($a -lt 0){throw "R15 field safety anchor missing: $label"}
    $b=$text.IndexOf($old,$a+$old.Length,[StringComparison]::Ordinal)
    if($b -ge 0){throw "R15 field safety anchor not unique: $label"}
    return $text.Substring(0,$a)+$new+$text.Substring($a+$old.Length)
}

$p=[IO.File]::ReadAllText((Resolve-Path $proxyPath)) -replace "`r`n","`n"
$i=[IO.File]::ReadAllText((Resolve-Path $inlPath)) -replace "`r`n","`n"

if($p.IndexOf('PTAR R15 TRUE720 JITTER9 RUNTIME',[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 field safety requires game sampling runtime first' }
if($p.IndexOf('R15_FIELD_SAFETYFIX1',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 field safety already applied' }
if($i.IndexOf('R15_TRUE720_PHASE_RESERVOIR_RUNTIME',[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 field safety requires phase reservoir runtime first' }

# Track the game's depth-test state. Main-RT draws with depth disabled are very
# commonly HUD/full-screen work and must not receive geometry jitter or the
# native-equivalent texture LOD bias.
$p=Replace-One $p @'
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetSamplerState)(
    IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD);
'@ @'
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderState)(
    IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetSamplerState)(
    IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD);
'@ 'SetRenderState typedef'

$p=Replace-One $p @'
static PFN_SetRenderTarget g_realSetRenderTarget=0;
static PFN_SetSamplerState g_realSetSamplerState=0;
'@ @'
static PFN_SetRenderTarget g_realSetRenderTarget=0;
static PFN_SetRenderState g_realSetRenderState=0;
static PFN_SetSamplerState g_realSetSamplerState=0;
'@ 'SetRenderState pointer'

$p=Replace-One $p @'
    bool mainTarget;
    bool currentVertexShaderPatched;
    UINT phaseIndex;
'@ @'
    bool mainTarget;
    bool currentVertexShaderPatched;
    bool depthEnabled;
    bool lodBiasActive;
    UINT phaseIndex;
'@ 'sampling state guards'

$p=Replace-One $p @'
        !g_ptar.inPresent &&
        g_ptarR15.mainTarget &&
        !PtHudFgEnabled();
'@ @'
        !g_ptar.inPresent &&
        g_ptarR15.mainTarget &&
        g_ptarR15.currentVertexShaderPatched &&
        g_ptarR15.depthEnabled &&
        !PtHudFgEnabled();
'@ 'HUD/depth draw qualification'

$memberFn=@'
static bool PtR15IsPatchedVertexShader(IDirect3DVertexShader9* shader)
{
    if(!shader) return false;
    for(size_t i=0;i<g_ptarR15PatchedShaders.size();++i)
        if(g_ptarR15PatchedShaders[i]==shader) return true;
    return false;
}
'@
$memberPlus=$memberFn+@'

static void PtR15ForgetPatchedVertexShader(IDirect3DVertexShader9* shader)
{
    if(!shader) return;
    for(size_t i=0;i<g_ptarR15PatchedShaders.size();)
    {
        if(g_ptarR15PatchedShaders[i]==shader)
            g_ptarR15PatchedShaders.erase(g_ptarR15PatchedShaders.begin()+i);
        else
            ++i;
    }
}
'@
$p=Replace-One $p $memberFn $memberPlus 'shader membership cleanup helper'

# Do not hold an extra COM reference for the lifetime of the process. A raw
# identity is sufficient while the game owns the shader. Before registering any
# newly-created object, remove a stale equal address so allocator reuse cannot
# turn an unpatched shader into a false positive.
$p=Replace-One $p @'
                (*out)->AddRef();
                g_ptarR15PatchedShaders.push_back(*out);
'@ @'
                PtR15ForgetPatchedVertexShader(*out);
                g_ptarR15PatchedShaders.push_back(*out);
'@ 'remove shader AddRef leak'

$p=Replace-One $p @'
    return g_realCreateVertexShader(self,function,out);
}

static HRESULT STDMETHODCALLTYPE HookSetVertexShader(
'@ @'
    HRESULT originalHr=g_realCreateVertexShader(self,function,out);
    if(SUCCEEDED(originalHr) && out && *out)
        PtR15ForgetPatchedVertexShader(*out);
    return originalHr;
}

static HRESULT STDMETHODCALLTYPE HookSetVertexShader(
'@ 'clean stale identity on original shader creation'

# Avoid sixteen redundant SetSamplerState calls for every VS/RT state touch.
# A full sampler rebias is required only when entering or leaving a qualified
# 3-D main-target draw domain; individual game LOD writes remain handled by the
# existing HookSetSamplerState path.
$lodOld=@'
static void PtR15ApplyLodBiases(IDirect3DDevice9* dev)
{
    if(!dev || !g_realSetSamplerState) return;
    const bool nativeFootprint=PtR15ShouldAffectGameDraw();
    for(DWORD sampler=0;sampler<16;++sampler)
    {
        const DWORD value=nativeFootprint?
            PtR15NativeLodBiasState(g_ptarR15.rawLodBias[sampler]):
            g_ptarR15.rawLodBias[sampler];
        g_realSetSamplerState(dev,sampler,D3DSAMP_MIPMAPLODBIAS,value);
    }
}
'@
$lodNew=@'
static void PtR15ApplyLodBiases(IDirect3DDevice9* dev)
{
    if(!dev || !g_realSetSamplerState) return;
    const bool nativeFootprint=PtR15ShouldAffectGameDraw();
    if(nativeFootprint==g_ptarR15.lodBiasActive) return;
    for(DWORD sampler=0;sampler<16;++sampler)
    {
        const DWORD value=nativeFootprint?
            PtR15NativeLodBiasState(g_ptarR15.rawLodBias[sampler]):
            g_ptarR15.rawLodBias[sampler];
        g_realSetSamplerState(dev,sampler,D3DSAMP_MIPMAPLODBIAS,value);
    }
    g_ptarR15.lodBiasActive=nativeFootprint;
}
'@
$p=Replace-One $p $lodOld $lodNew 'LOD transition cache'

$p=Replace-One $p @'
    g_ptarR15.mainTarget=mainTarget;
    g_ptarR15.currentVertexShaderPatched=false;
    g_ptarR15.phaseIndex=0;
'@ @'
    g_ptarR15.mainTarget=mainTarget;
    g_ptarR15.currentVertexShaderPatched=false;
    g_ptarR15.depthEnabled=true;
    g_ptarR15.lodBiasActive=false;
    g_ptarR15.phaseIndex=0;
'@ 'reset draw qualification state'

# State transition hook: switch jitter/LOD on only while the game's depth test
# is active. D3DZB_USEW is considered enabled; only D3DZB_FALSE disables it.
$p=Replace-One $p @'
static HRESULT STDMETHODCALLTYPE HookSetSamplerState(
'@ @'
static HRESULT STDMETHODCALLTYPE HookSetRenderState(
    IDirect3DDevice9* self,D3DRENDERSTATETYPE state,DWORD value)
{
    if(!g_realSetRenderState)
        return D3DERR_INVALIDCALL;
    HRESULT hr=g_realSetRenderState(self,state,value);
    if(SUCCEEDED(hr) && g_ptar.active && self==g_ptar.device &&
       !g_ptar.inPresent && state==D3DRS_ZENABLE)
    {
        g_ptarR15.depthEnabled=(value!=(DWORD)D3DZB_FALSE);
        PtR15ApplyVertexConstant(self);
        PtR15ApplyLodBiases(self);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetSamplerState(
'@ 'depth render-state hook'

# Critical phase-order fix. The old patch advanced while inPresent was still
# true; PtR15ShouldAffectGameDraw therefore rejected its own next-frame jitter
# and restored c255 instead. Clear inPresent first, then advance and apply the
# next real-frame phase. Every game Present consumes one REAL source frame, so
# advance even if the OS Present result later causes a Reset.
$p=Replace-One $p @'
    self->SetDepthStencilSurface(g_ptar.virtualDepth);
    SetVirtualViewport(self);
    PtR15AdvanceForNextGameFrame(self);

    PtDiagStage("PRESENT_RETURN_TO_GAME");
    g_ptar.inPresent=false;
    return result;
'@ @'
    self->SetDepthStencilSurface(g_ptar.virtualDepth);
    SetVirtualViewport(self);

    PtDiagStage("PRESENT_RETURN_TO_GAME");
    g_ptar.inPresent=false;
    PtR15AdvanceForNextGameFrame(self);
    return result;
'@ 'post-Present phase order'

# Install/capture SetRenderState (device vtable slot 57) before sampler hooks.
$p=Replace-One $p @'
    old=0;
    if(!PatchVtableSlot(dev,69,(void*)&HookSetSamplerState,&old,"Device.SetSamplerState"))
'@ @'
    old=0;
    if(!PatchVtableSlot(dev,57,(void*)&HookSetRenderState,&old,"Device.SetRenderState"))
        return false;
    if(old!=(void*)&HookSetRenderState) g_realSetRenderState=(PFN_SetRenderState)old;

    old=0;
    if(!PatchVtableSlot(dev,69,(void*)&HookSetSamplerState,&old,"Device.SetSamplerState"))
'@ 'SetRenderState vtable hook'

$p=Replace-One $p @'
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];
    g_realSetSamplerState=(PFN_SetSamplerState)vt[69];
'@ @'
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];
    g_realSetRenderState=(PFN_SetRenderState)vt[57];
    g_realSetSamplerState=(PFN_SetSamplerState)vt[69];
'@ 'capture SetRenderState original'

# CreateDevice is the correct generation boundary for the identity cache. Do
# not clear it on Reset because D3D9 vertex shader objects survive Reset.
$p=Replace-One $p @'
    PtR15ResetSamplingState(true);
    PtR15ApplyLodBiases(dev);
    PtDiagLogA(
        "R15_TRUE720_RUNTIME READY jitter=3x3 lodBias=-0.5849625 "
'@ @'
    g_ptarR15PatchedShaders.clear();
    PtR15ResetSamplingState(true);
    PtR15ApplyLodBiases(dev);
    PtDiagLogA(
        "R15_TRUE720_RUNTIME READY jitter=3x3 lodBias=-0.5849625 "
'@ 'CreateDevice shader generation boundary'

# If any R15 resolve pass fails, an older accumulation surface can no longer be
# assumed to be temporally aligned with the source-motion history. Invalidate
# it immediately rather than carrying an apparently-valid stale phase lattice.
$i=Replace-One $i @'
fail:
    dev->SetTexture(3,0);
    PtDiagLogA(
'@ @'
fail:
    dev->SetTexture(3,0);
    g_ptarTsr1.accumValid=false;
    PtDiagLogA(
'@ 'phase-resolve failure invalidation'

# Marker and static invariants for CI/field provenance.
$p=$p.Replace('// PTAR R15 TRUE720 JITTER9 RUNTIME','// PTAR R15 TRUE720 JITTER9 RUNTIME`n// R15_FIELD_SAFETYFIX1')

foreach($token in @(
    'R15_FIELD_SAFETYFIX1',
    'HookSetRenderState',
    'PatchVtableSlot(dev,57',
    'g_ptarR15.depthEnabled',
    'g_ptarR15.lodBiasActive',
    'PtR15ForgetPatchedVertexShader',
    'g_ptar.inPresent=false;`n    PtR15AdvanceForNextGameFrame(self)'))
{
    if($p.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
        {throw "R15 field safety token missing: $token"}
}
if($p.IndexOf('(*out)->AddRef();',[StringComparison]::Ordinal) -ge 0)
{ throw 'R15 field safety: patched shader AddRef leak survived' }
if($i.IndexOf("g_ptarTsr1.accumValid=false;`n    PtDiagLogA(",[StringComparison]::Ordinal) -lt 0)
{ throw 'R15 field safety: resolve failure invalidation missing' }

[IO.File]::WriteAllText((Resolve-Path $proxyPath),$p,$utf8)
[IO.File]::WriteAllText((Resolve-Path $inlPath),$i,$utf8)

Write-Host 'R15_FIELD_SAFETYFIX1=PASS'
Write-Host 'R15_PHASE_ADVANCE_AFTER_INPRESENT_CLEAR=PASS'
Write-Host 'R15_HUD_DEPTH_GUARD=PASS'
Write-Host 'R15_LOD_TRANSITION_CACHE=PASS'
Write-Host 'R15_SHADER_EXTRA_ADDREF=0'
Write-Host 'R15_ACCUM_FAIL_INVALIDATES=PASS'
