$ErrorActionPreference='Stop'

$proxyPath='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$p=[IO.File]::ReadAllText((Resolve-Path $proxyPath)) -replace "`r`n","`n"

function Replace-One([string]$text,[string]$old,[string]$new,[string]$label)
{
    $a=$text.IndexOf($old,[StringComparison]::Ordinal)
    if($a -lt 0){throw "R15 runtime anchor missing: $label"}
    $b=$text.IndexOf($old,$a+$old.Length,[StringComparison]::Ordinal)
    if($b -ge 0){throw "R15 runtime anchor not unique: $label"}
    return $text.Substring(0,$a)+$new+$text.Substring($a+$old.Length)
}

if($p.IndexOf('PTAR R15 TRUE720 JITTER9 RUNTIME',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R15 runtime already applied'
}

# The production build copies the validated patcher header into build_x86.
$includeAnchor='#include "ptar_gw16i_hud_d3d9.h"'
$p=Replace-One $p $includeAnchor ($includeAnchor+"`n"+'#include "ptar_r15_vs_jitter_patch.h"') 'jitter patcher include'

$typedefAnchor=@'
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderTarget)(
    IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
'@
$typedefInsert=@'
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderTarget)(
    IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetSamplerState)(
    IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateVertexShader)(
    IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetVertexShader)(
    IDirect3DDevice9*,IDirect3DVertexShader9*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetVertexShaderConstantF)(
    IDirect3DDevice9*,UINT,const float*,UINT);
'@
$p=Replace-One $p $typedefAnchor $typedefInsert 'R15 device typedefs'

$pointerAnchor='static PFN_SetRenderTarget g_realSetRenderTarget=0;'
$pointerInsert=@'
static PFN_SetRenderTarget g_realSetRenderTarget=0;
static PFN_SetSamplerState g_realSetSamplerState=0;
static PFN_CreateVertexShader g_realCreateVertexShader=0;
static PFN_SetVertexShader g_realSetVertexShader=0;
static PFN_SetVertexShaderConstantF g_realSetVertexShaderConstantF=0;
'@
$p=Replace-One $p $pointerAnchor $pointerInsert 'R15 original function pointers'

$stateAnchor='static PTARContext g_ptar={};'
$stateInsert=@'
static PTARContext g_ptar={};

// PTAR R15 TRUE720 JITTER9 RUNTIME
// Deliberate 3x3 subpixel sampling is applied ONLY to the game's main 720p
// render target. Offscreen/shadow-map passes retain their original projection.
// A native-equivalent mip bias is also applied only in the main source domain.
struct PtR15SamplingState
{
    bool mainTarget;
    bool currentVertexShaderPatched;
    UINT phaseIndex;
    float gameC255[4];
    DWORD rawLodBias[16];
    unsigned long patchedShaderCount;
    unsigned long rejectedShaderCount;
};

static PtR15SamplingState g_ptarR15={};
static std::vector<IDirect3DVertexShader9*> g_ptarR15PatchedShaders;

static const int g_ptarR15PhaseSchedule[9][2]={
    {1,1},{2,1},{2,2},{1,2},{0,2},{0,1},{0,0},{1,0},{2,0}
};

static float PtR15AxisJitter(int phaseClass)
{
    if(phaseClass==0) return -1.0f/3.0f;
    if(phaseClass==2) return  1.0f/3.0f;
    return 0.0f;
}

static bool PtR15Exact720To1080()
{
    return
        g_ptar.active && g_ptar.spatialActive &&
        g_ptar.sourceW==1280 && g_ptar.sourceH==720 &&
        g_ptar.outputW==1920 && g_ptar.outputH==1080;
}

static bool PtR15ShouldAffectGameDraw()
{
    return
        PtR15Exact720To1080() &&
        !g_ptar.inPresent &&
        g_ptarR15.mainTarget &&
        !PtHudFgEnabled();
}

static bool PtR15IsPatchedVertexShader(IDirect3DVertexShader9* shader)
{
    if(!shader) return false;
    for(size_t i=0;i<g_ptarR15PatchedShaders.size();++i)
        if(g_ptarR15PatchedShaders[i]==shader) return true;
    return false;
}

