#pragma once

#include <windows.h>
#include <cstdio>

// Passive F7 benchmark for the D3D9 backend.
// Contract inherited from the validated D3D11 F7 meter:
//   - 3 s settling + 10 s clean recording by default;
//   - measure only successful visible Presents;
//   - never change resolution, presentation mode, FG or pacing;
//   - reject a long interruption by restarting the settling window;
//   - repeated F7 while active is ignored;
//   - persist the last result beside d3d9.dll.
//
// This module intentionally owns no D3D objects and performs no waits.

#define PTAR_F7BENCH_MAX_INTERVALS 2048u
#define PTAR_F7BENCH_LONG_GAP_MS 250u

#define PTAR_F7BENCH_IDLE 0L
#define PTAR_F7BENCH_SETTLING 1L
#define PTAR_F7BENCH_RECORDING 2L
#define PTAR_F7BENCH_READY 3L

struct PTF7BenchmarkResult
{
    unsigned long visibleFrames;
    unsigned long realFrames;
    unsigned long generatedFrames;
    unsigned long longGapRestarts;
    double averageVisibleFps;
    double realFps;
    double generatedFps;
    double p99FrameMs;
    double onePercentLowFps;
    unsigned int warmupSeconds;
    unsigned int durationSeconds;
    bool fgEnabledAtStart;
};

struct PTF7BenchmarkState
{
    SRWLOCK lock;
    LARGE_INTEGER frequency;
    volatile LONG phase;
    LONGLONG settleUntilQpc;
    LONGLONG recordStartQpc;
    LONGLONG recordEndQpc;
    LONGLONG lastQpc;
    unsigned int intervalsUs[PTAR_F7BENCH_MAX_INTERVALS];
    unsigned int intervalCount;
    unsigned long visibleFrames;
    unsigned long realFrames;
    unsigned long generatedFrames;
    unsigned long longGapRestarts;
    unsigned int warmupSeconds;
    unsigned int durationSeconds;
    bool fgEnabledAtStart;
    bool initialized;
    wchar_t resultPath[MAX_PATH];
};

static PTF7BenchmarkState g_ptarF7Benchmark={};

inline void PtF7BenchmarkInit()
{
    if(g_ptarF7Benchmark.initialized)
        return;

    QueryPerformanceFrequency(&g_ptarF7Benchmark.frequency);
    if(g_ptarF7Benchmark.frequency.QuadPart<=0)
        g_ptarF7Benchmark.frequency.QuadPart=1;

    g_ptarF7Benchmark.warmupSeconds=3u;
    g_ptarF7Benchmark.durationSeconds=10u;
    g_ptarF7Benchmark.phase=PTAR_F7BENCH_IDLE;
    g_ptarF7Benchmark.initialized=true;
}

inline LONGLONG PtF7BenchmarkNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

inline unsigned int PtF7BenchmarkClampWarmup(int value)
{
    if(value<0 || value>30)
        return 3u;
    return (unsigned int)value;
}

inline unsigned int PtF7BenchmarkClampDuration(int value)
{
    if(value<1 || value>120)
        return 10u;
    return (unsigned int)value;
}

inline void PtF7BenchmarkConfigure(
    HMODULE selfModule,
    int warmupSeconds,
    int durationSeconds)
{
    PtF7BenchmarkInit();
    AcquireSRWLockExclusive(&g_ptarF7Benchmark.lock);

    g_ptarF7Benchmark.warmupSeconds=
        PtF7BenchmarkClampWarmup(warmupSeconds);
    g_ptarF7Benchmark.durationSeconds=
        PtF7BenchmarkClampDuration(durationSeconds);

    g_ptarF7Benchmark.resultPath[0]=0;
    if(selfModule)
    {
        wchar_t modulePath[MAX_PATH]={0};
        const DWORD n=GetModuleFileNameW(
            selfModule,modulePath,(DWORD)_countof(modulePath));
        if(n>0 && n<MAX_PATH)
        {
            wchar_t* slash=wcsrchr(modulePath,L'\\');
            if(slash)
            {
                *(slash+1)=0;
                const wchar_t* fileName=L"PTAR_F7_BENCHMARK_LAST.txt";
                if(wcslen(modulePath)+wcslen(fileName)+1<_countof(modulePath))
                {
                    wcscat_s(modulePath,fileName);
                    wcsncpy_s(
                        g_ptarF7Benchmark.resultPath,
                        modulePath,
                        _TRUNCATE);
                }
            }
        }
    }

    ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
}

