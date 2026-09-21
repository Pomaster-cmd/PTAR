#pragma once

#include <windows.h>

// Generic D3D9 frame-generation presentation pacer.
// No game-specific state. The current development target is 60 visible Hz:
// REAL frames are therefore scheduled at 30 Hz and GENERATED frames at the
// midpoint. If the source frame arrives too late for a clean midpoint, the
// generated frame is skipped rather than delivered with severe cadence error.

struct PTFGPacerState
{
    LARGE_INTEGER frequency;
    LONGLONG lastRealQpc;
    LONGLONG lastVisibleQpc;
    double visibleFpsEma;
    unsigned long realPresents;
    unsigned long generatedPresents;
    unsigned long generatedLateSkips;
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
        "FG_PACER_INIT qpc_freq=%lld target_visible_hz=60",
        (long long)g_ptarFgPacer.frequency.QuadPart);
}

static void PtFgPacerReset()
{
    PtFgPacerInit();
    g_ptarFgPacer.lastRealQpc=0;
    g_ptarFgPacer.lastVisibleQpc=0;
    g_ptarFgPacer.visibleFpsEma=0.0;
    g_ptarFgPacer.realPresents=0;
    g_ptarFgPacer.generatedPresents=0;
    g_ptarFgPacer.generatedLateSkips=0;
    g_ptarFgPacer.waitYields=0;
    PtDiagLogA("FG_PACER_RESET");
}

static LONGLONG PtFgPacerPeriodTicks()
{
    PtFgPacerInit();
    // 60 visible frames/s. Keep integer arithmetic deterministic.
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
    const LONGLONG coarseThreshold=freq/500; // about 2 ms

    for(;;)
    {
        const LONGLONG now=PtFgPacerNow();
        const LONGLONG remain=target-now;
        if(remain<=0)
            break;

        // Sleep(0) only yields the remainder of the current quantum and does
        // not depend on system timer-resolution changes. Near the deadline,
        // SwitchToThread avoids a long Win8.1 timer oversleep.
        if(remain>coarseThreshold)
            Sleep(0);
        else
            SwitchToThread();

        ++g_ptarFgPacer.waitYields;
    }
}

static bool PtFgPacerPrepareGenerated()
{
    PtFgPacerInit();

    if(g_ptarFgPacer.lastRealQpc<=0)
    {
        PtDiagLogA("FG_PACER_NO_REAL_HISTORY");
        return false;
    }

    const LONGLONG period=PtFgPacerPeriodTicks();
    const LONGLONG midpoint=g_ptarFgPacer.lastRealQpc+period;
    const LONGLONG now=PtFgPacerNow();

    // Allow a small late window (~25% of one visible period). Beyond that,
    // presenting a generated midpoint would make the G/R spacing visibly
    // asymmetric, so fail soft to REAL-only for this source frame.
    const LONGLONG lateTolerance=period/4;
    if(now>midpoint+lateTolerance)
    {
        ++g_ptarFgPacer.generatedLateSkips;
        PtDiagLogA(
            "FG_PACER_LATE_SKIP late_ticks=%lld skips=%lu",
            (long long)(now-midpoint),
            g_ptarFgPacer.generatedLateSkips);
        return false;
    }

    PtFgPacerWaitUntil(midpoint);
    return true;
}

static void PtFgPacerPrepareReal(bool fgPair)
{
    PtFgPacerInit();

    if(!fgPair || g_ptarFgPacer.lastRealQpc<=0)
        return;

    const LONGLONG target=
        g_ptarFgPacer.lastRealQpc+2*PtFgPacerPeriodTicks();
    PtFgPacerWaitUntil(target);
}

static void PtFgPacerRecordVisible(bool generated)
{
    PtFgPacerInit();

    const LONGLONG now=PtFgPacerNow();
    if(g_ptarFgPacer.lastVisibleQpc>0)
    {
        const LONGLONG delta=now-g_ptarFgPacer.lastVisibleQpc;
        if(delta>0)
        {
            const double fps=
                (double)g_ptarFgPacer.frequency.QuadPart/(double)delta;
            if(fps>1.0 && fps<1000.0)
            {
                g_ptarFgPacer.visibleFpsEma=
                    g_ptarFgPacer.visibleFpsEma<=0.0?
                        fps:
                        g_ptarFgPacer.visibleFpsEma*0.90+fps*0.10;
            }
        }
    }

    g_ptarFgPacer.lastVisibleQpc=now;

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
    return g_ptarFgPacer.visibleFpsEma;
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
