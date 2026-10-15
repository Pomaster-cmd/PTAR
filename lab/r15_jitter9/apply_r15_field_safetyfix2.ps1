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

# Insert the render-state type structurally so the field package is independent
# of harmless formatting changes made by the preceding runtime generators.
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

# Patch PtR15SamplingState semantically rather than relying on field adjacency.
if($p.IndexOf('bool depthEnabled;',[StringComparison]::Ordinal) -lt 0 -or
   $p.IndexOf('bool lodBiasActive;',[StringComparison]::Ordinal) -lt 0)
{
    $stateStart=$p.IndexOf('struct PtR15SamplingState',[StringComparison]::Ordinal)
    $stateEnd=$p.IndexOf('};',$stateStart,[StringComparison]::Ordinal)
    if($stateStart -lt 0 -or $stateEnd -lt 0){throw 'R15 SAFETYFIX2 sampling-state boundaries missing'}
    $stateBody=$p.Substring($stateStart,$stateEnd-$stateStart)
    $fieldPattern='(?m)^[ \t]*bool\s+currentVertexShaderPatched\s*;[ \t]*$'
    $fieldMatches=[regex]::Matches($stateBody,$fieldPattern)
    if($fieldMatches.Count -ne 1){throw "R15 SAFETYFIX2 sampling-state patched-VS field count=$($fieldMatches.Count)"}
    $m=$fieldMatches[0]
    $fieldInsert=$m.Value+"`n    bool depthEnabled;`n    bool lodBiasActive;"
    $stateBody=$stateBody.Substring(0,$m.Index)+$fieldInsert+$stateBody.Substring($m.Index+$m.Length)
    $p=$p.Substring(0,$stateStart)+$stateBody+$p.Substring($stateEnd)
}

# Jitter/LOD may affect only a qualified, depth-enabled 3-D draw using a shader
# for which the position-output patch actually succeeded.
$qualStart=$p.IndexOf('static bool PtR15ShouldAffectGameDraw()',[StringComparison]::Ordinal)
$qualEnd=$p.IndexOf('static bool PtR15IsPatchedVertexShader',$qualStart,[StringComparison]::Ordinal)
if($qualStart -lt 0 -or $qualEnd -lt 0 -or $qualEnd -le $qualStart){throw 'R15 SAFETYFIX2 3D qualification boundaries missing'}
$qualBody=$p.Substring($qualStart,$qualEnd-$qualStart)
if($qualBody.IndexOf('g_ptarR15.currentVertexShaderPatched',[StringComparison]::Ordinal) -lt 0 -or
   $qualBody.IndexOf('g_ptarR15.depthEnabled',[StringComparison]::Ordinal) -lt 0)
{
    $hudPattern='(?m)^(\s*)!PtHudFgEnabled\(\);\s*$'
    $hudMatches=[regex]::Matches($qualBody,$hudPattern)
    if($hudMatches.Count -ne 1){throw "R15 SAFETYFIX2 3D qualification HUD term count=$($hudMatches.Count)"}
    $m=$hudMatches[0]
    $indent=$m.Groups[1].Value
    $guardInsert=$indent+'g_ptarR15.currentVertexShaderPatched &&'+"`n"+
        $indent+'g_ptarR15.depthEnabled &&'+"`n"+
        $indent+'!PtHudFgEnabled();'
    $qualBody=$qualBody.Substring(0,$m.Index)+$guardInsert+$qualBody.Substring($m.Index+$m.Length)
    $p=$p.Substring(0,$qualStart)+$qualBody+$p.Substring($qualEnd)
}

# The shader identity vector is non-owning. It exists only to recognize the
# patched shader pointer selected by SetVertexShader. Never hold an extra COM
# reference; scrub stale pointer identities before recording/reusing an address.
if($p.IndexOf('static void PtR15ForgetPatchedVertexShader(',[StringComparison]::Ordinal) -lt 0)
{
    $memberStart=$p.IndexOf('static bool PtR15IsPatchedVertexShader(',[StringComparison]::Ordinal)
    $memberEnd=$p.IndexOf('static void PtR15BuildJitterConstant',$memberStart,[StringComparison]::Ordinal)
    if($memberStart -lt 0 -or $memberEnd -lt 0 -or $memberEnd -le $memberStart)
        {throw 'R15 SAFETYFIX2 shader identity helper boundaries missing'}
    $forget=@'
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

'@
    $p=$p.Substring(0,$memberEnd)+$forget+$p.Substring($memberEnd)
}

