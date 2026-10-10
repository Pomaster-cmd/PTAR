from pathlib import Path
import re

p=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp')
s=p.read_text(encoding='utf-8')
hp=Path('compat/x86_d3d9/ptar_d3d9_isolated_presenter.h')
h=hp.read_text(encoding='utf-8')

# FIELDHOTFIX7 is a topology port, not a new pacing algorithm. It reuses the
# validated D3D11 GW12->GW16 contract: separate display presenter, nonblocking
# producer/mailbox, generated-midpoint before the matching current REAL, and
# soft-OFF REAL_ONLY ownership.
#
# The current D3D11 STALEGUARD1 order key is GENERATED=2*seq, REAL=2*seq+1.
# For a pair built from previous/current source frames that means G01 then R1.
# Preserve that temporal ordering here; do not derive order from old diagnostic
# wording that described residence after an already displayed preceding REAL.

# Adapt the D3D9 presenter header to the exact current D3D11 order semantics.
h=h.replace(
    '// - display presents REAL then GENERATED with one VBlank each;',
    '// - display presents GENERATED then matching REAL with one VBlank each;')

old_present='''        hr=PtD3D9IsoPresentOne(
            dev,realSurf[slotIndex],false,slot->realMarker);
        if(SUCCEEDED(hr) && slot->hasGenerated)
        {
            hr=PtD3D9IsoPresentOne(
                dev,genSurf[slotIndex],true,slot->generatedMarker);
        }
'''
new_present='''        // D3D11 STALEGUARD1 ordering: GENERATED=2*seq, REAL=2*seq+1.
        // Initial activation publishes REAL_ONLY; every complete later pair is
        // the midpoint GENERATED first, then its matching current REAL.
        if(slot->hasGenerated)
        {
            hr=PtD3D9IsoPresentOne(
                dev,genSurf[slotIndex],true,slot->generatedMarker);
        }
        else
        {
            hr=S_OK;
        }
        if(SUCCEEDED(hr))
        {
            hr=PtD3D9IsoPresentOne(
                dev,realSurf[slotIndex],false,slot->realMarker);
        }
'''
if old_present not in h:
    raise SystemExit('presenter REAL->G order anchor missing')
h=h.replace(old_present,new_present,1)

old_submit='''    PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
    HRESULT hr=g_ptar.device->StretchRect(
        realSurface,0,slot->realSurface,0,D3DTEXF_NONE);
    if(SUCCEEDED(hr))
        hr=PtD3D9IsoDrawHudToSurface(
            slot->realSurface,false,hasGenerated,&slot->realMarker);

    slot->hasGenerated=hasGenerated?TRUE:FALSE;
    slot->generatedMarker=0;

    if(SUCCEEDED(hr) && hasGenerated && generatedSurface)
    {
        hr=g_ptar.device->StretchRect(
            generatedSurface,0,slot->generatedSurface,0,D3DTEXF_NONE);
        if(SUCCEEDED(hr))
            hr=PtD3D9IsoDrawHudToSurface(
                slot->generatedSurface,true,true,&slot->generatedMarker);
    }
'''
new_submit='''    PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
    slot->hasGenerated=hasGenerated?TRUE:FALSE;
    slot->generatedMarker=0;
    slot->realMarker=0;

    // Bake markers in the same order they will be displayed. This preserves
    // the existing verifier serial contract and prevents a synthetic backward
    // marker transition when the presenter outputs G before matching R.
    HRESULT hr=S_OK;
    if(hasGenerated && generatedSurface)
    {
        hr=g_ptar.device->StretchRect(
            generatedSurface,0,slot->generatedSurface,0,D3DTEXF_NONE);
        if(SUCCEEDED(hr))
            hr=PtD3D9IsoDrawHudToSurface(
                slot->generatedSurface,true,true,&slot->generatedMarker);
    }

    if(SUCCEEDED(hr))
    {
        hr=g_ptar.device->StretchRect(
            realSurface,0,slot->realSurface,0,D3DTEXF_NONE);
        if(SUCCEEDED(hr))
            hr=PtD3D9IsoDrawHudToSurface(
                slot->realSurface,false,hasGenerated,&slot->realMarker);
    }
'''
if old_submit not in h:
    raise SystemExit('producer HUD order anchor missing')