static void PtR15BuildJitterConstant(float out[4])
{
    out[0]=out[1]=out[2]=out[3]=0.0f;
    if(!PtR15ShouldAffectGameDraw()) return;

    const UINT phase=g_ptarR15.phaseIndex%9u;
    const int px=g_ptarR15PhaseSchedule[phase][0];
    const int py=g_ptarR15PhaseSchedule[phase][1];
    const float jx=PtR15AxisJitter(px);
    const float jy=PtR15AxisJitter(py);

    // Calibrated by the D3D9 HAL raster oracle. j is the desired sampling
    // displacement, therefore geometry moves by the opposite screen shift.
    out[0]=-2.0f*jx/(float)g_ptar.sourceW;
    out[1]= 2.0f*jy/(float)g_ptar.sourceH;
}

static DWORD PtR15NativeLodBiasState(DWORD raw)
{
    float gameBias=0.0f;
    memcpy(&gameBias,&raw,sizeof(gameBias));
    gameBias-=0.5849625007211562f; // -log2(1.5), lab optimum = -0.585
    DWORD adjusted=0;
    memcpy(&adjusted,&gameBias,sizeof(adjusted));
    return adjusted;
}

static void PtR15ApplyVertexConstant(IDirect3DDevice9* dev)
{
    if(!dev || !g_realSetVertexShaderConstantF) return;
    if(g_ptarR15.currentVertexShaderPatched && PtR15ShouldAffectGameDraw())
    {
        float jitter[4]={0,0,0,0};
        PtR15BuildJitterConstant(jitter);
        g_realSetVertexShaderConstantF(dev,255,jitter,1);
    }
    else
    {
        g_realSetVertexShaderConstantF(dev,255,g_ptarR15.gameC255,1);
    }
}

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

static void PtR15ResetSamplingState(bool mainTarget)
{
    g_ptarR15.mainTarget=mainTarget;
    g_ptarR15.currentVertexShaderPatched=false;
    g_ptarR15.phaseIndex=0;
    ZeroMemory(g_ptarR15.gameC255,sizeof(g_ptarR15.gameC255));
    ZeroMemory(g_ptarR15.rawLodBias,sizeof(g_ptarR15.rawLodBias));
}

static void PtR15AdvanceForNextGameFrame(IDirect3DDevice9* dev)
{
    if(PtR15Exact720To1080() && !PtHudFgEnabled())
        g_ptarR15.phaseIndex=(g_ptarR15.phaseIndex+1u)%9u;
    else
        g_ptarR15.phaseIndex=0;

    // HookPresent returns the game to the virtual source target.
    g_ptarR15.mainTarget=true;
    PtR15ApplyVertexConstant(dev);
    PtR15ApplyLodBiases(dev);
}
'@
$p=Replace-One $p $stateAnchor $stateInsert 'R15 state/helpers'

