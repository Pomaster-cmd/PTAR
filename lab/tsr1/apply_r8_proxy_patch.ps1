$ErrorActionPreference='Stop'

$path='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

function Replace-One([string]$old,[string]$new,[string]$label)
{
    $first=$script:text.IndexOf($old,[StringComparison]::Ordinal)
    if($first -lt 0){throw "R8 proxy patch anchor missing: $label"}
    $second=$script:text.IndexOf($old,$first+$old.Length,[StringComparison]::Ordinal)
    if($second -ge 0){throw "R8 proxy patch anchor not unique: $label"}
    $script:text=$script:text.Substring(0,$first)+$new+$script:text.Substring($first+$old.Length)
}

# Earlier qualification stages may insert additional bytecode headers between
# the FG headers and ptar_diag.h. Anchor only on ptar_diag.h, which is unique
# and stable across the qualified R3->R7 lineage.
Replace-One @'
#include "ptar_diag.h"
'@ @'
#include "ptar_tsr1_motion_subpixel_ps_bytecode.h"
#include "ptar_tsr1_resolve_ps_bytecode.h"
#include "ptar_tsr1_runtime.h"
#include "ptar_diag.h"
'@ 'bytecode includes'

Replace-One @'
    g_ptar.active=false;
    g_ptar.spatialActive=false;
    g_ptar.previousRealValid=false;
    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}
'@ @'
    g_ptar.active=false;
    g_ptar.spatialActive=false;
    g_ptar.previousRealValid=false;
    PtTsr1Release();
    if(g_ptar.stateBlock){g_ptar.stateBlock->Release();g_ptar.stateBlock=0;}
'@ 'TSR release hook'

Replace-One @'
    PtDiagStage("Initialize_CreateStateBlock");
    hr=dev->CreateStateBlock(D3DSBT_ALL,&g_ptar.stateBlock);
'@ @'
    if(spatialActive)
    {
        PtDiagStage("Initialize_CreateTSR1");
        HRESULT tsrHr=PtTsr1Initialize(
            dev,sourceW,sourceH,outputW,outputH,desc.Format);
        if(FAILED(tsrHr))
            PtDiagLogA(
                "TSR1_INIT_FAILOPEN hr=0x%08lX fallback=R7",
                (unsigned long)tsrHr);
    }

    PtDiagStage("Initialize_CreateStateBlock");
    hr=dev->CreateStateBlock(D3DSBT_ALL,&g_ptar.stateBlock);
'@ 'TSR initialization'

Replace-One @'
static HRESULT RenderSpatialToCurrent(IDirect3DDevice9* dev)
'@ @'
#include "ptar_tsr1_runtime.inl"

static HRESULT RenderSpatialToCurrent(IDirect3DDevice9* dev)
'@ 'TSR runtime implementation include'

$oldPresent=@'
    {
        bool fgProducing=false;
        bool fgPipelineReady=false;
        const bool fgSession=
            PtHudFgEnabled() && g_ptar.previousRealValid;

        if(fgSession)
        {
            HRESULT fgHr=RenderFGMotionAndIntermediate(self);
            if(SUCCEEDED(fgHr))
            {
                fgPipelineReady=true;

                // Build FG first, then let the production-port local-grid
                // pacer place GENERATED/REAL without historical skip storms.
                if(PtFgPacerPrepareGenerated())
                {
                    PtDiagStage("FG_PRESENT_GENERATED");
                    HRESULT generatedPresent=PresentPTARTexture(
                        self,g_ptar.generatedTexture,hwnd,dirty,true,true);
                    if(SUCCEEDED(generatedPresent))
                    {
                        fgProducing=true;
                    }
                    else
                    {
                        PtDiagLogA(
                            "FG_PRESENT_GENERATED_FAIL hr=0x%08lX",
                            (unsigned long)generatedPresent);
                    }
                }
            }
            else
            {
                PtDiagLogA(
                    "FG_PIPELINE_FALLBACK_REAL hr=0x%08lX",
                    (unsigned long)fgHr);
            }
        }

        // Preserve the production-port GENERATED -> REAL local cadence when
        // the FG pipeline is healthy. If it fails, return to REAL-only.
        PtFgPacerPrepareReal(fgSession && fgPipelineReady);

        PtDiagStage("PRESENT_REAL");
        result=PresentPTARTexture(
            self,g_ptar.currentRealTexture,hwnd,dirty,false,fgProducing);

        if(SUCCEEDED(result))
            RotateRealHistory();
    }
