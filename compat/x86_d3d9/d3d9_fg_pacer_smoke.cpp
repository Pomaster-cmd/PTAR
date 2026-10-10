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
                      unsigned int eb,unsigned int ef,unsigned int es,const char* label)
{
    const bool ok=budget==eb && first==ef && second==es;
    std::printf("PAIRBAL2_%s budget=%u split=%u+%u expected=%u/%u+%u %s\n",
        label,budget,first,second,eb,ef,es,ok?"PASS":"FAIL");
    return ok;
}

static void SeedSourceAgeMs(unsigned int ms)
{
    const LONGLONG now=PtFgPacerNow();
    g_ptarFgPacer.lastRealQpc=now-PtFgPacerMsTicks(ms);
    g_ptarFgPacer.lastVisibleQpc=g_ptarFgPacer.lastRealQpc;
}

int main()
{
    PtFgPacerReset();
    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;

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

    // Deterministic field regression model. Do not use Sleep(26): a hosted
    // Windows scheduler can overshoot it by 10+ ms and invalidate the model.
    // Seed the previous REAL exactly 26 ms in the past instead.
    PtFgPacerReset();
    SeedSourceAgeMs(26);

    LARGE_INTEGER g0={0},g1={0},r0={0},r1={0};
    QueryPerformanceCounter(&g0);
    if(!PtFgPacerPrepareGenerated()) return 14;
    QueryPerformanceCounter(&g1);
    PtFgPacerRecordVisible(true);

    QueryPerformanceCounter(&r0);
    PtFgPacerPrepareReal(true);
    QueryPerformanceCounter(&r1);
    PtFgPacerRecordVisible(false);

    const double waitG=Ms(g1.QuadPart-g0.QuadPart,freq);
    const double waitR=Ms(r1.QuadPart-r0.QuadPart,freq);
    const double source=Ms(g_ptarFgPacer.lastSourceWorkQpc,freq);
    std::printf("PAIR_DEADLINE_SOURCE_WORK_MS=%.3f\n",source);
    std::printf("PAIR_DEADLINE_GENERATED_WAIT_MS=%.3f\n",waitG);
    std::printf("PAIR_DEADLINE_REAL_SLACK_WAIT_MS=%.3f\n",waitR);
    std::printf("PAIR_DEADLINE_BUDGET=%u\n",g_ptarFgPacer.pairBudget);

    if(g_ptarFgPacer.pairBudget!=2) return 15;
    if(source<25.0 || source>28.0) { std::printf("FAIL deterministic source model drift\n"); return 16; }
    if(waitG>5.0) { std::printf("FAIL full member wait remains before G\n"); return 17; }
    if(waitR<2.0 || waitR>14.0) { std::printf("FAIL remaining pair slack outside expected range\n"); return 18; }
    if(waitR>12.0) { std::printf("FAIL FIELDHOTFIX5 full-slot wait regression\n"); return 19; }

    // Fast source: 10 ms already consumed, so the total two-slot deadline
    // leaves about 23.3 ms rather than adding two member waits.
    PtFgPacerReset();
    SeedSourceAgeMs(10);
    LARGE_INTEGER f0={0},f1={0},f2={0};
    QueryPerformanceCounter(&f0);
    if(!PtFgPacerPrepareGenerated()) return 20;
    QueryPerformanceCounter(&f1);
    PtFgPacerRecordVisible(true);
    PtFgPacerPrepareReal(true);
    QueryPerformanceCounter(&f2);
    PtFgPacerRecordVisible(false);
    const double fastG=Ms(f1.QuadPart-f0.QuadPart,freq);
    const double fastSlack=Ms(f2.QuadPart-f1.QuadPart,freq);
    std::printf("PAIR_DEADLINE_FAST_G_WAIT_MS=%.3f\n",fastG);
    std::printf("PAIR_DEADLINE_FAST_REAL_SLACK_MS=%.3f\n",fastSlack);
    if(fastG>5.0 || fastSlack<16.0 || fastSlack>32.0) return 21;

    // Explicitly seed an already missed pair deadline. PrepareReal must fail
    // open and must not add a compensating full-slot delay.
    PtFgPacerReset();
    SeedSourceAgeMs(26);
    if(!PtFgPacerPrepareGenerated()) return 22;
    PtFgPacerRecordVisible(true);
    g_ptarFgPacer.nextVisibleQpc=PtFgPacerNow()-PtFgPacerMsTicks(10);
    LARGE_INTEGER s0={0},s1={0};
    QueryPerformanceCounter(&s0);
    PtFgPacerPrepareReal(true);
    QueryPerformanceCounter(&s1);
    const double lateWait=Ms(s1.QuadPart-s0.QuadPart,freq);
    PtFgPacerRecordVisible(false);
    std::printf("PAIR_DEADLINE_LATE_REAL_WAIT_MS=%.3f\n",lateWait);
    if(lateWait>5.0) return 23;

    // FG OFF: one-slot 60-Hz ceiling on the same IMMEDIATE device clock.
    PtFgPacerReset();
    PtFgPacerRecordVisible(false);
    LARGE_INTEGER o0={0},o1={0};
    QueryPerformanceCounter(&o0);
    PtFgPacerPrepareReal(false);
    QueryPerformanceCounter(&o1);
    const double offWait=Ms(o1.QuadPart-o0.QuadPart,freq);
    std::printf("PAIR_DEADLINE_FG_OFF_SYNC1_MS=%.3f\n",offWait);
    if(offWait<8.0 || offWait>40.0) return 24;

    if(PtFgPacerLateSkipCount()!=0) return 25;

    std::printf("D3D11_MAINPERF2_PAIRBAL2_SELECTOR_PORT=PASS\n");
    std::printf("D3D9_PAIR_DEADLINE_TOTAL_BUDGET=PASS\n");
    std::printf("D3D9_NO_PRE_GENERATED_MEMBER_WAIT=PASS\n");
    std::printf("D3D9_FIELD_26MS_TARGET_60_MODEL=PASS\n");
    std::printf("D3D9_DEVICE_PRESENT_INTERVAL=IMMEDIATE_REQUIRED\n");
    std::printf("D3D9_FG_OFF_SOFTWARE_SYNC1=PASS\n");
    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
