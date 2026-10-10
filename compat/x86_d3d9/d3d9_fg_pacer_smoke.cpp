#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cmath>
#pragma warning(disable:4505)
#include "ptar_diag.h"
#include "ptar_fg_pacer.h"

static double Ms(LONGLONG ticks,LONGLONG freq)
{
    return 1000.0*(double)ticks/(double)freq;
}

static bool CheckPair(unsigned int budget,unsigned int first,unsigned int second,
                      unsigned int eb,unsigned int ef,unsigned int es,
                      const char* label)
{
    const bool ok=budget==eb && first==ef && second==es && first>=1 && first<=2 && second>=1 && second<=2;
    std::printf("PAIRBAL2_%s budget=%u split=%u+%u expected=%u/%u+%u %s\n",
        label,budget,first,second,eb,ef,es,ok?"PASS":"FAIL");
    return ok;
}

int main()
{
    PtFgPacerReset();
    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;

    // Exact D3D11 MAINPERF2 PAIRBAL2 selector contract.
    PtFgPacerSelectPairBudget(PtFgPacerMsTicks(20));
    if(!CheckPair(g_ptarFgPacer.pairBudget,g_ptarFgPacer.pairFirstSlots,g_ptarFgPacer.pairSecondSlots,2,1,1,"LOW_20MS")) return 10;

    PtFgPacerReset();
    PtFgPacerSelectPairBudget(PtFgPacerMsTicks(80));
    if(!CheckPair(g_ptarFgPacer.pairBudget,g_ptarFgPacer.pairFirstSlots,g_ptarFgPacer.pairSecondSlots,4,2,2,"HIGH_80MS")) return 11;

    PtFgPacerReset();
    PtFgPacerSelectPairBudget(PtFgPacerMsTicks(50));
    if(!CheckPair(g_ptarFgPacer.pairBudget,g_ptarFgPacer.pairFirstSlots,g_ptarFgPacer.pairSecondSlots,3,1,2,"MID_50MS_A")) return 12;
    PtFgPacerSelectPairBudget(PtFgPacerMsTicks(50));
    if(!CheckPair(g_ptarFgPacer.pairBudget,g_ptarFgPacer.pairFirstSlots,g_ptarFgPacer.pairSecondSlots,3,2,1,"MID_50MS_B")) return 13;

    // Runtime member placement: both GENERATED and REAL must receive a real
    // display slot. This is the regression gate for the field 35 ms / 5 ms
    // imbalance. Anchoring is to the previous successful Present timestamp.
    PtFgPacerReset();
    PtFgPacerRecordVisible(false);

    LARGE_INTEGER beforeG={0},afterG={0},beforeR={0},afterR={0};
    QueryPerformanceCounter(&beforeG);
    if(!PtFgPacerPrepareGenerated()) return 14;
    QueryPerformanceCounter(&afterG);
    PtFgPacerRecordVisible(true);

    QueryPerformanceCounter(&beforeR);
    PtFgPacerPrepareReal(true);
    QueryPerformanceCounter(&afterR);
    PtFgPacerRecordVisible(false);

    const double waitG=Ms(afterG.QuadPart-beforeG.QuadPart,freq);
    const double waitR=Ms(afterR.QuadPart-beforeR.QuadPart,freq);
    std::printf("PAIRBAL2_MEMBER_WAIT_G_MS=%.3f\n",waitG);
    std::printf("PAIRBAL2_MEMBER_WAIT_R_MS=%.3f\n",waitR);

    // Scheduler jitter on hosted CI can be wide, but neither member may be
    // the old near-immediate 0..5 ms slot when the pair is healthy.
    if(waitG<8.0 || waitG>55.0) { std::printf("FAIL generated member slot\n"); return 15; }
    if(waitR<8.0 || waitR>55.0) { std::printf("FAIL real member slot\n"); return 16; }

    const double ratio=(waitG>waitR)?waitG/waitR:waitR/waitG;
    std::printf("PAIRBAL2_MEMBER_WAIT_RATIO=%.3f\n",ratio);
    if(ratio>2.5) { std::printf("FAIL pair member imbalance\n"); return 17; }

    // A long source stall must resynchronise fail-open, never revive the old
    // generated-frame rejection/skip storm.
    const unsigned long resyncBefore=g_ptarFgPacer.resyncs;
    Sleep(60);
    LARGE_INTEGER stall0={0},stall1={0};
    QueryPerformanceCounter(&stall0);
    if(!PtFgPacerPrepareGenerated()) return 18;
    QueryPerformanceCounter(&stall1);
    const double stallWait=Ms(stall1.QuadPart-stall0.QuadPart,freq);
    std::printf("PAIRBAL2_STALL_WAIT_MS=%.3f\n",stallWait);
    std::printf("PAIRBAL2_RESYNC_COUNT=%lu\n",g_ptarFgPacer.resyncs);
    if(g_ptarFgPacer.resyncs<=resyncBefore) { std::printf("FAIL stale grid did not resync\n"); return 19; }
    if(stallWait>12.0) { std::printf("FAIL stale pair waited instead of resync\n"); return 20; }
    if(PtFgPacerLateSkipCount()!=0) { std::printf("FAIL late skip reintroduced\n"); return 21; }

    PtFgPacerRecordVisible(true);
    PtFgPacerPrepareReal(true);
    PtFgPacerRecordVisible(false);

    if(PtFgPacerGeneratedCount()<2 || PtFgPacerRealCount()<3)
    {
        std::printf("FAIL presentation counters inconsistent\n");
        return 22;
    }

    std::printf("D3D11_MAINPERF2_PAIRBAL2_SELECTOR_PORT=PASS\n");
    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