h=h.replace(old_submit,new_submit,1)
hp.write_text(h,encoding='utf-8',newline='\n')

anchor='static PTARContext g_ptar={};'
if s.count(anchor)!=1:
    raise SystemExit('g_ptar anchor mismatch')
s=s.replace(anchor,anchor+'\n\nstatic void PtD3D9IsolatedPresenterStop();',1)

anchor='static void ReleasePTARResources()\n{\n'
if anchor not in s:
    raise SystemExit('ReleasePTARResources anchor missing')
s=s.replace(anchor,anchor+'    PtD3D9IsolatedPresenterStop();\n',1)

# The main/game device is now an internal producer only. As with the D3D11
# isolated Flip3 path, DWM/composed display ownership belongs to the separate
# presenter. Keeping the producer device windowed avoids two presentation
# owners fighting for the same HWND and removes its VBlank from the game path.
interval='    actual->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;'
if s.count(interval)!=1:
    raise SystemExit('FIELDHOTFIX5 immediate interval anchor missing')
producer_policy=interval+'''\n    actual->Windowed=TRUE;\n    actual->FullScreen_RefreshRateInHz=0;\n    actual->SwapEffect=D3DSWAPEFFECT_DISCARD;\n'''
s=s.replace(interval,producer_policy.rstrip('\n'),1)

# Insert the isolated-presenter implementation after the existing HUD renderer,
# before RotateRealHistory / HookPresent use it.
inc='#include "ptar_d3d9_isolated_presenter.h"\n\n'
rotate='static void RotateRealHistory()'
if s.count(rotate)!=1:
    raise SystemExit('RotateRealHistory anchor mismatch')
s=s.replace(rotate,inc+rotate,1)

# Replace only the active same-device GENERATED->REAL pacing block. Rendering,
# interpolation, history, HUD input and all fallback code stay inherited.
pat=re.compile(r'''    \{\n        bool fgProducing=false;\n        bool fgPipelineReady=false;\n        const bool fgSession=.*?\n    \}\n\nrestore_game_state:''',re.S)
m=pat.search(s)
if not m:
    raise SystemExit('HookPresent active FG block not found')
new='''    {
        bool fgPipelineReady=false;
        const bool fgSession=
            PtHudFgEnabled() && g_ptar.previousRealValid;

        if(fgSession)
        {
            HRESULT fgHr=RenderFGMotionAndIntermediate(self);
            if(SUCCEEDED(fgHr))
            {
                fgPipelineReady=true;
                // Keep the inherited PairBal2 selector callable for telemetry
                // continuity, but it no longer owns display waits. The D3D11
                // isolated presenter is the sole cadence authority.
                PtFgPacerPrepareGenerated();
            }
            else
            {
                PtDiagLogA(
                    "FG_PIPELINE_FALLBACK_REAL hr=0x%08lX",
                    (unsigned long)fgHr);
            }
        }

        // D3D11 GW12->GW16 topology: publish completed source work into the
        // nonblocking mailbox. The display worker presents midpoint GENERATED
        // then matching REAL at Sync1 (STALEGUARD1 order key). Do not block the
        // game thread on either VBlank.
        const bool presenterOwnsDisplay=PtD3D9IsolatedPresenterSubmit(
            g_ptar.currentRealSurface,
            (fgSession && fgPipelineReady)?g_ptar.generatedSurface:0,
            fgSession && fgPipelineReady);

        if(presenterOwnsDisplay)
        {
            result=S_OK;
            RotateRealHistory();
        }
        else
        {
            // Exact D3D11 failure policy: if the isolated presenter is not
            // available, fail open to REAL_ONLY instead of running a broken FG
            // cadence on the game device.
            if(PtHudFgEnabled())
            {
                g_ptarHudFgEnabled=false;
                PtFgPacerReset();
                PtHudFeedback(13,0,0,0);
                PtDiagLogA("ISO_PRESENTER_UNAVAILABLE fallback=REAL_ONLY");
            }

            PtFgPacerPrepareReal(false);
            PtDiagStage("PRESENT_REAL_FALLBACK");
            result=PresentPTARTexture(
                self,g_ptar.currentRealTexture,hwnd,dirty,false,false);
            if(SUCCEEDED(result))
                RotateRealHistory();
        }
    }

restore_game_state:'''
s=s[:m.start()]+new+s[m.end():]

