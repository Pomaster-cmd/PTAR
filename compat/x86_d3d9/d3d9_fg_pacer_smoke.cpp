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

    // Existing D3D11 MAINPERF2/PAIRBAL2 selector contract.
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

    // Single-clock regression: with device Present forced IMMEDIATE, the
    // existing software grid must provide a real slot to both G and R.
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
    std::printf("PAIRBAL2_SINGLECLOCK_WAIT_G_MS=%.3f\n",waitG);
    std::printf("PAIRBAL2_SINGLECLOCK_WAIT_R_MS=%.3f\n",waitR);

    if(waitG<8.0 || waitG>55.0) { std::printf("FAIL generated member slot\n"); return 15; }
    if(waitR<8.0 || waitR>55.0) { std::printf("FAIL real member slot\n"); return 16; }

    const double ratio=(waitG>waitR)?waitG/waitR:waitR/waitG;
    std::printf("PAIRBAL2_SINGLECLOCK_WAIT_RATIO=%.3f\n",ratio);
    if(ratio>2.5) { std::printf("FAIL pair member imbalance\n"); return 17; }

    // FG OFF must keep the existing one-slot software Sync1 ceiling now that
    // the device Present itself is IMMEDIATE.
    PtFgPacerReset();
    PtFgPacerRecordVisible(false);
    LARGE_INTEGER off0={0},off1={0};
    QueryPerformanceCounter(&off0);
    PtFgPacerPrepareReal(false);
    QueryPerformanceCounter(&off1);
    const double offWait=Ms(off1.QuadPart-off0.QuadPart,freq);
    std::printf("PAIRBAL2_FG_OFF_SYNC1_WAIT_MS=%.3f\n",offWait);
    if(offWait<8.0 || offWait>55.0) { std::printf("FAIL FG OFF software Sync1 slot\n"); return 18; }

    // A long source stall must resynchronise fail-open and never create a skip
    // storm or a compensating second wait.
    PtFgPacerReset();
    PtFgPacerRecordVisible(false);
    Sleep(60);
    const unsigned long resyncBefore=g_ptarFgPacer.resyncs;
    LARGE_INTEGER stall0={0},stall1={0};
    QueryPerformanceCounter(&stall0);
    if(!PtFgPacerPrepareGenerated()) return 19;
    QueryPerformanceCounter(&stall1);
    const double stallWait=Ms(stall1.QuadPart-stall0.QuadPart,freq);
    std::printf("PAIRBAL2_STALL_WAIT_MS=%.3f\n",stallWait);
    std::printf("PAIRBAL2_RESYNC_COUNT=%lu\n",g_ptarFgPacer.resyncs);
    if(g_ptarFgPacer.resyncs<=resyncBefore) { std::printf("FAIL stale grid did not resync\n"); return 20; }
    if(stallWait>12.0) { std::printf("FAIL stale pair waited instead of resync\n"); return 21; }
    if(PtFgPacerLateSkipCount()!=0) { std::printf("FAIL late skip reintroduced\n"); return 22; }

    PtFgPacerRecordVisible(true);
    PtFgPacerPrepareReal(true);
    PtFgPacerRecordVisible(false);

    std::printf("D3D11_MAINPERF2_PAIRBAL2_SELECTOR_PORT=PASS\n");
    std::printf("D3D9_PRESENT_CLOCK=SOFTWARE_PAIRBAL2_ONLY\n");
    std::printf("D3D9_DEVICE_PRESENT_INTERVAL=IMMEDIATE_REQUIRED\n");
    std::printf("D3D9_FG_OFF_SOFTWARE_SYNC1=PASS\n");
    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
