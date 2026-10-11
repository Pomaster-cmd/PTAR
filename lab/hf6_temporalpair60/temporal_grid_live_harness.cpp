#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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

static void BusyBlockMs(DWORD ms)
{
    if(!ms) return;
    LARGE_INTEGER f={};
    LARGE_INTEGER start={};
    LARGE_INTEGER now={};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&start);
    const LONGLONG target=start.QuadPart+(f.QuadPart*(LONGLONG)ms)/1000;
    do { QueryPerformanceCounter(&now); } while(now.QuadPart<target);
}

static bool RunControlledCadenceCase(const char* name, DWORD simulatedPresentMs, int frames)
{
    PTARTemporalGrid grid={};
    PtTemporalGridReset(&grid);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    std::vector<LONGLONG> starts;
    starts.reserve(frames);
    LONGLONG now=PtTemporalGridNow();
    PtTemporalGridArmWithOneSlotPrebuffer(&grid,now);

    for(int i=0;i<frames;++i)
    {
        PtTemporalGridWaitUntilDeadline(&grid,0);
        const LONGLONG callStart=PtTemporalGridNow();
        starts.push_back(callStart);
        BusyBlockMs(simulatedPresentMs);
        PtTemporalGridRecordSubmission(&grid,callStart);
    }

    if(starts.size()<20)
        return false;

    std::vector<double> intervals;
    for(size_t i=1;i<starts.size();++i)
        intervals.push_back(TicksToMs(starts[i]-starts[i-1],grid.frequency.QuadPart));

    const double minv=*std::min_element(intervals.begin(),intervals.end());
    std::vector<double> body(intervals.begin()+8,intervals.end());
    std::sort(body.begin(),body.end());
    const double median=body[body.size()/2];
    const double p95=body[(body.size()*95)/100];
    const double mean=[](const std::vector<double>& v){double s=0;for(double x:v)s+=x;return s/v.size();}(body);

    bool pass=minv>=16.40;
    if(simulatedPresentMs<=12)
    {
        pass = pass && median>=16.45 && median<=17.40;
        pass = pass && mean>=16.45 && mean<=18.20;
        pass = pass && p95<=22.0;
    }
    else
    {
        pass = pass && median>=19.0;
    }

    std::printf("%s controlled_block=%lums min=%.3f mean=%.3f median=%.3f p95=%.3f rebases=%lu %s\n",
        name,(unsigned long)simulatedPresentMs,minv,mean,median,p95,
        grid.skippedGridSlots,pass?"PASS":"FAIL");
    return pass;
}

static bool RunSchedulerNoiseCase()
{
    PTARTemporalGrid grid={};
    PtTemporalGridReset(&grid);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    std::vector<LONGLONG> starts;
    const int frames=100;

    PtTemporalGridArmWithOneSlotPrebuffer(&grid,PtTemporalGridNow());
    for(int i=0;i<frames;++i)
    {
        PtTemporalGridWaitUntilDeadline(&grid,0);
        const LONGLONG s=PtTemporalGridNow();
        starts.push_back(s);
        Sleep(12);
        PtTemporalGridRecordSubmission(&grid,s);
    }

    double minv=1e9;
    double maxv=0.0;
    for(size_t i=1;i<starts.size();++i)
    {
        const double dt=TicksToMs(starts[i]-starts[i-1],grid.frequency.QuadPart);
        minv=std::min(minv,dt);
        maxv=std::max(maxv,dt);
    }
    const bool pass=minv>=16.40;
    std::printf("GRID_SCHEDULER_NOISE_SLEEP12 min=%.3f max=%.3f rebases=%lu %s\n",
        minv,maxv,grid.skippedGridSlots,pass?"PASS":"FAIL");
    return pass;
}

static bool RunStarvationRecovery()
{
    PTARTemporalGrid grid={};
    PtTemporalGridReset(&grid);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
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
        minv=std::min(minv,dt);
    }
    const bool pass=minv>=16.40;
    std::printf("STARVATION_RECOVERY min=%.3fms %s\n",minv,pass?"PASS":"FAIL");
    return pass;
}

int main()
{
    bool controlled=true;
    controlled = RunControlledCadenceCase("GRID_PRESENT_0MS",0,100) && controlled;
    controlled = RunControlledCadenceCase("GRID_PRESENT_5MS",5,100) && controlled;
    controlled = RunControlledCadenceCase("GRID_PRESENT_12MS",12,100) && controlled;
    controlled = RunControlledCadenceCase("GRID_PRESENT_20MS",20,80) && controlled;

    const bool noise=RunSchedulerNoiseCase();
    const bool starvation=RunStarvationRecovery();
    const bool noMicroburst=controlled && noise && starvation;

    std::printf("TEMPORAL_GRID_LIVE_CONTROLLED_60HZ=%s\n",controlled?"PASS":"FAIL");
    std::printf("TEMPORAL_GRID_LIVE_SCHEDULER_NO_CATCHUP=%s\n",noise?"PASS":"FAIL");
    std::printf("TEMPORAL_GRID_LIVE_STARVATION_NO_CATCHUP=%s\n",starvation?"PASS":"FAIL");
    std::printf("TEMPORAL_GRID_LIVE_NO_MICROBURST=%s\n",noMicroburst?"PASS":"FAIL");
    std::printf("TEMPORAL_GRID_LIVE_60HZ_SHORT_PRESENT=%s\n",controlled?"PASS":"FAIL");
    return noMicroburst?0:1;
}
