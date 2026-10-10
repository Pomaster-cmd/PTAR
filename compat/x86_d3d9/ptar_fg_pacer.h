#pragma once

#include <windows.h>
#include "ptar_runtime_metrics.h"

// FIELDHOTFIX5 reuses the existing D3D11 MAINPERF2/PAIRBAL2 port from
// FIELDHOTFIX3. The field failure of FIELDHOTFIX3 was not the selector: it was
// the integration, because the software PairBal2 grid was stacked on top of a
// synchronized D3D9 Present. FIELDHOTFIX4 removed the software grid and exposed
// the opposite failure: GENERATED/REAL Present completions collapsed together.
//
// This revision therefore keeps ONE cadence authority only:
//   - D3D9 device Present interval is IMMEDIATE (patched at CreateDevice/Reset)
//   - the already-existing PairBal2 software grid owns the 1/2-slot timing
//   - no second VBlank wait is added by D3D9 Present
//
// The PairBal2 selector itself is unchanged from FIELDHOTFIX3:
//   source work <= 34 ms  -> pair budget 2 VBlanks -> 1+1
//   source work 34..66 ms -> error-diffused budget 2..4 VBlanks
//   source work >= 66 ms  -> pair budget 4 VBlanks -> 2+2
//   budget 3 alternates 1+2 / 2+1
//   no member may request more than 2 slots.
//
// FG OFF uses the same existing one-slot grid as a software Sync1 ceiling so
// forcing the device to IMMEDIATE does not turn the non-FG path into an
// uncapped presenter. Slow source frames fail open naturally because a missed
// one-slot deadline is already in the past.
//
// Conviction hardening is retained: kernel32!Sleep is resolved indirectly so
// the pacer does not inherit Ultimate ASI Loader's EXE Sleep-IAT hook.

typedef VOID (WINAPI *PTFN_PtarNativeSleep)(DWORD);

struct PTFGPacerState
{
    LARGE_INTEGER frequency;
    LONGLONG lastRealQpc;
    LONGLONG lastVisibleQpc;
    LONGLONG nextVisibleQpc;
    LONGLONG lastSourceWorkQpc;
    LONGLONG pairAccQpc;
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
static PTFN_PtarNativeSleep g_ptarFgNativeSleep=0;
static bool g_ptarFgNativeSleepResolved=false;

static PTFN_PtarNativeSleep PtFgPacerResolveNativeSleep()
{
    if(g_ptarFgNativeSleepResolved)
        return g_ptarFgNativeSleep;

    g_ptarFgNativeSleepResolved=true;

    HMODULE kernel32=GetModuleHandleW(L"kernel32.dll");
    if(kernel32)
    {
        g_ptarFgNativeSleep=(PTFN_PtarNativeSleep)
            GetProcAddress(kernel32,"Sleep");
    }

    PtDiagLogA(
        "FG_PACER_NATIVE_SLEEP resolved=%p policy=INDIRECT_KERNEL32_EXPORT",
        (void*)g_ptarFgNativeSleep);

    return g_ptarFgNativeSleep;
}

static void PtFgPacerYieldCoarse()
{
    PTFN_PtarNativeSleep sleepFn=PtFgPacerResolveNativeSleep();
    if(sleepFn)
        sleepFn(0);
    else
        SwitchToThread();
}

static void PtFgPacerInit()
{
    if(g_ptarFgPacer.initialized)
        return;

    QueryPerformanceFrequency(&g_ptarFgPacer.frequency);
    if(g_ptarFgPacer.frequency.QuadPart<=0)
        g_ptarFgPacer.frequency.QuadPart=1;

    g_ptarFgPacer.initialized=true;
    PtDiagLogA(
        "FG_PACER_INIT qpc_freq=%lld policy=D3D11_MAINPERF2_PAIRBAL2_D3D9_SINGLECLOCK_IMMEDIATE",
        (long long)g_ptarFgPacer.frequency.QuadPart);
}

static LONGLONG PtFgPacerNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static LONGLONG PtFgPacerPeriodTicks()
{
    PtFgPacerInit();
    LONGLONG ticks=g_ptarFgPacer.frequency.QuadPart/60;
    return ticks>0?ticks:1;
}

static LONGLONG PtFgPacerMsTicks(unsigned int ms)
{
    PtFgPacerInit();
    const LONGLONG ticks=
        (g_ptarFgPacer.frequency.QuadPart*(LONGLONG)ms)/1000;
    return ticks>0?ticks:1;
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
    PtDiagLogA("FG_PACER_RESET policy=D3D11_MAINPERF2_PAIRBAL2_D3D9_SINGLECLOCK_IMMEDIATE");
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
            PtFgPacerYieldCoarse();
        else
            SwitchToThread();

        ++g_ptarFgPacer.waitYields;
    }
}

