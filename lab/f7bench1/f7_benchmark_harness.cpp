#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>

static void PtDiagLogA(const char*,...){ }
#include "../../compat/x86_d3d9/ptar_f7_benchmark.h"

static int g_failures=0;

static void Gate(bool ok,const char* name)
{
    std::printf("%s=%s\n",name,ok?"PASS":"FAIL");
    if(!ok) ++g_failures;
}

static bool Near(double value,double target,double tolerance)
{
    double d=value-target;
    if(d<0.0) d=-d;
    return d<=tolerance;
}

static LONGLONG Ticks(double seconds)
{
    return (LONGLONG)(seconds*(double)g_ptarF7Benchmark.frequency.QuadPart+0.5);
}

static bool RunFixedRate(double fps,bool fg,PTF7BenchmarkResult* result)
{
    const LONGLONG base=Ticks(100.0);
    if(!PtF7BenchmarkStartAt(base,fg))
        return false;

    const LONGLONG settleEnd=base+Ticks(3.0);
    const LONGLONG step=Ticks(1.0/fps);
    LONGLONG qpc=settleEnd;
    bool generated=fg;

    PtF7BenchmarkRecordVisible(qpc,generated);
    for(unsigned int i=0;i<(unsigned int)(fps*10.0+8.0);++i)
    {
        qpc+=step;
        if(fg) generated=!generated; else generated=false;
        PtF7BenchmarkRecordVisible(qpc,generated);
        if(InterlockedCompareExchange(&g_ptarF7Benchmark.phase,0,0)==PTAR_F7BENCH_READY)
            break;
    }
    return PtF7BenchmarkConsumeResult(result);
}

int main()
{
    PtF7BenchmarkConfigure(0,3,10);

    PTF7BenchmarkResult r60={};
    Gate(RunFixedRate(60.0,true,&r60),"F7BENCH_60HZ_RESULT");
    Gate(Near(r60.averageVisibleFps,60.0,0.20),"F7BENCH_60HZ_AVG");
    Gate(Near(r60.p99FrameMs,16.666,0.20),"F7BENCH_60HZ_P99");
    Gate(Near(r60.onePercentLowFps,60.0,0.30),"F7BENCH_60HZ_1P_LOW");
    Gate(r60.realFrames>0 && r60.generatedFrames>0,"F7BENCH_60HZ_GR_COUNTS");
    Gate(r60.longGapRestarts==0,"F7BENCH_60HZ_NO_RESTART");

    PTF7BenchmarkResult r55={};
    Gate(RunFixedRate(55.0,true,&r55),"F7BENCH_55HZ_RESULT");
    Gate(Near(r55.averageVisibleFps,55.0,0.20),"F7BENCH_55HZ_AVG");
    Gate(Near(r55.onePercentLowFps,55.0,0.30),"F7BENCH_55HZ_1P_LOW");

    // Re-entry must be refused while the original measurement remains active.
    const LONGLONG reentryBase=Ticks(300.0);
    Gate(PtF7BenchmarkStartAt(reentryBase,true),"F7BENCH_REENTRY_FIRST_START");
    Gate(!PtF7BenchmarkStartAt(reentryBase+Ticks(0.1),true),"F7BENCH_REENTRY_SECOND_IGNORED");

    // Complete the active measurement so the next test starts from IDLE.
    LONGLONG qpc=reentryBase+Ticks(3.0);
    PtF7BenchmarkRecordVisible(qpc,true);
    for(unsigned int i=0;i<610;++i)
    {
        qpc+=Ticks(1.0/60.0);
        PtF7BenchmarkRecordVisible(qpc,(i&1u)!=0u);
        if(InterlockedCompareExchange(&g_ptarF7Benchmark.phase,0,0)==PTAR_F7BENCH_READY)
            break;
    }
    PTF7BenchmarkResult throwAway={};
    Gate(PtF7BenchmarkConsumeResult(&throwAway),"F7BENCH_REENTRY_CLEANUP");

    // A >250 ms interruption must restart settling and exclude the contaminated window.
    const LONGLONG gapBase=Ticks(500.0);
    Gate(PtF7BenchmarkStartAt(gapBase,true),"F7BENCH_GAP_START");
    qpc=gapBase+Ticks(3.0);
    PtF7BenchmarkRecordVisible(qpc,true);
    for(unsigned int i=0;i<120;++i)
    {
        qpc+=Ticks(1.0/60.0);
        PtF7BenchmarkRecordVisible(qpc,(i&1u)!=0u);
    }
    qpc+=Ticks(0.300);
    PtF7BenchmarkRecordVisible(qpc,false);
    Gate(
        InterlockedCompareExchange(&g_ptarF7Benchmark.phase,0,0)==PTAR_F7BENCH_SETTLING,
        "F7BENCH_LONG_GAP_RESTARTS_SETTLING");

    qpc+=Ticks(3.0);
    PtF7BenchmarkRecordVisible(qpc,true);
    for(unsigned int i=0;i<610;++i)
    {
        qpc+=Ticks(1.0/60.0);
        PtF7BenchmarkRecordVisible(qpc,(i&1u)!=0u);
        if(InterlockedCompareExchange(&g_ptarF7Benchmark.phase,0,0)==PTAR_F7BENCH_READY)
            break;
    }
    PTF7BenchmarkResult gapResult={};
    Gate(PtF7BenchmarkConsumeResult(&gapResult),"F7BENCH_LONG_GAP_RESULT");
    Gate(gapResult.longGapRestarts==1,"F7BENCH_LONG_GAP_RESTART_COUNT");
    Gate(Near(gapResult.averageVisibleFps,60.0,0.20),"F7BENCH_LONG_GAP_CLEAN_AVG");

    // FG OFF remains a valid passive benchmark: all visible Presents are REAL.
    PTF7BenchmarkResult off={};
    Gate(RunFixedRate(60.0,false,&off),"F7BENCH_FG_OFF_RESULT");
    Gate(off.generatedFrames==0,"F7BENCH_FG_OFF_ZERO_GENERATED");
    Gate(off.realFrames==off.visibleFrames,"F7BENCH_FG_OFF_ALL_REAL");
    Gate(Near(off.averageVisibleFps,60.0,0.20),"F7BENCH_FG_OFF_AVG");

    Gate(g_ptarF7Benchmark.resultPath[0]==0,"F7BENCH_HARNESS_NO_FILE_IO");

    std::printf("F7BENCH_ALL_GATES=%s\n",g_failures?"FAIL":"PASS");
    return g_failures?1:0;
}
