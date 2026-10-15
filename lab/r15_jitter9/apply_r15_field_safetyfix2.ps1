$ErrorActionPreference='Stop'
$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$inlPath='compat/x86_d3d9/ptar_tsr1_runtime.inl'
$utf8=New-Object System.Text.UTF8Encoding($false)

function R1([string]$t,[string]$o,[string]$n,[string]$label)
{
    $a=$t.IndexOf($o,[StringComparison]::Ordinal)
    if($a -lt 0){throw "R15 SAFETYFIX2 missing anchor: $label"}
    if($t.IndexOf($o,$a+$o.Length,[StringComparison]::Ordinal) -ge 0){throw "R15 SAFETYFIX2 non-unique anchor: $label"}
    return $t.Substring(0,$a)+$n+$t.Substring($a+$o.Length)
}

$p=[IO.File]::ReadAllText((Resolve-Path $proxyPath)) -replace "`r`n","`n"
$i=[IO.File]::ReadAllText((Resolve-Path $inlPath)) -replace "`r`n","`n"
if($p.IndexOf('PTAR R15 TRUE720 JITTER9 RUNTIME',[StringComparison]::Ordinal) -lt 0){throw 'sampling runtime missing'}
if($i.IndexOf('R15_TRUE720_PHASE_RESERVOIR_RUNTIME',[StringComparison]::Ordinal) -lt 0){throw 'phase runtime missing'}
if($p.IndexOf('R15_FIELD_SAFETYFIX2',[StringComparison]::Ordinal) -ge 0){throw 'SAFETYFIX2 already applied'}

# The R14/R15 generator chain has legitimately changed whitespace around the
# device typedef block several times. Match the SetSamplerState declaration
# structurally, not byte-for-byte, and insert SetRenderState immediately before
# it. This keeps the patch strict (exactly one structural match) without tying
# field packaging to one formatting revision.
if($p.IndexOf('typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderState)',[StringComparison]::Ordinal) -lt 0)
{
    $typedefPattern='(?s)typedef\s+HRESULT\s*\(STDMETHODCALLTYPE\s+\*PFN_SetSamplerState\)\s*\(\s*IDirect3DDevice9\*,\s*DWORD,\s*D3DSAMPLERSTATETYPE,\s*DWORD\s*\);'
    $typedefMatches=[regex]::Matches($p,$typedefPattern)
    if($typedefMatches.Count -ne 1){throw "R15 SAFETYFIX2 render-state typedef match count=$($typedefMatches.Count)"}
    $renderStateTypedef=@'
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderState)(
    IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD);
'@
    $m=$typedefMatches[0]
    $p=$p.Substring(0,$m.Index)+$renderStateTypedef+"`n"+$m.Value+$p.Substring($m.Index+$m.Length)
}

if($p.IndexOf('static PFN_SetRenderState g_realSetRenderState=0;',[StringComparison]::Ordinal) -lt 0)
{
    $p=R1 $p 'static PFN_SetRenderTarget g_realSetRenderTarget=0;' @'
static PFN_SetRenderTarget g_realSetRenderTarget=0;
static PFN_SetRenderState g_realSetRenderState=0;
'@ 'render-state pointer'
}

$p=R1 $p @'
    bool mainTarget;
    bool currentVertexShaderPatched;
    UINT phaseIndex;
'@ @'
    bool mainTarget;
    bool currentVertexShaderPatched;
    bool depthEnabled;
    bool lodBiasActive;
    UINT phaseIndex;
'@ 'state guards'

$p=R1 $p @'
        !g_ptar.inPresent &&
        g_ptarR15.mainTarget &&
        !PtHudFgEnabled();
'@ @'
        !g_ptar.inPresent &&
        g_ptarR15.mainTarget &&
        g_ptarR15.currentVertexShaderPatched &&
        g_ptarR15.depthEnabled &&
        !PtHudFgEnabled();
'@ '3D qualification'