# Restart isolated presenter after a successful Reset.
reset_anchor='''    if(FAILED(hr))
    {
        D3DPRESENT_PARAMETERS fallback=original;
        HRESULT fallbackHr=g_realReset(self,&fallback);
        *pp=original;
        Log(L"RESET_PTAR_INIT_FAIL fallback=0x%08X",(unsigned)fallbackHr);
        return fallbackHr;
    }

    return S_OK;
}

static bool HookDevice'''
if reset_anchor not in s:
    raise SystemExit('HookReset success anchor missing')
reset_new=reset_anchor.replace(
    '    return S_OK;\n}\n\nstatic bool HookDevice',
    '    PtD3D9IsolatedPresenterStart(original.hDeviceWindow);\n    return S_OK;\n}\n\nstatic bool HookDevice')
s=s.replace(reset_anchor,reset_new,1)

# Start isolated presenter only after the game device has been fully hooked.
create_anchor='''    if(!HookDevice(dev))
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
}'''
if create_anchor not in s:
    raise SystemExit('HookCreateDevice success anchor missing')
create_new=create_anchor.replace(
    '    *pp=original;\n    return S_OK;',
    '    PtD3D9IsolatedPresenterStart(\n        original.hDeviceWindow?original.hDeviceWindow:focus);\n\n    *pp=original;\n    return S_OK;',1)
s=s.replace(create_anchor,create_new,1)

p.write_text(s,encoding='utf-8',newline='\n')

# Static gates: the active block must not call a second Present or PairBal2 REAL
# wait, and the port must use the existing isolated presenter header.
t=p.read_text(encoding='utf-8')
h=hp.read_text(encoding='utf-8')
checks={
    'header include':'#include "ptar_d3d9_isolated_presenter.h"',
    'producer windowed':'actual->Windowed=TRUE;',
    'producer immediate':'actual->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;',
    'submit':'PtD3D9IsolatedPresenterSubmit(',
    'start':'PtD3D9IsolatedPresenterStart(',
    'stop':'PtD3D9IsolatedPresenterStop();',
    'order comment':'display worker presents midpoint GENERATED',
}
for name,needle in checks.items():
    if needle not in t:
        raise SystemExit('missing gate '+name)

if 'dev,genSurf[slotIndex],true,slot->generatedMarker' not in h:
    raise SystemExit('generated presenter leg missing')
if 'dev,realSurf[slotIndex],false,slot->realMarker' not in h:
    raise SystemExit('real presenter leg missing')
if h.index('dev,genSurf[slotIndex],true,slot->generatedMarker') > h.index('dev,realSurf[slotIndex],false,slot->realMarker'):
    raise SystemExit('wrong display order: D3D11 requires G before matching R')

active=t[t.index('// D3D11 GW12->GW16 topology:'):t.index('restore_game_state:')]
if 'PtFgPacerPrepareReal(true)' in active or 'FG_PRESENT_GENERATED' in active:
    raise SystemExit('obsolete same-device FG presentation remains active')

print('D3D11_ISOLATED_PRESENTER_TOPOLOGY_PORT=PASS')
print('ACTIVE_ORDER=GENERATED_THEN_MATCHING_REAL')
print('SOURCE_THREAD_DISPLAY_WAIT=NONE')
print('FG_OFF=REAL_ONLY_PRESENTER_OWNERSHIP')
