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

    LARGE_INTEGER beforeG={0},afterG={0};
    LARGE_INTEGER beforeR={0},afterR={0};

    QueryPerformanceCounter(&beforeG);
    const bool generate=PtFgPacerPrepareGenerated();
    QueryPerformanceCounter(&afterG);

    QueryPerformanceCounter(&beforeR);
    PtFgPacerPrepareReal(true);
    QueryPerformanceCounter(&afterR);

    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;
    const double waitG=Ms(
        afterG.QuadPart-beforeG.QuadPart,freq);
    const double waitR=Ms(
        afterR.QuadPart-beforeR.QuadPart,freq);

    std::printf(
        "FG_PRODUCER_GENERATED_PREP_MS=%.3f\n",
        waitG);
    std::printf(
        "FG_PRODUCER_REAL_PREP_MS=%.3f\n",
        waitR);

    if(!generate)
        return 10;

    // Producer-side compatibility helpers must not pace the game thread.
    if(waitG>2.0 || waitR>2.0)
    {
        std::printf("FAIL producer-side pacing wait detected\n");
        return 11;
    }

    PtFgPacerRecordVisible(false);
    Sleep(17);
    PtFgPacerRecordVisible(true);
    Sleep(17);
    PtFgPacerRecordVisible(false);

    const double visible=PtFgPacerVisibleFps();
    std::printf(
        "FG_VISIBLE_RATE_SAMPLE=%.3f\n",
        visible);
    std::printf(
        "FG_VISIBLE_REAL_COUNT=%lu\n",
        PtFgPacerRealCount());
    std::printf(
        "FG_VISIBLE_GENERATED_COUNT=%lu\n",
        PtFgPacerGeneratedCount());
    std::printf(
        "FG_PACER_RESYNC_COUNT=%lu\n",
        g_ptarFgPacer.resyncs);
    std::printf(
        "FG_PACER_LATE_SKIP_COUNT=%lu\n",
        PtFgPacerLateSkipCount());
    std::printf(
        "FG_PACER_WAIT_YIELDS=%lu\n",
        g_ptarFgPacer.waitYields);

    if(PtFgPacerRealCount()!=2 ||
       PtFgPacerGeneratedCount()!=1)
        return 12;

    if(g_ptarFgPacer.resyncs!=0 ||
       PtFgPacerLateSkipCount()!=0 ||
       g_ptarFgPacer.waitYields!=0)
    {
        std::printf("FAIL legacy producer pacer activity returned\n");
        return 13;
    }

    if(visible<20.0 || visible>120.0)
        return 14;

    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