# Patch HookCreateVertexShader inside its own boundaries. The old field script
# assumed exact whitespace around AddRef/push_back; runtime-generation changes
# proved that brittle. The semantic contract is stronger: exactly one extra
# AddRef and exactly one patched-shader insertion must exist in this function.
$createStart=$p.IndexOf('static HRESULT STDMETHODCALLTYPE HookCreateVertexShader(',[StringComparison]::Ordinal)
$createEnd=$p.IndexOf('static HRESULT STDMETHODCALLTYPE HookSetVertexShader(',$createStart,[StringComparison]::Ordinal)
if($createStart -lt 0 -or $createEnd -lt 0 -or $createEnd -le $createStart)
    {throw 'R15 SAFETYFIX2 CreateVertexShader boundaries missing'}
$createBody=$p.Substring($createStart,$createEnd-$createStart)

$addRefPattern='(?m)^[ \t]*\(\*out\)->AddRef\(\);[ \t]*(?:\n|$)'
$addRefs=[regex]::Matches($createBody,$addRefPattern)
if($addRefs.Count -ne 1){throw "R15 SAFETYFIX2 CreateVertexShader extra-AddRef count=$($addRefs.Count)"}
$m=$addRefs[0]
$createBody=$createBody.Substring(0,$m.Index)+$createBody.Substring($m.Index+$m.Length)

$pushPattern='(?m)^([ \t]*)g_ptarR15PatchedShaders\.push_back\(\*out\);[ \t]*$'
$pushes=[regex]::Matches($createBody,$pushPattern)
if($pushes.Count -ne 1){throw "R15 SAFETYFIX2 patched-shader push count=$($pushes.Count)"}
$m=$pushes[0]
$indent=$m.Groups[1].Value
$pushReplacement=$indent+'PtR15ForgetPatchedVertexShader(*out);'+"`n"+$m.Value
$createBody=$createBody.Substring(0,$m.Index)+$pushReplacement+$createBody.Substring($m.Index+$m.Length)

# The early invalid-argument fallback may legitimately call the original
# CreateVertexShader too. Only rewrite the LAST fallback in this hook, which is
# the normal unpatched-shader return path immediately before HookSetVertexShader.
$fallbackPattern='(?m)^([ \t]*)return\s+g_realCreateVertexShader\(self,function,out\);[ \t]*$'
$fallbacks=[regex]::Matches($createBody,$fallbackPattern)
if($fallbacks.Count -lt 1){throw 'R15 SAFETYFIX2 original CreateVertexShader fallback missing'}
$m=$fallbacks[$fallbacks.Count-1]
$indent=$m.Groups[1].Value
$fallbackReplacement=$indent+'HRESULT originalHr=g_realCreateVertexShader(self,function,out);'+"`n"+
    $indent+'if(SUCCEEDED(originalHr) && out && *out)'+"`n"+
    $indent+'    PtR15ForgetPatchedVertexShader(*out);'+"`n"+
    $indent+'return originalHr;'
$createBody=$createBody.Substring(0,$m.Index)+$fallbackReplacement+$createBody.Substring($m.Index+$m.Length)
$p=$p.Substring(0,$createStart)+$createBody+$p.Substring($createEnd)

# LOD bias becomes a transition-cached state, avoiding 16 redundant sampler
# writes on every draw while still restoring the game's raw state on domain exit.
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

$resetStart=$p.IndexOf('static void PtR15ResetSamplingState(bool mainTarget)',[StringComparison]::Ordinal)
$resetEnd=$p.IndexOf('static void PtR15AdvanceForNextGameFrame',$resetStart,[StringComparison]::Ordinal)
if($resetStart -lt 0 -or $resetEnd -lt 0 -or $resetEnd -le $resetStart){throw 'R15 SAFETYFIX2 reset-function boundaries missing'}
$resetBody=$p.Substring($resetStart,$resetEnd-$resetStart)
if($resetBody.IndexOf('g_ptarR15.depthEnabled=true;',[StringComparison]::Ordinal) -lt 0)
{
    $resetNeedle='    g_ptarR15.currentVertexShaderPatched=false;'
    $r=$resetBody.IndexOf($resetNeedle,[StringComparison]::Ordinal)
    if($r -lt 0 -or $resetBody.IndexOf($resetNeedle,$r+$resetNeedle.Length,[StringComparison]::Ordinal) -ge 0)
        {throw 'R15 SAFETYFIX2 reset patched-VS assignment not unique'}
    $resetInsert=$resetNeedle+"`n    g_ptarR15.depthEnabled=true;`n    g_ptarR15.lodBiasActive=false;"
    $resetBody=$resetBody.Substring(0,$r)+$resetInsert+$resetBody.Substring($r+$resetNeedle.Length)
    $p=$p.Substring(0,$resetStart)+$resetBody+$p.Substring($resetEnd)
}

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