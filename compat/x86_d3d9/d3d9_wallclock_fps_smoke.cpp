#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include "ptar_runtime_metrics.h"

int main()
{
    PTARRollingRate rate={};
    PtRollingRateInit(&rate);

    // Use a deterministic synthetic QPC frequency so the test is independent
    // of hosted-runner scheduling.
    rate.frequency.QuadPart=1000000;
    rate.initialized=true;

    for(int i=0;i<=50;++i)
        PtRollingRateRecordAt(&rate,(LONGLONG)i*20000);

    const double fps50=PtRollingRateValueAt(&rate,1000000);
    std::printf("FPS_50_STEADY=%.3f\n",fps50);
    if(fps50<49.5 || fps50>50.5)
        return 10;

    // Add a 200 ms stall. Wall-clock rate must fall materially instead of
    // staying near the pre-stall instantaneous cadence.
    PtRollingRateRecordAt(&rate,1200000);
    const double fpsAfterStall=PtRollingRateValueAt(&rate,1200000);
    std::printf("FPS_AFTER_200MS_STALL=%.3f\n",fpsAfterStall);
    if(fpsAfterStall>=48.0)
        return 11;

    PtRollingRateReset(&rate);
    for(int i=0;i<=60;++i)
        PtRollingRateRecordAt(
            &rate,
            (LONGLONG)((1000000.0/60.0)*(double)i));

    const double fps60=PtRollingRateValueAt(
        &rate,rate.stamps[(rate.writeIndex+255u)&255u]);
    std::printf("FPS_60_STEADY=%.3f\n",fps60);
    if(fps60<59.0 || fps60>61.0)
        return 12;

    std::printf("D3D9_WALLCLOCK_FPS_SMOKE=PASS\n");
    return 0;
}