$setRtOld=@'
static HRESULT STDMETHODCALLTYPE HookSetRenderTarget(
    IDirect3DDevice9* self,DWORD index,IDirect3DSurface9* surface)
{
    if(g_ptar.active && self==g_ptar.device && index==0 &&
       surface && surface==g_ptar.realBackBuffer && g_ptar.sourceSurface)
        surface=g_ptar.sourceSurface;
    return g_realSetRenderTarget(self,index,surface);
}
'@
$setRtNew=@'
static HRESULT STDMETHODCALLTYPE HookSetRenderTarget(
    IDirect3DDevice9* self,DWORD index,IDirect3DSurface9* surface)
{
    if(g_ptar.active && self==g_ptar.device && index==0 &&
       surface && surface==g_ptar.realBackBuffer && g_ptar.sourceSurface)
        surface=g_ptar.sourceSurface;

    HRESULT hr=g_realSetRenderTarget(self,index,surface);
    if(SUCCEEDED(hr) && g_ptar.active && self==g_ptar.device &&
       index==0 && !g_ptar.inPresent)
    {
        g_ptarR15.mainTarget=(surface && surface==g_ptar.sourceSurface);
        PtR15ApplyVertexConstant(self);
        PtR15ApplyLodBiases(self);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetSamplerState(
    IDirect3DDevice9* self,DWORD sampler,D3DSAMPLERSTATETYPE type,DWORD value)
{
    if(!g_realSetSamplerState)
        return D3DERR_INVALIDCALL;

    if(g_ptar.active && self==g_ptar.device && !g_ptar.inPresent &&
       sampler<16 && type==D3DSAMP_MIPMAPLODBIAS)
    {
        g_ptarR15.rawLodBias[sampler]=value;
        if(PtR15ShouldAffectGameDraw())
            value=PtR15NativeLodBiasState(value);
    }
    return g_realSetSamplerState(self,sampler,type,value);
}

static HRESULT STDMETHODCALLTYPE HookCreateVertexShader(
    IDirect3DDevice9* self,const DWORD* function,IDirect3DVertexShader9** out)
{
    if(!g_realCreateVertexShader)
        return D3DERR_INVALIDCALL;
    if(out) *out=0;
    if(!function || !out)
        return g_realCreateVertexShader(self,function,out);

    if(g_ptar.active && self==g_ptar.device)
    {
        std::vector<DWORD> patched;
        PtVsJitterPatchInfo info={};
        if(PtPatchVertexShaderForJitter(function,&patched,&info))
        {
            HRESULT patchedHr=g_realCreateVertexShader(self,patched.data(),out);
            if(SUCCEEDED(patchedHr) && *out)
            {
                (*out)->AddRef();
                g_ptarR15PatchedShaders.push_back(*out);
                ++g_ptarR15.patchedShaderCount;
                PtDiagLogA(
                    "R15_VS_PATCH PASS shader=%p vs=%u_%u temp=r%u writes=%u total=%lu",
                    *out,info.shaderMajor,info.shaderMinor,info.tempRegNum,
                    info.positionWrites,g_ptarR15.patchedShaderCount);
                return patchedHr;
            }
            if(*out){(*out)->Release();*out=0;}
            PtDiagLogA(
                "R15_VS_PATCH_CREATE_FAIL hr=0x%08lX fallback=ORIGINAL",
                (unsigned long)patchedHr);
        }
        else
        {
            ++g_ptarR15.rejectedShaderCount;
        }
    }
    return g_realCreateVertexShader(self,function,out);
}

static HRESULT STDMETHODCALLTYPE HookSetVertexShader(
    IDirect3DDevice9* self,IDirect3DVertexShader9* shader)
{
    if(!g_realSetVertexShader)
        return D3DERR_INVALIDCALL;
    HRESULT hr=g_realSetVertexShader(self,shader);
    if(SUCCEEDED(hr) && g_ptar.active && self==g_ptar.device && !g_ptar.inPresent)
    {
        g_ptarR15.currentVertexShaderPatched=PtR15IsPatchedVertexShader(shader);
        PtR15ApplyVertexConstant(self);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetVertexShaderConstantF(
    IDirect3DDevice9* self,UINT start,const float* data,UINT count)
{
    if(!g_realSetVertexShaderConstantF)
        return D3DERR_INVALIDCALL;

    if(g_ptar.active && self==g_ptar.device && !g_ptar.inPresent &&
       data && start<=255u && count>0u && start+count>255u)
    {
        const UINT offset=(255u-start)*4u;
        memcpy(g_ptarR15.gameC255,data+offset,4*sizeof(float));
    }

    HRESULT hr=g_realSetVertexShaderConstantF(self,start,data,count);
    if(SUCCEEDED(hr) && g_ptar.active && self==g_ptar.device && !g_ptar.inPresent)
        PtR15ApplyVertexConstant(self);
    return hr;
}
'@
$p=Replace-One $p $setRtOld $setRtNew 'R15 render/sampler/VS hooks'

# On Present return, advance the deterministic nine-frame phase only after the
# current REAL source has been consumed. This keeps phase identity unambiguous.
$returnAnchor=@'
    self->SetDepthStencilSurface(g_ptar.virtualDepth);
    SetVirtualViewport(self);

    PtDiagStage("PRESENT_RETURN_TO_GAME");
'@
$returnNew=@'
    self->SetDepthStencilSurface(g_ptar.virtualDepth);
    SetVirtualViewport(self);
    PtR15AdvanceForNextGameFrame(self);

    PtDiagStage("PRESENT_RETURN_TO_GAME");
'@
$p=Replace-One $p $returnAnchor $returnNew 'R15 present phase advance'

# Reset returns D3D9 sampler/constant state to defaults; reinitialise the lab
# sampling state after PTAR resources are successfully rebuilt.
$resetTail=@'
    if(FAILED(hr))
    {
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT fallbackHr=g_realReset(self,&fallback);
        *pp=original;
        Log(L"RESET_PTAR_INIT_FAIL fallback=0x%08X",(unsigned)fallbackHr);
        return fallbackHr;
    }

    return S_OK;
}

static bool HookDevice(IDirect3DDevice9* dev)
'@
$resetTailNew=@'
    if(FAILED(hr))
    {
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT fallbackHr=g_realReset(self,&fallback);
        *pp=original;
        Log(L"RESET_PTAR_INIT_FAIL fallback=0x%08X",(unsigned)fallbackHr);
        return fallbackHr;
    }

    PtR15ResetSamplingState(true);
    PtR15ApplyLodBiases(self);
    return S_OK;
}

static bool HookDevice(IDirect3DDevice9* dev)
'@
$p=Replace-One $p $resetTail $resetTailNew 'R15 reset sampling state'

$hookTail=@'
    old=0;
    if(!PatchVtableSlot(dev,37,(void*)&HookSetRenderTarget,&old,"Device.SetRenderTarget"))
        return false;
    if(old!=(void*)&HookSetRenderTarget) g_realSetRenderTarget=(PFN_SetRenderTarget)old;

    PtDiagStage("HookDevice_DONE");
    PtDiagLogA("HOOK_DEVICE_DONE vtable=%p present=%p reset=%p getbb=%p setrt=%p",
        vt,(void*)g_realPresent,(void*)g_realReset,
        (void*)g_realGetBackBuffer,(void*)g_realSetRenderTarget);
'@
$hookTailNew=@'
    old=0;
    if(!PatchVtableSlot(dev,37,(void*)&HookSetRenderTarget,&old,"Device.SetRenderTarget"))
        return false;
    if(old!=(void*)&HookSetRenderTarget) g_realSetRenderTarget=(PFN_SetRenderTarget)old;

    old=0;
    if(!PatchVtableSlot(dev,69,(void*)&HookSetSamplerState,&old,"Device.SetSamplerState"))
        return false;
    if(old!=(void*)&HookSetSamplerState) g_realSetSamplerState=(PFN_SetSamplerState)old;

    old=0;
    if(!PatchVtableSlot(dev,91,(void*)&HookCreateVertexShader,&old,"Device.CreateVertexShader"))
        return false;
    if(old!=(void*)&HookCreateVertexShader) g_realCreateVertexShader=(PFN_CreateVertexShader)old;

    old=0;
    if(!PatchVtableSlot(dev,92,(void*)&HookSetVertexShader,&old,"Device.SetVertexShader"))
        return false;
    if(old!=(void*)&HookSetVertexShader) g_realSetVertexShader=(PFN_SetVertexShader)old;

    old=0;
    if(!PatchVtableSlot(dev,94,(void*)&HookSetVertexShaderConstantF,&old,"Device.SetVertexShaderConstantF"))
        return false;
    if(old!=(void*)&HookSetVertexShaderConstantF) g_realSetVertexShaderConstantF=(PFN_SetVertexShaderConstantF)old;

    PtDiagStage("HookDevice_DONE");
    PtDiagLogA(
        "HOOK_DEVICE_DONE vtable=%p present=%p reset=%p getbb=%p setrt=%p "
        "r15_sampler=%p r15_createvs=%p r15_setvs=%p r15_setc=%p",
        vt,(void*)g_realPresent,(void*)g_realReset,
        (void*)g_realGetBackBuffer,(void*)g_realSetRenderTarget,
        (void*)g_realSetSamplerState,(void*)g_realCreateVertexShader,
        (void*)g_realSetVertexShader,(void*)g_realSetVertexShaderConstantF);
'@
$p=Replace-One $p $hookTail $hookTailNew 'R15 vtable hooks'

# Capture the original function pointers before HookDevice patches the shared
# D3D9 device vtable.
$createPointers=@'
    g_realGetDisplayMode=(PFN_GetDisplayMode)vt[8];
    g_realReset=(PFN_Reset)vt[16];
    g_realPresent=(PFN_Present)vt[17];
    g_realGetBackBuffer=(PFN_GetBackBuffer)vt[18];
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];
'@
$createPointersNew=@'
    g_realGetDisplayMode=(PFN_GetDisplayMode)vt[8];
    g_realReset=(PFN_Reset)vt[16];
    g_realPresent=(PFN_Present)vt[17];
    g_realGetBackBuffer=(PFN_GetBackBuffer)vt[18];
    g_realSetRenderTarget=(PFN_SetRenderTarget)vt[37];
    g_realSetSamplerState=(PFN_SetSamplerState)vt[69];
    g_realCreateVertexShader=(PFN_CreateVertexShader)vt[91];
    g_realSetVertexShader=(PFN_SetVertexShader)vt[92];
    g_realSetVertexShaderConstantF=(PFN_SetVertexShaderConstantF)vt[94];
'@
$p=Replace-One $p $createPointers $createPointersNew 'R15 capture original vtable'

$createTail=@'
    if(!HookDevice(dev))
    {
        ReleasePTARResources();
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT resetHr=g_realReset(dev,&fallback);
        Log(L"CREATEDEVICE_HOOK_FAIL native_reset=0x%08X",(unsigned)resetHr);
        *pp=original;
        return S_OK;
    }

    *pp=original;
    return S_OK;
}
'@
$createTailNew=@'
    if(!HookDevice(dev))
    {
        ReleasePTARResources();
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT resetHr=g_realReset(dev,&fallback);
        Log(L"CREATEDEVICE_HOOK_FAIL native_reset=0x%08X",(unsigned)resetHr);
        *pp=original;
        return S_OK;
    }

    PtR15ResetSamplingState(true);
    PtR15ApplyLodBiases(dev);
    PtDiagLogA(
        "R15_TRUE720_RUNTIME READY jitter=3x3 lodBias=-0.5849625 "
        "src=%ux%u out=%ux%u fgScope=OFF_ONLY",
        g_ptar.sourceW,g_ptar.sourceH,g_ptar.outputW,g_ptar.outputH);

    *pp=original;
    return S_OK;
}
'@
$p=Replace-One $p $createTail $createTailNew 'R15 post-hook initialization'

