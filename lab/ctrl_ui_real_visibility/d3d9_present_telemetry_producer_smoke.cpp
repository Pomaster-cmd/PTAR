#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "ptar_present_telemetry.h"

int main(int argc,char** argv)
{
    int count=600;
    int sleepMs=3;
    if(argc>1) count=atoi(argv[1]);
    if(argc>2) sleepMs=atoi(argv[2]);
    for(int i=0;i<count;++i)
    {
        PtPresentTelemetryRecord((i&1)!=0);
        Sleep((DWORD)sleepMs);
    }
    std::printf("PRODUCER_WRITTEN=%d\n",count);
    std::fflush(stdout);
    Sleep(2500);
    return 0;
}
