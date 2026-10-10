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

static double TwoSyntheticSync1Presents()
{
    LARGE_INTEGER a={0},b={0};
    QueryPerformanceCounter(&a);
    Sleep(16);
    Sleep(16);
    QueryPerformanceCounter(&b);
    return Ms(b.QuadPart-a.QuadPart,g_ptarFgPacer.frequency.QuadPart);
}

int main()
{
    PtFgPacerReset();
    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;

    // D3D11 MAINPERF2/PAIRBAL2 architecture: presentation synchronization is
    // owned by Present itself. D3D9's directly representable branch therefore
    // must add no software pacing delay around the synchronized Present calls.
    PtFgPacerRecordVisible(false);

    LARGE_INTEGER g0={0},g1={0},r0={0},r1={0};
    QueryPerformanceCounter(&g0);
    const bool generate=PtFgPacerPrepareGenerated();
    QueryPerformanceCounter(&g1);
    if(!generate)
    {
        std::printf("FAIL generated frame rejected\n");
        return 10;
    }

    PtFgPacerRecordVisible(true);

    QueryPerformanceCounter(&r0);
    PtFgPacerPrepareReal(true);
    QueryPerformanceCounter(&r1);
    PtFgPacerRecordVisible(false);

    const double waitG=Ms(g1.QuadPart-g0.QuadPart,freq);
    const double waitR=Ms(r1.QuadPart-r0.QuadPart,freq);

    std::printf("PAIRBAL2_D3D9_SOFTWARE_WAIT_G_MS=%.3f\n",waitG);
    std::printf("PAIRBAL2_D3D9_SOFTWARE_WAIT_R_MS=%.3f\n",waitR);
    std::printf("PAIRBAL2_D3D9_WAIT_YIELDS=%lu\n",g_ptarFgPacer.waitYields);
    std::printf("PAIRBAL2_D3D9_RESYNCS=%lu\n",g_ptarFgPacer.resyncs);
    std::printf("PAIRBAL2_D3D9_LATE_SKIPS=%lu\n",PtFgPacerLateSkipCount());

    if(waitG>5.0)
    {
        std::printf("FAIL generated software wait reintroduced\n");
        return 11;
    }
    if(waitR>5.0)
    {
        std::printf("FAIL real software wait reintroduced\n");
        return 12;
    }
    if(g_ptarFgPacer.waitYields!=0)
    {
        std::printf("FAIL software wait/yield path active\n");
        return 13;
    }
    if(g_ptarFgPacer.resyncs!=0)
    {
        std::printf("FAIL local-grid resync path active\n");
        return 14;
    }
    if(PtFgPacerLateSkipCount()!=0)
    {
        std::printf("FAIL generated late-skip path active\n");
        return 15;
    }

    // Robust double-wait regression. Hosted Windows runners may overshoot
    // Sleep(16) substantially, so first measure the exact same two synthetic
    // driver waits as a baseline. Then insert PTAR Prepare*/RecordVisible calls
    // around the same waits. The PTAR model may add only a small bookkeeping
    // delta; the old FIELDHOTFIX3 would add ~33 ms of extra QPC pacing here.
    const double baselineMs=TwoSyntheticSync1Presents();

    PtFgPacerReset();
    PtFgPacerRecordVisible(false);
    LARGE_INTEGER pair0={0},pair1={0};
    QueryPerformanceCounter(&pair0);
    if(!PtFgPacerPrepareGenerated()) return 16;
    Sleep(16); // stand-in for synchronized GENERATED Present
    PtFgPacerRecordVisible(true);
    PtFgPacerPrepareReal(true);
    Sleep(16); // stand-in for synchronized REAL Present
    PtFgPacerRecordVisible(false);
    QueryPerformanceCounter(&pair1);

    const double modelMs=Ms(pair1.QuadPart-pair0.QuadPart,freq);
    const double addedMs=modelMs-baselineMs;
    std::printf("PAIRBAL2_D3D9_SYNC1_BASELINE_MS=%.3f\n",baselineMs);
    std::printf("PAIRBAL2_D3D9_SYNC1_MODEL_MS=%.3f\n",modelMs);
    std::printf("PAIRBAL2_D3D9_SYNC1_ADDED_MS=%.3f\n",addedMs);

    if(addedMs>12.0)
    {
        std::printf("FAIL synchronized-Present model contains extra pacing delay\n");
        return 17;
    }

    // Long source work must not cause a compensating software wait. That is
    // important on the exact slow scenes where FIELDHOTFIX3 destroyed REAL FPS.
    Sleep(60);
    LARGE_INTEGER s0={0},s1={0};
    QueryPerformanceCounter(&s0);
    if(!PtFgPacerPrepareGenerated())
        return 18;
    QueryPerformanceCounter(&s1);
    const double stallWait=Ms(s1.QuadPart-s0.QuadPart,freq);
    std::printf("PAIRBAL2_D3D9_AFTER_STALL_WAIT_MS=%.3f\n",stallWait);
    if(stallWait>5.0)
    {
        std::printf("FAIL post-stall software wait reintroduced\n");
        return 19;
    }

    PtFgPacerRecordVisible(true);
    PtFgPacerPrepareReal(true);
    PtFgPacerRecordVisible(false);

    if(PtFgPacerGeneratedCount()!=2 || PtFgPacerRealCount()!=3)
    {
        std::printf("FAIL presentation counters inconsistent G=%lu R=%lu\n",
            PtFgPacerGeneratedCount(),PtFgPacerRealCount());
        return 20;
    }

    std::printf("D3D11_PAIRBAL2_D3D9_SYNC1_POLICY_PORT=PASS\n");
    std::printf("D3D9_NO_EXTRA_SOFTWARE_PACING=PASS\n");
    std::printf("D3D9_SYNC1_DOUBLE_WAIT_REGRESSION=PASS\n");
    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