inline void PtF7BenchmarkResetRecordingLocked()
{
    g_ptarF7Benchmark.recordStartQpc=0;
    g_ptarF7Benchmark.recordEndQpc=0;
    g_ptarF7Benchmark.lastQpc=0;
    g_ptarF7Benchmark.intervalCount=0;
    g_ptarF7Benchmark.visibleFrames=0;
    g_ptarF7Benchmark.realFrames=0;
    g_ptarF7Benchmark.generatedFrames=0;
}

inline bool PtF7BenchmarkStartAt(
    LONGLONG now,
    bool fgEnabledAtStart)
{
    PtF7BenchmarkInit();
    AcquireSRWLockExclusive(&g_ptarF7Benchmark.lock);

    if(g_ptarF7Benchmark.phase!=PTAR_F7BENCH_IDLE)
    {
        ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
        return false;
    }

    PtF7BenchmarkResetRecordingLocked();
    g_ptarF7Benchmark.longGapRestarts=0;
    g_ptarF7Benchmark.fgEnabledAtStart=fgEnabledAtStart;
    g_ptarF7Benchmark.settleUntilQpc=
        now+
        g_ptarF7Benchmark.frequency.QuadPart*
        (LONGLONG)g_ptarF7Benchmark.warmupSeconds;
    InterlockedExchange(
        &g_ptarF7Benchmark.phase,
        PTAR_F7BENCH_SETTLING);

    const unsigned int warmup=g_ptarF7Benchmark.warmupSeconds;
    const unsigned int duration=g_ptarF7Benchmark.durationSeconds;
    ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);

    PtDiagLogA("========== F7 D3D9 PASSIVE BENCHMARK ARMED ==========");
    PtDiagLogA("BENCH SETTLE SECONDS %u",warmup);
    PtDiagLogA("BENCH RECORD SECONDS %u",duration);
    PtDiagLogA(
        "BENCH CONTRACT=VISIBLE_PRESENTS_ONLY FG=%s NO_MODE_CHANGE NO_PACING_CHANGE",
        fgEnabledAtStart?"ON":"OFF");
    return true;
}

inline bool PtF7BenchmarkStart(bool fgEnabledAtStart)
{
    return PtF7BenchmarkStartAt(
        PtF7BenchmarkNow(),
        fgEnabledAtStart);
}

inline bool PtF7BenchmarkActive()
{
    const LONG phase=InterlockedCompareExchange(
        &g_ptarF7Benchmark.phase,0,0);
    return phase==PTAR_F7BENCH_SETTLING ||
           phase==PTAR_F7BENCH_RECORDING;
}