static void PtFgPacerSelectPairBudget(LONGLONG sourceWorkQpc)
{
    // Existing FIELDHOTFIX3 MAINPERF2/PAIRBAL2 model.
    const LONGLONG low=PtFgPacerMsTicks(34);
    const LONGLONG high=PtFgPacerMsTicks(66);
    const LONGLONG width=high-low;

    unsigned int budget=2;

    if(sourceWorkQpc<=0 || sourceWorkQpc<=low)
    {
        budget=2;
        g_ptarFgPacer.pairAccQpc=0;
    }
    else if(sourceWorkQpc>=high)
    {
        budget=4;
        g_ptarFgPacer.pairAccQpc=0;
    }
    else
    {
        g_ptarFgPacer.pairAccQpc += 2*(sourceWorkQpc-low);
        budget=2;
        if(g_ptarFgPacer.pairAccQpc>=width)
        {
            g_ptarFgPacer.pairAccQpc-=width;
            ++budget;
        }
        if(g_ptarFgPacer.pairAccQpc>=width)
        {
            g_ptarFgPacer.pairAccQpc-=width;
            ++budget;
        }
    }

    unsigned int first=1;
    unsigned int second=1;

    if(budget==4)
    {
        first=2;
        second=2;
    }
    else if(budget==3)
    {
        g_ptarFgPacer.budget3Orientation=
            !g_ptarFgPacer.budget3Orientation;
        if(g_ptarFgPacer.budget3Orientation)
        {
            first=1;
            second=2;
        }
        else
        {
            first=2;
            second=1;
        }
    }

    g_ptarFgPacer.pairBudget=budget;
    g_ptarFgPacer.pairFirstSlots=first;
    g_ptarFgPacer.pairSecondSlots=second;

    PtDiagLogA(
        "FG_PAIRBAL2 source_work_ticks=%lld budget=%u split=%u+%u acc=%lld",
        (long long)sourceWorkQpc,
        budget,first,second,
        (long long)g_ptarFgPacer.pairAccQpc);
}

static void PtFgPacerWaitMemberSlots(unsigned int slots)
{
    if(slots<1) slots=1;
    if(slots>2) slots=2;

    const LONGLONG now=PtFgPacerNow();
    const LONGLONG period=PtFgPacerPeriodTicks();

    if(g_ptarFgPacer.lastVisibleQpc<=0)
    {
        g_ptarFgPacer.nextVisibleQpc=now;
        return;
    }

    const LONGLONG target=
        g_ptarFgPacer.lastVisibleQpc+period*(LONGLONG)slots;
    g_ptarFgPacer.nextVisibleQpc=target;

    if(now>target+period)
    {
        ++g_ptarFgPacer.resyncs;
        PtDiagLogA(
            "FG_PAIRBAL2_RESYNC late_ticks=%lld slots=%u resyncs=%lu",
            (long long)(now-target),slots,g_ptarFgPacer.resyncs);
        return;
    }

    PtFgPacerWaitUntil(target);
}

static bool PtFgPacerPrepareGenerated()
{
    PtFgPacerInit();

    const LONGLONG now=PtFgPacerNow();
    LONGLONG sourceWork=0;
    if(g_ptarFgPacer.lastRealQpc>0 && now>g_ptarFgPacer.lastRealQpc)
        sourceWork=now-g_ptarFgPacer.lastRealQpc;

    g_ptarFgPacer.lastSourceWorkQpc=sourceWork;
    PtFgPacerSelectPairBudget(sourceWork);
    PtFgPacerWaitMemberSlots(g_ptarFgPacer.pairFirstSlots);
    return true;
}

static void PtFgPacerPrepareReal(bool fgPair)
{
    PtFgPacerInit();

    if(!fgPair)
    {
        // Device-level presentation is IMMEDIATE in FIELDHOTFIX5. Reuse the
        // existing one-slot scheduler as the software equivalent of Sync1 so
        // FG OFF keeps a 60-Hz ceiling instead of becoming uncapped.
        PtFgPacerWaitMemberSlots(1);
        g_ptarFgPacer.pairBudget=2;
        g_ptarFgPacer.pairFirstSlots=1;
        g_ptarFgPacer.pairSecondSlots=1;
        return;
    }

    PtFgPacerWaitMemberSlots(g_ptarFgPacer.pairSecondSlots);
}

static void PtFgPacerRecordVisible(bool generated)
{
    PtFgPacerInit();

    const LONGLONG now=PtFgPacerNow();
    g_ptarFgPacer.lastVisibleQpc=now;
    g_ptarFgPacer.nextVisibleQpc=now;
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
    return g_ptarFgPacer.generatedLateSkips;
}
