#pragma once

#include <windows.h>
#include "ptar_runtime_metrics.h"

// FIELDHOTFIX6 keeps the existing D3D11 MAINPERF2/PAIRBAL2 selector and the
// FIELDHOTFIX5 single-clock D3D9 integration (device Present = IMMEDIATE).
// The field trace from 2026-10-10 proved the remaining defect: FIELDHOTFIX5
// treated the PairBal2 member slots as waits that happen AFTER source work.
// With ~26 ms of source+FG work this produced ~26 ms REAL dwell + ~17 ms
// GENERATED dwell => ~43 ms/pair (~45 visible FPS), although the game is >30
// FPS without FG.
//
// The PairBal2 budget is a TOTAL REAL->REAL pair deadline, not extra latency.
// Correct D3D9 mapping:
//   1) measure source work since the last successful REAL Present;
//   2) keep the existing PairBal2 budget selector (2..4 60-Hz slots);
//   3) present GENERATED immediately when the pair is ready;
//   4) wait only the REMAINING slack until last REAL + pairBudget*period;
//   5) present REAL and start the next pair.
//
// For the field case (~26 ms work, budget=2), only ~7 ms of slack is added,
// targeting a ~33.3 ms REAL->REAL pair / ~60 mixed Presents per second instead
// of the broken ~43 ms pair. This does not invent a second cadence authority:
// the D3D9 device remains IMMEDIATE and PairBal2 owns the sole deadline.
//
// FG OFF retains a one-slot (60-Hz) software ceiling on the same clock.
// Conviction hardening is retained: kernel32!Sleep is resolved indirectly so
// Ultimate ASI Loader's EXE Sleep-IAT hook is bypassed.

typedef VOID (WINAPI *PTFN_PtarNativeSleep)(DWORD);

struct PTFGPacerState
{
    LARGE_INTEGER frequency;
    LONGLONG lastRealQpc;
    LONGLONG lastVisibleQpc;
    LONGLONG nextVisibleQpc;       // current REAL deadline
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
        g_ptarFgNativeSleep=(PTFN_PtarNativeSleep)GetProcAddress(kernel32,"Sleep");
    PtDiagLogA("FG_PACER_NATIVE_SLEEP resolved=%p policy=INDIRECT_KERNEL32_EXPORT",(void*)g_ptarFgNativeSleep);
    return g_ptarFgNativeSleep;
}

static void PtFgPacerYieldCoarse()
{
    PTFN_PtarNativeSleep sleepFn=PtFgPacerResolveNativeSleep();
    if(sleepFn) sleepFn(0); else SwitchToThread();
}

static void PtFgPacerInit()
{
    if(g_ptarFgPacer.initialized) return;
    QueryPerformanceFrequency(&g_ptarFgPacer.frequency);
    if(g_ptarFgPacer.frequency.QuadPart<=0) g_ptarFgPacer.frequency.QuadPart=1;
    g_ptarFgPacer.initialized=true;
    PtDiagLogA("FG_PACER_INIT qpc_freq=%lld policy=D3D11_MAINPERF2_PAIRBAL2_D3D9_PAIR_DEADLINE",(long long)g_ptarFgPacer.frequency.QuadPart);
}

static LONGLONG PtFgPacerNow()
{
    LARGE_INTEGER now={}; QueryPerformanceCounter(&now); return now.QuadPart;
}

static LONGLONG PtFgPacerPeriodTicks()
{
    PtFgPacerInit();
    const LONGLONG ticks=g_ptarFgPacer.frequency.QuadPart/60;
    return ticks>0?ticks:1;
}

static LONGLONG PtFgPacerMsTicks(unsigned int ms)
{
    PtFgPacerInit();
    const LONGLONG ticks=(g_ptarFgPacer.frequency.QuadPart*(LONGLONG)ms)/1000;
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
    PtDiagLogA("FG_PACER_RESET policy=D3D11_MAINPERF2_PAIRBAL2_D3D9_PAIR_DEADLINE");
}

static void PtFgPacerWaitUntil(LONGLONG target)
{
    if(target<=0) return;
    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;
    const LONGLONG coarseThreshold=freq/500;
    for(;;)
    {
        const LONGLONG now=PtFgPacerNow();
        const LONGLONG remain=target-now;
        if(remain<=0) break;
        if(remain>coarseThreshold) PtFgPacerYieldCoarse(); else SwitchToThread();
        ++g_ptarFgPacer.waitYields;
    }
}

