#pragma once

#include <windows.h>
#include "ptar_runtime_metrics.h"

// D3D9 adaptation of the validated D3D11 MAINPERF2/PAIRBAL2 presentation
// architecture. This deliberately does NOT invent a second pacing mechanism.
//
// The current D3D11 production contract selects SyncInterval=1 or 2 directly
// on IDXGISwapChain::Present and explicitly introduces no extra Sleep,
// WaitForVBlank, DwmFlush or per-frame pacing detour. Regular D3D9 has no
// per-Present SyncInterval parameter: its PresentationInterval is fixed on the
// device at CreateDevice/Reset time.
//
// Therefore D3D9 reuses the PAIRBAL2 Sync1 presentation branch as the only
// directly representable production path:
//   FG OFF : synchronized REAL Present, interval one
//   FG ON  : GENERATED then REAL, each synchronized by D3D9 Present interval one
//   no additional software timing wait around either Present.
//
// FIELDHOTFIX3 attempted to emulate dynamic Sync1/Sync2 with a QPC wait before
// an already synchronized D3D9 Present. Field telemetry exposed the resulting
// double synchronization: GENERATED residence locked near one 60-Hz vblank
// while REAL throughput collapsed. Keeping any software wait here would
// recreate that defect.

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
        "FG_PACER_INIT qpc_freq=%lld policy=D3D11_PAIRBAL2_D3D9_SYNC1_PRESENT_ONLY",
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
    PtDiagLogA("FG_PACER_RESET policy=D3D11_PAIRBAL2_D3D9_SYNC1_PRESENT_ONLY");
}

static bool PtFgPacerPrepareGenerated()
{
    PtFgPacerInit();

    // Match the D3D11 production architecture: no software pacing wait before
    // GENERATED. D3DPRESENT_INTERVAL_ONE on the D3D9 device is the cadence
    // authority for this directly representable PAIRBAL2 branch.
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

    // No extra wait between GENERATED and REAL. The preceding GENERATED
    // Present and this REAL Present each synchronize exactly once through D3D9.
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
    // Kept for HUD/diagnostic continuity. The D3D9 Sync1 adaptation has no
    // software late-skip path; synchronized Present is the pacing authority.
    return g_ptarFgPacer.generatedLateSkips;
}
