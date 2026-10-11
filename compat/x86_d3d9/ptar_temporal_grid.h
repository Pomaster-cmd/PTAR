#pragma once

#include <windows.h>

// Temporal pairing grid used ONLY by the asynchronous D3D9 presentation
// worker.  The game/source thread never waits on this clock.
//
// Contract:
// - nominal display period = 1/60 s;
// - never catch up a missed slot with a back-to-back Present;
// - after starvation, a new frame may be submitted immediately only when at
//   least one full grid period has elapsed since the previous submission;
// - the next member is then scheduled exactly one grid period later.
//
// This is intentionally independent from the FG algorithm and from the HF4
// diagnostic pacer.  The latter still owns visible counters/ring timestamps.

struct PTARTemporalGrid
{
    LARGE_INTEGER frequency;
    LONGLONG periodTicks;
    LONGLONG nextDeadlineQpc;
    LONGLONG lastSubmitQpc;
    unsigned long skippedGridSlots;
    unsigned long waitYields;
    bool initialized;
    bool armed;
};

static void PtTemporalGridInit(PTARTemporalGrid* grid)
{
    if(!grid || grid->initialized)
        return;

    QueryPerformanceFrequency(&grid->frequency);
    if(grid->frequency.QuadPart<=0)
        grid->frequency.QuadPart=1;

    // Round to nearest tick instead of truncating so long runs do not acquire
    // a systematic sub-tick drift.
    grid->periodTicks=(grid->frequency.QuadPart+30)/60;
    if(grid->periodTicks<=0)
        grid->periodTicks=1;

    grid->nextDeadlineQpc=0;
    grid->lastSubmitQpc=0;
    grid->skippedGridSlots=0;
    grid->waitYields=0;
    grid->armed=false;
    grid->initialized=true;
}

static LONGLONG PtTemporalGridNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static void PtTemporalGridReset(PTARTemporalGrid* grid)
{
    if(!grid)
        return;
    PtTemporalGridInit(grid);
    grid->nextDeadlineQpc=0;
    grid->lastSubmitQpc=0;
    grid->skippedGridSlots=0;
    grid->waitYields=0;
    grid->armed=false;
}

static void PtTemporalGridArmWithOneSlotPrebuffer(
    PTARTemporalGrid* grid,
    LONGLONG now)
{
    if(!grid)
        return;
    PtTemporalGridInit(grid);
    if(now<=0)
        now=PtTemporalGridNow();
    grid->nextDeadlineQpc=now+grid->periodTicks;
    grid->armed=true;
}

static void PtTemporalGridResumeAfterStarvation(
    PTARTemporalGrid* grid,
    LONGLONG now)
{
    if(!grid)
        return;
    PtTemporalGridInit(grid);
    if(now<=0)
        now=PtTemporalGridNow();

    if(!grid->armed)
    {
        PtTemporalGridArmWithOneSlotPrebuffer(grid,now);
        return;
    }

    // Never create a recovery burst.  If a whole slot already elapsed since
    // the previous submitted frame, immediate recovery is safe.  Otherwise
    // preserve the remaining part of the 16.67 ms separation.
    if(grid->lastSubmitQpc>0 &&
       now-grid->lastSubmitQpc<grid->periodTicks)
    {
        grid->nextDeadlineQpc=grid->lastSubmitQpc+grid->periodTicks;
    }
    else
    {
        grid->nextDeadlineQpc=now;
    }
}

static void PtTemporalGridWaitUntilDeadline(
    PTARTemporalGrid* grid,
    volatile LONG* stopRequested)
{
    if(!grid || !grid->armed || grid->nextDeadlineQpc<=0)
        return;

    for(;;)
    {
        if(stopRequested && InterlockedCompareExchange(
            (volatile LONG*)stopRequested,0,0)!=0)
            return;

        const LONGLONG now=PtTemporalGridNow();
        if(now>=grid->nextDeadlineQpc)
            return;

        // This wait runs only on the presentation worker.  Yielding instead
        // of sleeping the game thread is the key difference from the broken
        // FIELDHOTFIX5/6 source-thread pacing attempts.
        SwitchToThread();
        ++grid->waitYields;
    }
}

static void PtTemporalGridRecordSubmission(
    PTARTemporalGrid* grid,
    LONGLONG submitQpc)
{
    if(!grid)
        return;
    PtTemporalGridInit(grid);
    if(submitQpc<=0)
        submitQpc=PtTemporalGridNow();

    grid->lastSubmitQpc=submitQpc;
    if(!grid->armed)
    {
        grid->nextDeadlineQpc=submitQpc+grid->periodTicks;
        grid->armed=true;
        return;
    }

    grid->nextDeadlineQpc += grid->periodTicks;

    // If Present itself blocked or the worker was descheduled, skip elapsed
    // grid positions.  Do NOT issue catch-up Presents: that would recreate the
    // 0.5 ms GENERATED/REAL micro-bursts seen in the field trace.
    while(grid->nextDeadlineQpc<=submitQpc)
    {
        grid->nextDeadlineQpc += grid->periodTicks;
        ++grid->skippedGridSlots;
    }
}

static double PtTemporalGridTicksToMs(
    const PTARTemporalGrid* grid,
    LONGLONG ticks)
{
    if(!grid || grid->frequency.QuadPart<=0)
        return 0.0;
    return (double)ticks*1000.0/(double)grid->frequency.QuadPart;
}