foreach($token in @(
    'PTAR R15 TRUE720 JITTER9 RUNTIME',
    'HookSetSamplerState',
    'HookCreateVertexShader',
    'HookSetVertexShaderConstantF',
    'g_ptarR15PhaseSchedule',
    'gameBias-=0.5849625007211562f',
    'PatchVtableSlot(dev,69',
    'PatchVtableSlot(dev,91',
    'PatchVtableSlot(dev,92',
    'PatchVtableSlot(dev,94',
    'PtR15AdvanceForNextGameFrame(self)'))
{
    if($p.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
        {throw "R15 runtime final token missing: $token"}
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $proxyPath),$p,$utf8)
Write-Host 'R15_TRUE720_GAME_SAMPLING_RUNTIME_PATCH=PASS'
Write-Host 'R15_JITTER_PHASES=3_X_3'
Write-Host 'R15_JITTER_SOURCE_PIXELS=-1_OVER_3_0_PLUS1_OVER_3'
Write-Host 'R15_JITTER_SCOPE=MAIN_SOURCE_RT_ONLY'
Write-Host 'R15_TEXTURE_LOD_BIAS=-0.5849625007211562'
Write-Host 'R15_TEXTURE_LOD_SCOPE=MAIN_SOURCE_RT_ONLY'
Write-Host 'R15_FG_SCOPE=OFF_ONLY'
