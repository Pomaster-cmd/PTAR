#pragma once

#include <windows.h>
#include "ptar_runtime_metrics.h"

// D3D9 adaptation of the accepted D3D11 GW16F/G NOLOCK30_1 presentation
// policy.  This deliberately does NOT create a new pacing algorithm.
//
// The validated D3D11 production contract is:
//   FG OFF : REAL target up to 60/s, Present SyncInterval=1
//   FG ON  : REAL+GENERATED stream, Present SyncInterval=1
//   no extra Sleep, WaitForVBlank, DwmFlush or per-frame pacing detour.
//
// D3D9 cannot choose a DXGI SyncInterval per Present.  The corresponding
// contract is therefore established once on the D3D9 presentation parameters
// (D3DPRESENT_INTERVAL_ONE) and this helper becomes bookkeeping only.  The
// driver/vblank-blocking Present is the sole cadence authority.
//
// FIELDHOTFIX3 incorrectly added a QPC wait before an already synchronized
// D3D9 Present.  Field telemetry then showed GENERATED residence locked around
// one extra 60-Hz vblank (~17.2 ms) while REAL throughput collapsed.  Keeping
// any software wait here would recreate that double-synchronization defect.

struct PTFGPacerState
{
    LARGE_INTEGER frequency;
    LONGLONG lastRealQpc;
    LONGLONG lastVisibleQpc;
    LONGLONG nextVisibleQpc;       // retained for diagnostic/source ABI continuity; always 0
    LONGLONG lastSourceWorkQpc;    // retained for diagnostic/source ABI continuity
    LONGLONG pairAccQpc;           // retained for diagnostic/source ABI continuity; always 0
    PTARRollingRate visibleRate;
    unsigned long realPresents;
    unsigned long generatedPresents;
    unsigned long generatedLateSkips;
    unsigned long resyncs;
    unsigned long waitYields;
    unsigned int pairBudget;
    unsigned int pairFirstSlots;
    unsigned int pairSecondSlots;
    bool budget3Orientation;
    bool initialized;
};

static PTFGPacerState g_ptarFgPacer={};

static void PtFgPacerInit()
{
    if(g_ptarFgPacer.initialized)
        return;

    QueryPerformanceFrequency(&g_ptarFgPacer.frequency);
    if(g_ptarFgPacer.frequency.QuadPart<=0)
        g_ptarFgPacer.frequency.QuadPart=1;

    g_ptarFgPacer.initialized=true;
    PtDiagLogA(
        "FG_PACER_INIT qpc_freq=%lld policy=D3D11_NOLOCK30_SYNC1_PRESENT_ONLY",
        (long long)g_ptarFgPacer.frequency.QuadPart);
}

static LONGLONG PtFgPacerNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static void PtFgPacerReset()
{
    PtFgPacerInit();
    g_ptarFgPacer.lastRealQpc=0;
    g_ptarFgPacer.lastVisibleQpc=0;
    g_ptarFgPacer.nextVisibleQpc=0;
    g_ptarFgPacer.lastSourceWorkQpc=0;
    g_ptarFgPacer.pairAccQpc=0;
    PtRollingRateReset(&g_ptarFgPacer.visibleRate);
    g_ptarFgPacer.realPresents=0;
    g_ptarFgPacer.generatedPresents=0;
    g_ptarFgPacer.generatedLateSkips=0;
    g_ptarFgPacer.resyncs=0;
    g_ptarFgPacer.waitYields=0;
    g_ptarFgPacer.pairBudget=2;
    g_ptarFgPacer.pairFirstSlots=1;
    g_ptarFgPacer.pairSecondSlots=1;
    g_ptarFgPacer.budget3Orientation=false;
    PtDiagLogA("FG_PACER_RESET policy=D3D11_NOLOCK30_SYNC1_PRESENT_ONLY");
}

static bool PtFgPacerPrepareGenerated()
{
    PtFgPacerInit();

    // Match D3D11 NOLOCK30_1: no software pacing wait before GENERATED.
    // D3DPRESENT_INTERVAL_ONE on the real Present is the cadence authority.
    const LONGLONG now=PtFgPacerNow();
    if(g_ptarFgPacer.lastRealQpc>0 && now>g_ptarFgPacer.lastRealQpc)
        g_ptarFgPacer.lastSourceWorkQpc=now-g_ptarFgPacer.lastRealQpc;
    else
        g_ptarFgPacer.lastSourceWorkQpc=0;

    return true;
}

static void PtFgPacerPrepareReal(bool fgPair)
{
    PtFgPacerInit();
    (void)fgPair;

    // Match D3D11 NOLOCK30_1: no extra wait between GENERATED and REAL.
    // The preceding GENERATED Present and this REAL Present each synchronize
    // once through the same presentation mechanism.
}

static void PtFgPacerRecordVisible(bool generated)
{
    PtFgPacerInit();

    const LONGLONG now=PtFgPacerNow();
    g_ptarFgPacer.lastVisibleQpc=now;
    g_ptarFgPacer.nextVisibleQpc=0;
    PtRollingRateRecordAt(&g_ptarFgPacer.visibleRate,now);

    if(generated)
    {
        ++g_ptarFgPacer.generatedPresents;
    }
    else
    {
        ++g_ptarFgPacer.realPresents;
        g_ptarFgPacer.lastRealQpc=now;
    }
}

static double PtFgPacerVisibleFps()
{
    return PtRollingRateValue(&g_ptarFgPacer.visibleRate);
}

static unsigned long PtFgPacerGeneratedCount()
{
    return g_ptarFgPacer.generatedPresents;
}

static unsigned long PtFgPacerRealCount()
{
    return g_ptarFgPacer.realPresents;
}

static unsigned long PtFgPacerLateSkipCount()
{
    // Kept for HUD/diagnostic continuity.  NOLOCK30_1 has no software
    // late-skip path; synchronized Present is the only pacing authority.
    return g_ptarFgPacer.generatedLateSkips;
}