inline void PtF7BenchmarkRecordVisible(
    LONGLONG qpc,
    bool generated)
{
    const LONG fastPhase=InterlockedCompareExchange(
        &g_ptarF7Benchmark.phase,0,0);
    if(fastPhase!=PTAR_F7BENCH_SETTLING &&
       fastPhase!=PTAR_F7BENCH_RECORDING)
        return;

    if(qpc<=0)
        qpc=PtF7BenchmarkNow();

    AcquireSRWLockExclusive(&g_ptarF7Benchmark.lock);
    LONG phase=g_ptarF7Benchmark.phase;

    if(phase==PTAR_F7BENCH_SETTLING)
    {
        if(qpc>=g_ptarF7Benchmark.settleUntilQpc)
        {
            PtF7BenchmarkResetRecordingLocked();
            g_ptarF7Benchmark.recordStartQpc=qpc;
            g_ptarF7Benchmark.recordEndQpc=
                qpc+
                g_ptarF7Benchmark.frequency.QuadPart*
                (LONGLONG)g_ptarF7Benchmark.durationSeconds;
            g_ptarF7Benchmark.lastQpc=qpc;
            g_ptarF7Benchmark.visibleFrames=1;
            if(generated)
                g_ptarF7Benchmark.generatedFrames=1;
            else
                g_ptarF7Benchmark.realFrames=1;
            InterlockedExchange(
                &g_ptarF7Benchmark.phase,
                PTAR_F7BENCH_RECORDING);
            ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
            PtDiagLogA("BENCH: clean D3D9 visible recording started");
            return;
        }

        ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
        return;
    }

    if(phase!=PTAR_F7BENCH_RECORDING)
    {
        ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
        return;
    }

    if(g_ptarF7Benchmark.lastQpc>0 &&
       qpc>g_ptarF7Benchmark.lastQpc)
    {
        const LONGLONG delta=qpc-g_ptarF7Benchmark.lastQpc;
        const LONGLONG longGap=
            (g_ptarF7Benchmark.frequency.QuadPart*
             (LONGLONG)PTAR_F7BENCH_LONG_GAP_MS)/1000;

        if(delta>longGap)
        {
            ++g_ptarF7Benchmark.longGapRestarts;
            PtF7BenchmarkResetRecordingLocked();
            g_ptarF7Benchmark.settleUntilQpc=
                qpc+
                g_ptarF7Benchmark.frequency.QuadPart*
                (LONGLONG)g_ptarF7Benchmark.warmupSeconds;
            InterlockedExchange(
                &g_ptarF7Benchmark.phase,
                PTAR_F7BENCH_SETTLING);
            ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
            PtDiagLogA(
                "BENCH: long frame gap detected; settling restarted restarts=%lu",
                g_ptarF7Benchmark.longGapRestarts);
            return;
        }

        if(g_ptarF7Benchmark.intervalCount<PTAR_F7BENCH_MAX_INTERVALS)
        {
            unsigned long long us=
                ((unsigned long long)delta*1000000ull)/
                (unsigned long long)g_ptarF7Benchmark.frequency.QuadPart;
            if(us>0xffffffffull) us=0xffffffffull;
            g_ptarF7Benchmark.intervalsUs[
                g_ptarF7Benchmark.intervalCount++]=(unsigned int)us;
        }
    }

    g_ptarF7Benchmark.lastQpc=qpc;
    ++g_ptarF7Benchmark.visibleFrames;
    if(generated)
        ++g_ptarF7Benchmark.generatedFrames;
    else
        ++g_ptarF7Benchmark.realFrames;

    if(qpc>=g_ptarF7Benchmark.recordEndQpc)
        InterlockedExchange(
            &g_ptarF7Benchmark.phase,
            PTAR_F7BENCH_READY);

    ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
}

inline void PtF7BenchmarkSortIntervals(
    unsigned int* values,
    unsigned int count)
{
    if(!values) return;
    for(unsigned int i=1;i<count;++i)
    {
        const unsigned int value=values[i];
        unsigned int j=i;
        while(j>0 && values[j-1]>value)
        {
            values[j]=values[j-1];
            --j;
        }
        values[j]=value;
    }
}