static void PtFgPacerSelectPairBudget(LONGLONG sourceWorkQpc)
{
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
        if(g_ptarFgPacer.pairAccQpc>=width){g_ptarFgPacer.pairAccQpc-=width;++budget;}
        if(g_ptarFgPacer.pairAccQpc>=width){g_ptarFgPacer.pairAccQpc-=width;++budget;}
    }

    unsigned int first=1,second=1;
    if(budget==4){first=2;second=2;}
    else if(budget==3)
    {
        g_ptarFgPacer.budget3Orientation=!g_ptarFgPacer.budget3Orientation;
        if(g_ptarFgPacer.budget3Orientation){first=1;second=2;} else {first=2;second=1;}
    }

    g_ptarFgPacer.pairBudget=budget;
    g_ptarFgPacer.pairFirstSlots=first;
    g_ptarFgPacer.pairSecondSlots=second;
    PtDiagLogA("FG_PAIRBAL2 source_work_ticks=%lld budget=%u split=%u+%u acc=%lld",(long long)sourceWorkQpc,budget,first,second,(long long)g_ptarFgPacer.pairAccQpc);
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

    // PairBal2 budget applies to the whole REAL->REAL pair. GENERATED is
    // already late relative to the ideal midpoint because CURRENT had to be
    // rendered first, so never spend another member-slot wait before it.
    if(g_ptarFgPacer.lastRealQpc>0)
        g_ptarFgPacer.nextVisibleQpc=
            g_ptarFgPacer.lastRealQpc +
            PtFgPacerPeriodTicks()*(LONGLONG)g_ptarFgPacer.pairBudget;
    else
        g_ptarFgPacer.nextVisibleQpc=now;

    PtDiagLogA("FG_PAIR_DEADLINE source_work_ticks=%lld budget=%u remaining_ticks=%lld",
        (long long)sourceWork,
        g_ptarFgPacer.pairBudget,
        (long long)(g_ptarFgPacer.nextVisibleQpc-now));
    return true;
}

static void PtFgPacerPrepareReal(bool fgPair)
{
    PtFgPacerInit();
    const LONGLONG now=PtFgPacerNow();

    if(!fgPair)
    {
        // IMMEDIATE device Present, software equivalent of Sync1 for FG OFF.
        if(g_ptarFgPacer.lastRealQpc>0)
        {
            const LONGLONG target=g_ptarFgPacer.lastRealQpc+PtFgPacerPeriodTicks();
            if(now<target) PtFgPacerWaitUntil(target);
        }
        g_ptarFgPacer.nextVisibleQpc=0;
        g_ptarFgPacer.pairBudget=2;
        g_ptarFgPacer.pairFirstSlots=1;
        g_ptarFgPacer.pairSecondSlots=1;
        return;
    }

    // Wait only unused pair slack. Never add a full slot after source work.
    if(g_ptarFgPacer.nextVisibleQpc>0)
    {
        if(now<g_ptarFgPacer.nextVisibleQpc)
        {
            PtFgPacerWaitUntil(g_ptarFgPacer.nextVisibleQpc);
        }
        else if(now>g_ptarFgPacer.nextVisibleQpc+PtFgPacerPeriodTicks())
        {
            ++g_ptarFgPacer.resyncs;
            PtDiagLogA("FG_PAIR_DEADLINE_MISSED late_ticks=%lld budget=%u resyncs=%lu",
                (long long)(now-g_ptarFgPacer.nextVisibleQpc),
                g_ptarFgPacer.pairBudget,
                g_ptarFgPacer.resyncs);
        }
    }
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
        g_ptarFgPacer.nextVisibleQpc=0;
    }
}

static double PtFgPacerVisibleFps(){return PtRollingRateValue(&g_ptarFgPacer.visibleRate);}
static unsigned long PtFgPacerGeneratedCount(){return g_ptarFgPacer.generatedPresents;}
static unsigned long PtFgPacerRealCount(){return g_ptarFgPacer.realPresents;}
static unsigned long PtFgPacerLateSkipCount(){return g_ptarFgPacer.generatedLateSkips;}