'@

$newPresent=@'
    {
        bool fgProducing=false;
        bool fgPipelineReady=false;
        bool tsrResolved=false;
        const bool fgEnabled=PtHudFgEnabled();

        // R8 quality priority: use source-domain temporal SR only for the exact
        // Q720 x1.5 path and only while FG is OFF. FG itself remains byte-for-
        // byte on its qualified R7 path, so quality work cannot destabilize it.
        if(!fgEnabled && g_ptar.spatialActive && PtTsr1CanResolve())
        {
            const PTARPresentationRect tsrFit=PtResolutionAspectFit(
                g_ptar.sourceW,g_ptar.sourceH,
                g_ptar.outputW,g_ptar.outputH);
            const bool exactTsr15=
                tsrFit.x==0 && tsrFit.y==0 &&
                tsrFit.width==g_ptar.outputW &&
                tsrFit.height==g_ptar.outputH &&
                PtResolutionExactScale15(
                    g_ptar.sourceW,g_ptar.sourceH,
                    tsrFit.width,tsrFit.height);

            if(exactTsr15)
            {
                HRESULT tsrHr=PtTsr1Resolve(
                    self,g_ptar.sourceTexture,g_ptar.generatedSurface);
                if(tsrHr==S_OK)
                    tsrResolved=true;
                else if(FAILED(tsrHr))
                    PtDiagLogA(
                        "TSR1_FRAME_FALLBACK_R7 hr=0x%08lX",
                        (unsigned long)tsrHr);
            }
        }

        const bool fgSession=
            fgEnabled && g_ptar.previousRealValid;

        if(fgSession)
        {
            HRESULT fgHr=RenderFGMotionAndIntermediate(self);
            if(SUCCEEDED(fgHr))
            {
                fgPipelineReady=true;

                // Existing FG path intentionally unchanged.
                if(PtFgPacerPrepareGenerated())
                {
                    PtDiagStage("FG_PRESENT_GENERATED");
                    HRESULT generatedPresent=PresentPTARTexture(
                        self,g_ptar.generatedTexture,hwnd,dirty,true,true);
                    if(SUCCEEDED(generatedPresent))
                    {
                        fgProducing=true;
                    }
                    else
                    {
                        PtDiagLogA(
                            "FG_PRESENT_GENERATED_FAIL hr=0x%08lX",
                            (unsigned long)generatedPresent);
                    }
                }
            }
            else
            {
                PtDiagLogA(
                    "FG_PIPELINE_FALLBACK_REAL hr=0x%08lX",
                    (unsigned long)fgHr);
            }
        }

        PtFgPacerPrepareReal(fgSession && fgPipelineReady);

        PtDiagStage(tsrResolved?"PRESENT_REAL_TSR1":"PRESENT_REAL");
        IDirect3DTexture9* realTexture=
            tsrResolved?g_ptar.generatedTexture:g_ptar.currentRealTexture;
        result=PresentPTARTexture(
            self,realTexture,hwnd,dirty,false,fgProducing);

        if(SUCCEEDED(result))
        {
            if(tsrResolved)
            {
                // Make the displayed TSR result the REAL history frame. The
                // old spatial current becomes the next generated scratch.
                IDirect3DTexture9* texture=g_ptar.currentRealTexture;
                g_ptar.currentRealTexture=g_ptar.generatedTexture;
                g_ptar.generatedTexture=texture;
                IDirect3DSurface9* surface=g_ptar.currentRealSurface;
                g_ptar.currentRealSurface=g_ptar.generatedSurface;
                g_ptar.generatedSurface=surface;
            }

            RotateRealHistory();

            // Keep the immediately previous SOURCE frame, not merely the
            // already-upscaled output. This is the information R8 needs for
            // genuine subpixel sample fusion on the next real frame.
            HRESULT historyHr=PtTsr1StoreSource(
                self,g_ptar.sourceSurface,g_ptar.sourceTexture,
                g_ptar.bilinearShader);
            if(FAILED(historyHr))
                PtDiagLogA(
                    "TSR1_HISTORY_FALLBACK_R7 hr=0x%08lX",
                    (unsigned long)historyHr);
        }
    }
'@
Replace-One $oldPresent $newPresent 'Present temporal resolve block'

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R8_PROXY_PATCH=PASS'