inline void PtF7BenchmarkWriteResult(
    const PTF7BenchmarkResult& result,
    const wchar_t* path)
{
    if(!path || !*path)
        return;

    char body[2048]={0};
    _snprintf_s(
        body,sizeof(body),_TRUNCATE,
        "PTAR F7 D3D9 PASSIVE BENCHMARK RESULT\r\n"
        "RESULT=PASS\r\n"
        "WARMUP_SECONDS=%u\r\n"
        "DURATION_SECONDS=%u\r\n"
        "FG_AT_START=%s\r\n"
        "VISIBLE_FRAMES=%lu\r\n"
        "REAL_PRESENTS=%lu\r\n"
        "GENERATED_PRESENTS=%lu\r\n"
        "AVG_VISIBLE_FPS=%.3f\r\n"
        "REAL_FPS=%.3f\r\n"
        "GENERATED_FPS=%.3f\r\n"
        "P99_FRAME_MS=%.3f\r\n"
        "ONE_PERCENT_LOW_FPS=%.3f\r\n"
        "LONG_GAP_RESTARTS=%lu\r\n"
        "CONTRACT=MEASUREMENT_ONLY_NO_MODE_OR_PACING_CHANGE\r\n",
        result.warmupSeconds,
        result.durationSeconds,
        result.fgEnabledAtStart?"ON":"OFF",
        result.visibleFrames,
        result.realFrames,
        result.generatedFrames,
        result.averageVisibleFps,
        result.realFps,
        result.generatedFps,
        result.p99FrameMs,
        result.onePercentLowFps,
        result.longGapRestarts);

    HANDLE file=CreateFileW(
        path,GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(file==INVALID_HANDLE_VALUE)
        return;

    DWORD written=0;
    WriteFile(file,body,(DWORD)strlen(body),&written,0);
    CloseHandle(file);
}

inline bool PtF7BenchmarkConsumeResult(
    PTF7BenchmarkResult* outResult)
{
    if(!outResult)
        return false;

    if(InterlockedCompareExchange(
        &g_ptarF7Benchmark.phase,0,0)!=PTAR_F7BENCH_READY)
        return false;

    AcquireSRWLockExclusive(&g_ptarF7Benchmark.lock);
    if(g_ptarF7Benchmark.phase!=PTAR_F7BENCH_READY)
    {
        ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);
        return false;
    }

    PTF7BenchmarkResult result={};
    result.visibleFrames=g_ptarF7Benchmark.visibleFrames;
    result.realFrames=g_ptarF7Benchmark.realFrames;
    result.generatedFrames=g_ptarF7Benchmark.generatedFrames;
    result.longGapRestarts=g_ptarF7Benchmark.longGapRestarts;
    result.warmupSeconds=g_ptarF7Benchmark.warmupSeconds;
    result.durationSeconds=g_ptarF7Benchmark.durationSeconds;
    result.fgEnabledAtStart=g_ptarF7Benchmark.fgEnabledAtStart;

    const LONGLONG elapsed=
        g_ptarF7Benchmark.lastQpc-
        g_ptarF7Benchmark.recordStartQpc;
    if(elapsed>0 && result.visibleFrames>1)
    {
        const double seconds=
            (double)elapsed/
            (double)g_ptarF7Benchmark.frequency.QuadPart;
        const double intervals=(double)(result.visibleFrames-1ul);
        result.averageVisibleFps=intervals/seconds;
        result.realFps=(double)result.realFrames/seconds;
        result.generatedFps=(double)result.generatedFrames/seconds;
    }

    const unsigned int count=g_ptarF7Benchmark.intervalCount;
    if(count>0)
    {
        PtF7BenchmarkSortIntervals(
            g_ptarF7Benchmark.intervalsUs,count);

        unsigned int p99Rank=(count*99u+99u)/100u;
        if(p99Rank<1u) p99Rank=1u;
        if(p99Rank>count) p99Rank=count;
        result.p99FrameMs=
            (double)g_ptarF7Benchmark.intervalsUs[p99Rank-1u]/1000.0;

        unsigned int worstCount=(count+99u)/100u;
        if(worstCount<1u) worstCount=1u;
        if(worstCount>count) worstCount=count;
        unsigned long long worstSum=0;
        for(unsigned int i=0;i<worstCount;++i)
            worstSum+=g_ptarF7Benchmark.intervalsUs[count-1u-i];
        const double worstAverageUs=
            (double)worstSum/(double)worstCount;
        if(worstAverageUs>0.0)
            result.onePercentLowFps=1000000.0/worstAverageUs;
    }

    wchar_t resultPath[MAX_PATH]={0};
    wcsncpy_s(
        resultPath,
        g_ptarF7Benchmark.resultPath,
        _TRUNCATE);

    InterlockedExchange(
        &g_ptarF7Benchmark.phase,
        PTAR_F7BENCH_IDLE);
    ReleaseSRWLockExclusive(&g_ptarF7Benchmark.lock);

    *outResult=result;
    PtF7BenchmarkWriteResult(result,resultPath);
    PtDiagLogA("========== F7 D3D9 PASSIVE BENCHMARK RESULT ==========");
    PtDiagLogA(
        "BENCH FRAMES=%lu REAL=%lu GENERATED=%lu AVG_FPS=%.3f P99_MS=%.3f ONE_PERCENT_LOW_FPS=%.3f RESTARTS=%lu",
        result.visibleFrames,
        result.realFrames,
        result.generatedFrames,
        result.averageVisibleFps,
        result.p99FrameMs,
        result.onePercentLowFps,
        result.longGapRestarts);
    PtDiagLogA("========== END F7 D3D9 PASSIVE BENCHMARK RESULT ==========");
    return true;
}
