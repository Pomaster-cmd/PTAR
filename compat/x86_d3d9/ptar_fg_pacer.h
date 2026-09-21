#pragma once

#include <windows.h>
#include "ptar_runtime_metrics.h"

// D3D9 FG production-port pacer.
//
// The previous FG1 pacer treated the previous REAL presentation as an absolute
// 60-Hz clock and rejected GENERATED when shader ME finished after that
// historical midpoint. Field evidence showed that this creates skip storms:
// the current frame pair cannot exist until CURRENT has been rendered, so the
// historical midpoint is frequently already in the past.
//
// PRODPORT1 instead preserves the production present-order invariant:
// GENERATED then REAL, with an even local display interval. The current pair is
// anchored when it is ready. We never busy-wait for an already missed
// historical midpoint and never let one late frame poison following pairs.

struct PTFGPacerState
{
    LARGE_INTEGER frequency;
    LONGLONG lastRealQpc;
    LONGLONG lastVisibleQpc;
    LONGLONG nextVisibleQpc;
    PTARRollingRate visibleRate;
    unsigned long realPresents;
    unsigned long generatedPresents;
    unsigned long generatedLateSkips;
    unsigned long resyncs;
    unsigned long waitYields;
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
        "FG_PACER_INIT qpc_freq=%lld policy=PRODPORT1_EVEN_LOCAL_GRID",
        (long long)g_ptarFgPacer.frequency.QuadPart);
}

static void PtFgPacerReset()
{
    PtFgPacerInit();
    g_ptarFgPacer.lastRealQpc=0;
    g_ptarFgPacer.lastVisibleQpc=0;
    g_ptarFgPacer.nextVisibleQpc=0;
    PtRollingRateReset(&g_ptarFgPacer.visibleRate);
    g_ptarFgPacer.realPresents=0;
    g_ptarFgPacer.generatedPresents=0;
    g_ptarFgPacer.generatedLateSkips=0;
    g_ptarFgPacer.resyncs=0;
    g_ptarFgPacer.waitYields=0;
    PtDiagLogA("FG_PACER_RESET policy=PRODPORT1");
}

static LONGLONG PtFgPacerPeriodTicks()
{
    PtFgPacerInit();
    LONGLONG ticks=g_ptarFgPacer.frequency.QuadPart/60;
    return ticks>0?ticks:1;
}

static LONGLONG PtFgPacerNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static void PtFgPacerWaitUntil(LONGLONG target)
{
    if(target<=0)
        return;

    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;
    const LONGLONG coarseThreshold=freq/500; // ~2 ms

    for(;;)
    {
        const LONGLONG now=PtFgPacerNow();
        const LONGLONG remain=target-now;
        if(remain<=0)
            break;

        if(remain>coarseThreshold)
            Sleep(0);
        else
            SwitchToThread();

        ++g_ptarFgPacer.waitYields;
    }
}

static void PtFgPacerResyncIfStale(LONGLONG now)
{
    const LONGLONG period=PtFgPacerPeriodTicks();

    if(g_ptarFgPacer.nextVisibleQpc<=0)
    {
        g_ptarFgPacer.nextVisibleQpc=now;
        return;
    }

    // If the local grid is more than one visible interval behind, catch up in
    // one operation. Do not replay stale deadlines or create a skip cascade.
    if(now>g_ptarFgPacer.nextVisibleQpc+period)
    {
        ++g_ptarFgPacer.resyncs;
        PtDiagLogA(
            "FG_PACER_RESYNC late_ticks=%lld resyncs=%lu",
            (long long)(now-g_ptarFgPacer.nextVisibleQpc),
            g_ptarFgPacer.resyncs);
        g_ptarFgPacer.nextVisibleQpc=now;
    }
}

static bool PtFgPacerPrepareGenerated()
{
    PtFgPacerInit();

    const LONGLONG now=PtFgPacerNow();
    PtFgPacerResyncIfStale(now);

    // GENERATED is already available here. Schedule it on the current/next
    // local visible slot instead of comparing it with a midpoint that elapsed
    // while CURRENT + ME were being produced.
    if(g_ptarFgPacer.nextVisibleQpc<now)
        g_ptarFgPacer.nextVisibleQpc=now;

    PtFgPacerWaitUntil(g_ptarFgPacer.nextVisibleQpc);
    return true;
}

static void PtFgPacerPrepareReal(bool fgPair)
{
    PtFgPacerInit();

    if(!fgPair)
    {
        g_ptarFgPacer.nextVisibleQpc=0;
        return;
    }

    const LONGLONG period=PtFgPacerPeriodTicks();
    const LONGLONG now=PtFgPacerNow();

    if(g_ptarFgPacer.nextVisibleQpc<=0)
        g_ptarFgPacer.nextVisibleQpc=now;

    // GENERATED has just occupied one visible slot. REAL follows exactly one
    // nominal visible period later. Present itself may block to VBlank; stale
    // grids are resynchronised on the next pair rather than accumulated.
    g_ptarFgPacer.nextVisibleQpc+=period;
    PtFgPacerResyncIfStale(now);
    PtFgPacerWaitUntil(g_ptarFgPacer.nextVisibleQpc);
}

static void PtFgPacerRecordVisible(bool generated)
{
    PtFgPacerInit();

    const LONGLONG now=PtFgPacerNow();
    g_ptarFgPacer.lastVisibleQpc=now;
    PtRollingRateRecordAt(&g_ptarFgPacer.visibleRate,now);

    if(generated)
    {
        ++g_ptarFgPacer.generatedPresents;
        // Advance to the REAL slot only in PrepareReal(), after the generated
        // Present actually succeeded.
    }
    else
    {
        ++g_ptarFgPacer.realPresents;
        g_ptarFgPacer.lastRealQpc=now;

        // Complete the pair: reserve the next GENERATED slot one period after
        // REAL. A slow game/Present will be caught by ResyncIfStale.
        if(g_ptarFgPacer.nextVisibleQpc>0)
            g_ptarFgPacer.nextVisibleQpc+=PtFgPacerPeriodTicks();
    }
}

static double PtFgPacerVisibleFps()
{
    // Deliberately wall-clock based. This is the visible throughput over the
    // recent QPC window, so hitches and pacing gaps reduce the displayed FPS
    // instead of being hidden by an instantaneous-FPS EMA.
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
    // Kept for HUD/ABI continuity. PRODPORT1 replaces historical late skips
    // with local-grid resynchronisation, so this remains zero by design.
    return g_ptarFgPacer.generatedLateSkips;
}

