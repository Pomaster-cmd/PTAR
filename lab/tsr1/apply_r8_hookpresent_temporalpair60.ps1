$ErrorActionPreference='Stop'

$path='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

function Replace-One([string]$old,[string]$new,[string]$label)
{
    $first=$script:text.IndexOf($old,[StringComparison]::Ordinal)
    if($first -lt 0){throw "R8 HookPresent anchor missing: $label"}
    $second=$script:text.IndexOf($old,$first+$old.Length,[StringComparison]::Ordinal)
    if($second -ge 0){throw "R8 HookPresent anchor not unique: $label"}
    $script:text=$script:text.Substring(0,$first)+$new+$script:text.Substring($first+$old.Length)
}

# R8 is deliberately layered on TEMPORALPAIR60 rather than replacing its
# presenter block.  FG keeps the qualified async G/R mailbox path verbatim;
# TSR1 is evaluated only when FG is OFF.
$oldSession=@'
        bool fgProducing=false;
        bool fgPipelineReady=false;
        const bool fgSession=
            PtHudFgEnabled() && g_ptar.previousRealValid;
'@
$newSession=@'
        bool fgProducing=false;
        bool fgPipelineReady=false;
        bool tsrResolved=false;
        const bool fgEnabled=PtHudFgEnabled();

        // Do not reuse source history across an FG interval. This keeps the
        // qualified FG path independent from TSR1 and forces a fresh R7 seed
        // on the first frame after FG is disabled.
        if(fgEnabled)
        {
            PtTsr1InvalidateHistory();
        }
        else if(
            g_ptar.spatialActive &&
            g_ptar.sourceW==1280u && g_ptar.sourceH==720u &&
            g_ptar.outputW==1920u && g_ptar.outputH==1080u &&
            PtTsr1CanResolve())
        {
            HRESULT tsrHr=PtTsr1Resolve(
                self,g_ptar.sourceTexture,g_ptar.generatedSurface);
            if(tsrHr==S_OK)
            {
                tsrResolved=true;
            }
            else if(FAILED(tsrHr))
            {
                PtDiagLogA(
                    "TSR1_FRAME_FALLBACK_R7 hr=0x%08lX",
                    (unsigned long)tsrHr);
            }
        }

        const bool fgSession=
            fgEnabled && g_ptar.previousRealValid;
'@
Replace-One $oldSession $newSession 'FG session gate'

$oldReal=@'
            if(!PtHudFgEnabled())
                PtTemporalShutdown();

            // Exact proven FASTBLIT2 fail-open path. No temporal worker and no
            // FG-core change is allowed to make the game unpresentable.
            PtFgPacerPrepareReal(false);
            PtDiagStage("PRESENT_REAL");
            result=PresentPTARTexture(
                self,g_ptar.currentRealTexture,g_ptar.currentRealSurface,
                hwnd,dirty,false,false);
            if(SUCCEEDED(result))
                RotateRealHistory();
'@
$newReal=@'
            if(!fgEnabled)
                PtTemporalShutdown();

            // Exact proven FASTBLIT2 fail-open remains the fallback. TSR1 uses
            // the existing generated texture only as an FG-OFF display scratch;
            // currentReal remains exact R7 so FG output history is never fed a
            // TSR frame when the user enables FG on the next frame.
            PtFgPacerPrepareReal(false);
            PtDiagStage(tsrResolved?"PRESENT_REAL_TSR1":"PRESENT_REAL");
            IDirect3DTexture9* presentTexture=
                tsrResolved?g_ptar.generatedTexture:g_ptar.currentRealTexture;
            IDirect3DSurface9* presentSurface=
                tsrResolved?g_ptar.generatedSurface:g_ptar.currentRealSurface;
            result=PresentPTARTexture(
                self,presentTexture,presentSurface,
                hwnd,dirty,false,false);
            if(SUCCEEDED(result))
            {
                // Keep output history on the exact R7 spatial current. TSR1
                // owns a separate 720p source history and therefore does not
                // need to contaminate the proven FG previous/current pair.
                RotateRealHistory();

                if(!fgEnabled)
                {
                    HRESULT historyHr=PtTsr1StoreSource(
                        self,g_ptar.sourceSurface,g_ptar.sourceTexture,
                        g_ptar.bilinearShader);
                    if(FAILED(historyHr))
                    {
                        PtDiagLogA(
                            "TSR1_HISTORY_FALLBACK_R7 hr=0x%08lX",
                            (unsigned long)historyHr);
                    }
                }
            }
'@
Replace-One $oldReal $newReal 'REAL fail-open presentation'

# Scope and lineage gates. These are intentionally about semantic ownership,
# not merely compilation: the asynchronous FG publisher must still exist and
# TSR1 must not replace it.
foreach($token in @(
    'PtTemporalPublishPair(self,hwnd)',
    '(void)PtFgPacerPrepareGenerated();',
    'TEMPORAL_PUBLISH_FAIL',
    'PtTsr1InvalidateHistory();',
    'PRESENT_REAL_TSR1',
    'g_ptar.sourceW==1280u',
    'g_ptar.outputW==1920u',
    'PtTsr1StoreSource('))
{
    if($text.IndexOf($token,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R8 HookPresent final token missing: $token"
    }
}

if($text.IndexOf('g_ptar.currentRealTexture=g_ptar.generatedTexture',[StringComparison]::Ordinal) -ge 0)
{
    throw 'R8 HookPresent illegally feeds TSR output into qualified FG history'
}

$utf8=New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Resolve-Path $path),$text,$utf8)
Write-Host 'R8_HOOKPRESENT_TEMPORALPAIR60=PASS'
Write-Host 'R8_TSR_SCOPE=FG_OFF_Q720_1280X720_TO_1920X1080'
Write-Host 'R8_FG_OUTPUT_HISTORY=R7_SPATIAL_ONLY'
