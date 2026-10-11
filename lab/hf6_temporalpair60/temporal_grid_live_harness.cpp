#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <cmath>
#include "../../compat/x86_d3d9/ptar_temporal_grid.h"

static double TicksToMs(LONGLONG ticks, LONGLONG freq)
{
    return (double)ticks * 1000.0 / (double)freq;
}

static bool RunCadenceCase(const char* name, DWORD simulatedPresentMs, int frames)
{
    PTARTemporalGrid grid={};
    PtTemporalGridReset(&grid);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    std::vector<LONGLONG> starts;
    starts.reserve(frames);
    LONGLONG now=PtTemporalGridNow();
    PtTemporalGridArmWithOneSlotPrebuffer(&grid,now);

    for(int i=0;i<frames;++i)
    {
        PtTemporalGridWaitUntilDeadline(&grid,0);
        const LONGLONG callStart=PtTemporalGridNow();
        starts.push_back(callStart);

        // Model time spent inside a synchronized Present. Runtime grid
        // progression deliberately uses callStart, not the return timestamp.
        if(simulatedPresentMs)
            Sleep(simulatedPresentMs);

        PtTemporalGridRecordSubmission(&grid,callStart);
    }

    if(starts.size()<20)
        return false;

    std::vector<double> intervals;
    for(size_t i=1;i<starts.size();++i)
        intervals.push_back(TicksToMs(starts[i]-starts[i-1],grid.frequency.QuadPart));

    // Ignore initial scheduling perturbation for distribution stats, but the
    // no-microburst invariant applies to every interval.
    const double minv=*std::min_element(intervals.begin(),intervals.end());
    std::vector<double> body(intervals.begin()+8,intervals.end());
    std::sort(body.begin(),body.end());
    const double median=body[body.size()/2];
    const double p95=body[(body.size()*95)/100];
    const double mean=[](const std::vector<double>& v){double s=0;for(double x:v)s+=x;return s/v.size();}(body);

    // Grid must never issue back-to-back catch-up submissions. With a short
    // Present (< one slot), cadence should remain centered on 16.667ms.
    bool pass=minv>=16.40;
    if(simulatedPresentMs<=12)
    {
        pass = pass && median>=16.45 && median<=17.40;
        pass = pass && mean>=16.45 && mean<=18.20;
        pass = pass && p95<=22.0;
    }
    else
    {
        // If Present itself is slower than a slot, throughput may fall but it
        // must still never create a sub-slot recovery burst.
        pass = pass && median>=19.0;
    }

    std::printf("%s block=%lums min=%.3f mean=%.3f median=%.3f p95=%.3f skipped=%lu %s\n",
        name,(unsigned long)simulatedPresentMs,minv,mean,median,p95,
        grid.skippedGridSlots,pass?"PASS":"FAIL");
    return pass;
}

static bool RunStarvationRecovery()
{
    PTARTemporalGrid grid={};
    PtTemporalGridReset(&grid);
    std::vector<LONGLONG> starts;

    LONGLONG now=PtTemporalGridNow();
    PtTemporalGridArmWithOneSlotPrebuffer(&grid,now);
    for(int i=0;i<20;++i)
    {
        PtTemporalGridWaitUntilDeadline(&grid,0);
        LONGLONG s=PtTemporalGridNow();
        starts.push_back(s);
        PtTemporalGridRecordSubmission(&grid,s);
    }

    Sleep(80);
    PtTemporalGridResumeAfterStarvation(&grid,PtTemporalGridNow());
    for(int i=0;i<20;++i)
    {
        PtTemporalGridWaitUntilDeadline(&grid,0);
        LONGLONG s=PtTemporalGridNow();
        starts.push_back(s);
        PtTemporalGridRecordSubmission(&grid,s);
    }

    double minv=1e9;
    for(size_t i=1;i<starts.size();++i)
    {
        double dt=TicksToMs(starts[i]-starts[i-1],grid.frequency.QuadPart);
        if(dt<minv) minv=dt;
    }
    bool pass=minv>=16.40;
    std::printf("STARVATION_RECOVERY min=%.3fms %s\n",minv,pass?"PASS":"FAIL");
    return pass;
}

int main()
{
    bool ok=true;
    ok = RunCadenceCase("GRID_PRESENT_0MS",0,100) && ok;
    ok = RunCadenceCase("GRID_PRESENT_5MS",5,100) && ok;
    ok = RunCadenceCase("GRID_PRESENT_12MS",12,100) && ok;
    ok = RunCadenceCase("GRID_PRESENT_20MS",20,80) && ok;
    ok = RunStarvationRecovery() && ok;

    std::printf("TEMPORAL_GRID_LIVE_NO_MICROBURST=%s\n",ok?"PASS":"FAIL");
    std::printf("TEMPORAL_GRID_LIVE_60HZ_SHORT_PRESENT=%s\n",ok?"PASS":"FAIL");
    return ok?0:1;
}