$member=@'
static bool PtR15IsPatchedVertexShader(IDirect3DVertexShader9* shader)
{
    if(!shader) return false;
    for(size_t i=0;i<g_ptarR15PatchedShaders.size();++i)
        if(g_ptarR15PatchedShaders[i]==shader) return true;
    return false;
}
'@
$p=R1 $p $member ($member+@'

static void PtR15ForgetPatchedVertexShader(IDirect3DVertexShader9* shader)
{
    if(!shader) return;
    for(size_t i=0;i<g_ptarR15PatchedShaders.size();)
    {
        if(g_ptarR15PatchedShaders[i]==shader)
            g_ptarR15PatchedShaders.erase(g_ptarR15PatchedShaders.begin()+i);
        else ++i;
    }
}
'@) 'shader identity helper'

$p=R1 $p @'
                (*out)->AddRef();
                g_ptarR15PatchedShaders.push_back(*out);
'@ @'
                PtR15ForgetPatchedVertexShader(*out);
                g_ptarR15PatchedShaders.push_back(*out);
'@ 'remove shader ownership leak'

$p=R1 $p @'
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
'@ 'original shader stale-identity cleanup'

$lod=@'
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
$p=R1 $p $lod @'
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
'@ 'LOD transition cache'

$p=R1 $p @'
    g_ptarR15.mainTarget=mainTarget;
    g_ptarR15.currentVertexShaderPatched=false;
    g_ptarR15.phaseIndex=0;
'@ @'
    g_ptarR15.mainTarget=mainTarget;
    g_ptarR15.currentVertexShaderPatched=false;
    g_ptarR15.depthEnabled=true;
    g_ptarR15.lodBiasActive=false;
    g_ptarR15.phaseIndex=0;
'@ 'reset guards'

$p=R1 $p 'static HRESULT STDMETHODCALLTYPE HookSetSamplerState(' @'
static HRESULT STDMETHODCALLTYPE HookSetRenderState(
    IDirect3DDevice9* self,D3DRENDERSTATETYPE state,DWORD value)
{
    if(!g_realSetRenderState) return D3DERR_INVALIDCALL;
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
'@ 'depth-state hook'

$p=R1 $p @'
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
'@ 'real-frame phase order'

$p=R1 $p @'
    old=0;
    if(!PatchVtableSlot(dev,69,(void*)&HookSetSamplerState,&old,"Device.SetSamplerState"))
'@ @'
    old=0;
    if(!PatchVtableSlot(dev,57,(void*)&HookSetRenderState,&old,"Device.SetRenderState"))
        return false;
    if(old!=(void*)&HookSetRenderState) g_realSetRenderState=(PFN_SetRenderState)old;

    old=0;
    if(!PatchVtableSlot(dev,69,(void*)&HookSetSamplerState,&old,"Device.SetSamplerState"))
'@ 'render-state vtable hook'

$p=R1 $p @'
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];
    g_realSetSamplerState=(PFN_SetSamplerState)vt[69];
'@ @'
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];
    g_realSetRenderState=(PFN_SetRenderState)vt[57];
    g_realSetSamplerState=(PFN_SetSamplerState)vt[69];
'@ 'render-state original capture'

$p=R1 $p @'
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
'@ 'device-generation identity reset'

$i=R1 $i @'
fail:
    dev->SetTexture(3,0);
    PtDiagLogA(
'@ @'
fail:
    dev->SetTexture(3,0);
    g_ptarTsr1.accumValid=false;
    PtDiagLogA(
'@ 'stale accumulation invalidation'

$p=$p.Replace('// PTAR R15 TRUE720 JITTER9 RUNTIME',"// PTAR R15 TRUE720 JITTER9 RUNTIME`n// R15_FIELD_SAFETYFIX2")

$advance="g_ptar.inPresent=false;`n    PtR15AdvanceForNextGameFrame(self);"
foreach($token in @('R15_FIELD_SAFETYFIX2','HookSetRenderState','PatchVtableSlot(dev,57','g_ptarR15.depthEnabled','g_ptarR15.lodBiasActive','PtR15ForgetPatchedVertexShader',$advance))
{
    if($p.IndexOf($token,[StringComparison]::Ordinal) -lt 0){throw "R15 SAFETYFIX2 missing final token: $token"}
}
if($p.IndexOf('(*out)->AddRef();',[StringComparison]::Ordinal) -ge 0){throw 'R15 SAFETYFIX2 leaked shader AddRef survived'}
if($i.IndexOf("g_ptarTsr1.accumValid=false;`n    PtDiagLogA(",[StringComparison]::Ordinal) -lt 0){throw 'R15 SAFETYFIX2 fail invalidation missing'}

[IO.File]::WriteAllText((Resolve-Path $proxyPath),$p,$utf8)
[IO.File]::WriteAllText((Resolve-Path $inlPath),$i,$utf8)
Write-Host 'R15_FIELD_SAFETYFIX2=PASS'
Write-Host 'R15_PHASE_ADVANCE_AFTER_INPRESENT_CLEAR=PASS'
Write-Host 'R15_HUD_DEPTH_GUARD=PASS'
Write-Host 'R15_LOD_TRANSITION_CACHE=PASS'
Write-Host 'R15_SHADER_EXTRA_ADDREF=0'
Write-Host 'R15_ACCUM_FAIL_INVALIDATES=PASS'
