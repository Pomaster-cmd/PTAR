$ErrorActionPreference='Stop'

$path='compat/x86_d3d9/ptar_d3d9_proxy.cpp'
$text=[IO.File]::ReadAllText((Resolve-Path $path)) -replace "`r`n","`n"

# Patch only inside HookPresent.  The qualified lineage is rebuilt dynamically
# by R3->R7 and several lab overlays legitimately add comments/instrumentation,
# so integration must anchor on semantic ownership tokens rather than an exact
# historical formatting snapshot.
$hookStart=$text.IndexOf('static HRESULT STDMETHODCALLTYPE HookPresent(',[StringComparison]::Ordinal)
if($hookStart -lt 0){throw 'R8 HookPresent function missing'}
$hookEnd=$text.IndexOf('restore_game_state:',$hookStart,[StringComparison]::Ordinal)
if($hookEnd -lt 0){throw 'R8 HookPresent restore label missing'}
$hook=$text.Substring($hookStart,$hookEnd-$hookStart)

foreach($required in @(
    'PtTemporalPublishPair(self,hwnd)',
    'TEMPORAL_PUBLISH_FAIL',
    'PtFgPacerPrepareReal(false)'))
{
    if($hook.IndexOf($required,[StringComparison]::Ordinal) -lt 0)
    {
        throw "R8 requires qualified TEMPORALPAIR60 HookPresent token: $required"
    }
}

# 1) Replace only the FG-session declaration.  Whitespace is deliberately
# flexible; this token is unique inside HookPresent after TEMPORALPAIR60.
$sessionPattern='const\s+bool\s+fgSession\s*=\s*PtHudFgEnabled\(\)\s*&&\s*g_ptar\.previousRealValid\s*;'
$sessionMatches=[regex]::Matches($hook,$sessionPattern,[Text.RegularExpressions.RegexOptions]::Singleline)
if($sessionMatches.Count -ne 1){throw "R8 HookPresent fgSession semantic anchor count=$($sessionMatches.Count)"}

$sessionReplacement=@'
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
                self,g_ptar.sourceTexture,
                g_ptar.currentRealTexture,g_ptar.generatedSurface);
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
$hook=[regex]::Replace(
    $hook,$sessionPattern,
    [Text.RegularExpressions.MatchEvaluator]{param($m) $sessionReplacement},
    [Text.RegularExpressions.RegexOptions]::Singleline)

# 2) Replace only the synchronous REAL fail-open tail inside the temporal
# fallback branch.  Locate it structurally from the unique fallback condition
# and RotateRealHistory call instead of depending on comments or line wrapping.
$fallbackToken='if(!(fgSession && fgPipelineReady && fgProducing))'
$fallback=$hook.IndexOf($fallbackToken,[StringComparison]::Ordinal)
if($fallback -lt 0){throw 'R8 HookPresent temporal fallback branch missing'}
$shutdown=$hook.IndexOf('if(!PtHudFgEnabled())',$fallback,[StringComparison]::Ordinal)
if($shutdown -lt 0){throw 'R8 HookPresent FG-off temporal shutdown anchor missing'}
$rotate=$hook.IndexOf('RotateRealHistory();',$shutdown,[StringComparison]::Ordinal)
if($rotate -lt 0){throw 'R8 HookPresent REAL RotateRealHistory anchor missing'}
$tailEnd=$rotate+'RotateRealHistory();'.Length

# Refuse to span into any later block. This protects the qualified presenter if
# its topology changes again.
$publish=$hook.IndexOf('PtTemporalPublishPair(self,hwnd)',$fallback,[StringComparison]::Ordinal)
if($publish -ge $shutdown){throw 'R8 HookPresent unexpected temporal publisher ordering'}
$present=$hook.IndexOf('PtDiagStage("PRESENT_REAL")',$shutdown,[StringComparison]::Ordinal)
if($present -lt 0 -or $present -gt $rotate){throw 'R8 HookPresent REAL present marker missing in fallback tail'}

$newReal=@'
if(!fgEnabled)
                PtTemporalShutdown();

            // Exact proven FASTBLIT2 remains the fail-open path. TSR1 uses the
            // existing generated texture only as an FG-OFF display scratch;
            // currentReal stays exact R7, so enabling FG on the next frame
            // cannot feed a TSR result into its qualified previous/current pair.
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
                // Keep output history on exact R7. TSR1 owns a separate source
                // history and therefore never contaminates the proven FG pair.
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
$hook=$hook.Substring(0,$shutdown)+$newReal+$hook.Substring($tailEnd)

# Reassemble and then enforce semantic scope gates on the actual final source.
$text=$text.Substring(0,$hookStart)+$hook+$text.Substring($hookEnd)
foreach($token in @(
    'PtTemporalPublishPair(self,hwnd)',
    '(void)PtFgPacerPrepareGenerated();',
    'TEMPORAL_PUBLISH_FAIL',
    'PtTsr1InvalidateHistory();',
    'PRESENT_REAL_TSR1',
    'g_ptar.sourceW==1280u',
    'g_ptar.outputW==1920u',
    'g_ptar.currentRealTexture,g_ptar.generatedSurface',
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
Write-Host 'R8_TSR_RESOLVE_BASE=EXISTING_R7_CURRENT'
