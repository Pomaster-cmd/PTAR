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
    const LONGLONG freq=g_ptarFgPacer.frequency.QuadPart;

    // D3D11 GW16F/G NOLOCK30_1 contract: no software pacing delay exists
    // around Present. The synchronized Present itself owns the cadence.
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

    std::printf("NOLOCK30_SOFTWARE_WAIT_G_MS=%.3f\n",waitG);
    std::printf("NOLOCK30_SOFTWARE_WAIT_R_MS=%.3f\n",waitR);
    std::printf("NOLOCK30_WAIT_YIELDS=%lu\n",g_ptarFgPacer.waitYields);
    std::printf("NOLOCK30_RESYNCS=%lu\n",g_ptarFgPacer.resyncs);
    std::printf("NOLOCK30_LATE_SKIPS=%lu\n",PtFgPacerLateSkipCount());

    // This test runs without a D3D9 device; Prepare* must therefore be a
    // bookkeeping-only path. A generous 5-ms ceiling catches accidental
    // reintroduction of the former ~16.7-ms QPC wait without making CI timing
    // fragile.
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

    // Model a driver-blocking Sync1 Present.  Each fake Present consumes one
    // ~60-Hz interval.  The PTAR Prepare calls must add essentially zero time,
    // so a GENERATED+REAL pair remains ~32-40 ms instead of the ~64-70 ms
    // produced by FIELDHOTFIX3's software-wait + driver-wait stacking.
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
    const double sync1Pair=Ms(pair1.QuadPart-pair0.QuadPart,freq);
    std::printf("NOLOCK30_SYNC1_DRIVER_MODEL_PAIR_MS=%.3f\n",sync1Pair);
    if(sync1Pair<25.0 || sync1Pair>50.0)
    {
        std::printf("FAIL synchronized-Present pair indicates extra pacing delay\n");
        return 17;
    }

    // Simulate a long source workload. NOLOCK30_1 must still return
    // immediately: it never compensates source work with an additional wait.
    Sleep(60);
    LARGE_INTEGER s0={0},s1={0};
    QueryPerformanceCounter(&s0);
    if(!PtFgPacerPrepareGenerated())
        return 18;
    QueryPerformanceCounter(&s1);
    const double stallWait=Ms(s1.QuadPart-s0.QuadPart,freq);
    std::printf("NOLOCK30_AFTER_STALL_WAIT_MS=%.3f\n",stallWait);
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

    std::printf("D3D11_NOLOCK30_SYNC1_POLICY_PORT=PASS\n");
    std::printf("D3D9_NO_EXTRA_SOFTWARE_PACING=PASS\n");
    std::printf("D3D9_SYNC1_DOUBLE_WAIT_REGRESSION=PASS\n");
    std::printf("D3D9_FG_PACER_SMOKE=PASS\n");
    return 0;
}
