#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#pragma warning(disable:4505)
#include "ptar_diag.h"
#include "ptar_fg_pacer.h"

static double Ms(LONGLONG ticks,LONGLONG freq)
{
    return 1000.0*(double)ticks/(double)freq;
}

int main()
{
    PtFgPacerReset();

    LARGE_INTEGER q0={0},q1={0},q2={0};
    QueryPerformanceCounter(&q0);

    // Establish REAL history. PRODPORT1 does not wait for a historical
    // midpoint that may already have elapsed while CURRENT + ME were built.
    PtFgPacerRecordVisible(false);

    LARGE_INTEGER beforeG={0},afterG={0};
    QueryPerformanceCounter(&beforeG);
    const bool generate=PtFgPacerPrepareGenerated();
    QueryPerformanceCounter(&afterG);

    if(!generate)
    {
        std::printf("FAIL generated frame unexpectedly rejected\n");
        return 10;
    }

    PtFgPacerRecordVisible(true);

    LARGE_INTEGER beforeR={0},afterR={0};
    QueryPerformanceCounter(&beforeR);
    PtFgPacerPrepareReal(true);
    QueryPerformanceCounter(&afterR);
    PtFgPacerRecordVisible(false);
    QueryPerformanceCounter(&q1);

    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;
    const double waitG=Ms(afterG.QuadPart-beforeG.QuadPart,freq);
    const double waitR=Ms(afterR.QuadPart-beforeR.QuadPart,freq);
    const double pair=Ms(q1.QuadPart-q0.QuadPart,freq);

    std::printf("FG_PACER_GENERATED_WAIT_MS=%.3f\n",waitG);
    std::printf("FG_PACER_REAL_WAIT_MS=%.3f\n",waitR);
    std::printf("FG_PACER_PAIR_MS=%.3f\n",pair);
    std::printf("FG_PACER_VISIBLE_FPS=%.3f\n",PtFgPacerVisibleFps());

    // First GENERATED should be immediate/near-immediate because it is already
    // ready; REAL should occupy the next nominal visible slot.
    if(waitG<0.0 || waitG>12.0)
    {
        std::printf("FAIL first generated wait outside PRODPORT1 tolerance\n");
        return 11;
    }
    if(waitR<8.0 || waitR>45.0)
    {
        std::printf("FAIL real-slot wait outside tolerance\n");
        return 12;
    }
    if(pair<8.0 || pair>60.0)
    {
        std::printf("FAIL first pair duration outside tolerance\n");
        return 13;
    }

    // Normal next pair: after a successful REAL, the next GENERATED should
    // land one nominal visible period later, preserving G/R alternation.
    LARGE_INTEGER beforeG2={0},afterG2={0};
    QueryPerformanceCounter(&beforeG2);
    const bool generate2=PtFgPacerPrepareGenerated();
    QueryPerformanceCounter(&afterG2);
    if(!generate2)
    {
        std::printf("FAIL second generated frame unexpectedly rejected\n");
        return 14;
    }

    const double waitG2=Ms(afterG2.QuadPart-beforeG2.QuadPart,freq);
    std::printf("FG_PACER_NEXT_GENERATED_WAIT_MS=%.3f\n",waitG2);
    if(waitG2<7.0 || waitG2>45.0)
    {
        std::printf("FAIL next generated slot outside tolerance\n");
        return 15;
    }

    PtFgPacerRecordVisible(true);
    PtFgPacerPrepareReal(true);
    PtFgPacerRecordVisible(false);

    // Simulate a source/driver stall that would previously have caused a
    // cascade of late-midpoint rejects. PRODPORT1 must accept GENERATED and
    // resynchronise the local grid instead of incrementing LATE_SKIP.
    const unsigned long resyncBefore=g_ptarFgPacer.resyncs;
    Sleep(45);

    LARGE_INTEGER beforeStallG={0},afterStallG={0};
    QueryPerformanceCounter(&beforeStallG);
    const bool stallGenerate=PtFgPacerPrepareGenerated();
    QueryPerformanceCounter(&afterStallG);

    const double stallWait=Ms(afterStallG.QuadPart-beforeStallG.QuadPart,freq);
    const unsigned long resyncAfter=g_ptarFgPacer.resyncs;

    std::printf("FG_PACER_STALL_GENERATE=%d\n",stallGenerate?1:0);
    std::printf("FG_PACER_STALL_WAIT_MS=%.3f\n",stallWait);
    std::printf("FG_PACER_RESYNC_COUNT=%lu\n",resyncAfter);
    std::printf("FG_PACER_LATE_SKIP_COUNT=%lu\n",PtFgPacerLateSkipCount());

    if(!stallGenerate)
    {
        std::printf("FAIL stalled pair was rejected instead of resynchronised\n");
        return 16;
    }
    if(resyncAfter<=resyncBefore)
    {
        std::printf("FAIL stale local grid did not resynchronise\n");
        return 17;
    }
    if(PtFgPacerLateSkipCount()!=0)
    {
        std::printf("FAIL PRODPORT1 reintroduced historical late skips\n");
        return 18;
    }
    if(stallWait>12.0)
    {
        std::printf("FAIL stale pair resync waited too long\n");
        return 19;
    }

    PtFgPacerRecordVisible(true);
    PtFgPacerPrepareReal(true);
    PtFgPacerRecordVisible(false);

    if(PtFgPacerGeneratedCount()<3 || PtFgPacerRealCount()<4)
    {
        std::printf("FAIL presentation counters inconsistent\n");
        return 20;
    }

    QueryPerformanceCounter(&q2);
    std::printf(
        "FG_PACER_TOTAL_TEST_MS=%.3f\n",
        Ms(q2.QuadPart-q0.QuadPart,freq));
    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
