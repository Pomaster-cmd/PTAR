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

    // Establish a real-frame timing anchor.
    PtFgPacerRecordVisible(false);

    LARGE_INTEGER beforeG={0},afterG={0};
    QueryPerformanceCounter(&beforeG);
    const bool generate=PtFgPacerPrepareGenerated();
    QueryPerformanceCounter(&afterG);

    if(!generate)
    {
        std::printf("FAIL midpoint unexpectedly rejected\n");
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

    // Broad CI tolerances: the semantic gate is ordering and the absence of a
    // gross oversleep. Hosted VM scheduling can add several milliseconds.
    if(waitG<6.0 || waitG>45.0)
    {
        std::printf("FAIL midpoint wait outside tolerance\n");
        return 11;
    }
    if(waitR<4.0 || waitR>45.0)
    {
        std::printf("FAIL real wait outside tolerance\n");
        return 12;
    }
    if(pair<22.0 || pair>85.0)
    {
        std::printf("FAIL pair duration outside tolerance\n");
        return 13;
    }

    // Late-source policy: after a fresh REAL anchor, deliberately miss the
    // midpoint + tolerance. GENERATED must be rejected.
    PtFgPacerRecordVisible(false);
    Sleep(30);
    const bool lateGenerate=PtFgPacerPrepareGenerated();

    std::printf(
        "FG_PACER_LATE_GENERATE=%d\n"
        "FG_PACER_LATE_SKIP_COUNT=%lu\n",
        lateGenerate?1:0,
        PtFgPacerLateSkipCount());

    if(lateGenerate)
    {
        std::printf("FAIL late midpoint was accepted\n");
        return 14;
    }
    if(PtFgPacerLateSkipCount()==0)
    {
        std::printf("FAIL late-skip counter not incremented\n");
        return 15;
    }

    if(PtFgPacerGeneratedCount()==0 || PtFgPacerRealCount()<2)
    {
        std::printf("FAIL presentation counters inconsistent\n");
        return 16;
    }

    QueryPerformanceCounter(&q2);
    std::printf(
        "FG_PACER_TOTAL_TEST_MS=%.3f\n",
        Ms(q2.QuadPart-q0.QuadPart,freq));
    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
