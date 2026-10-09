#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "ptar_present_telemetry.h"

static void BusyDelayMs(int ms)
{
    if(ms<=0) return;
    LARGE_INTEGER f={},s={},n={};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&s);
    const LONGLONG ticks=(f.QuadPart*(LONGLONG)ms)/1000;
    do { QueryPerformanceCounter(&n); } while((n.QuadPart-s.QuadPart)<ticks);
}

int main(int argc,char** argv)
{
    int count=600;
    int delayMs=3;
    if(argc>1) count=atoi(argv[1]);
    if(argc>2) delayMs=atoi(argv[2]);
    for(int i=0;i<count;++i)
    {
        PtPresentTelemetryRecord((i&1)!=0);
        BusyDelayMs(delayMs);
    }
    std::printf("PRODUCER_WRITTEN=%d\n",count);
    std::fflush(stdout);
    Sleep(2500);
    return 0;
}
