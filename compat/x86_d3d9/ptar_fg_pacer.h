#pragma once

#include <windows.h>
#include "ptar_runtime_metrics.h"

// D3D9 FG visible-rate/counter state.
//
// Presentation pacing no longer runs on the game's HookPresent thread. The
// async mailbox presenter owns the output clock, so this module deliberately
// contains no producer waits. It only records actually submitted visible
// REAL/GENERATED Presents for HUD/log telemetry.
//
// PrepareGenerated/PrepareReal are retained as no-wait compatibility helpers
// for older tests/callers. They must never sleep, yield to a deadline or lower
// REAL source throughput.

struct PTFGPacerState
{
    LARGE_INTEGER frequency;
    LONGLONG lastRealQpc;
    LONGLONG lastVisibleQpc;
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
        "FG_PACER_INIT qpc_freq=%lld policy=ASYNC_VISIBLE_METRICS_NO_PRODUCER_WAIT",
        (long long)g_ptarFgPacer.frequency.QuadPart);
}

static void PtFgPacerReset()
{
    PtFgPacerInit();
    g_ptarFgPacer.lastRealQpc=0;
    g_ptarFgPacer.lastVisibleQpc=0;
    PtRollingRateReset(&g_ptarFgPacer.visibleRate);
    g_ptarFgPacer.realPresents=0;
    g_ptarFgPacer.generatedPresents=0;
    g_ptarFgPacer.generatedLateSkips=0;
    g_ptarFgPacer.resyncs=0;
    g_ptarFgPacer.waitYields=0;
    PtDiagLogA(
        "FG_PACER_RESET policy=ASYNC_VISIBLE_METRICS_NO_PRODUCER_WAIT");
}

static LONGLONG PtFgPacerNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

inline bool PtFgPacerPrepareGenerated()
{
    PtFgPacerInit();
    return true;
}

inline void PtFgPacerPrepareReal(bool)
{
    PtFgPacerInit();
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
    }
    else
    {
        ++g_ptarFgPacer.realPresents;
        g_ptarFgPacer.lastRealQpc=now;
    }
}

static double PtFgPacerVisibleFps()
{
    // Wall-clock visible throughput. Hitches and missing output ticks lower
    // the reported rate instead of being hidden by an instantaneous-FPS EMA.
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
    return g_ptarFgPacer.generatedLateSkips;
}
