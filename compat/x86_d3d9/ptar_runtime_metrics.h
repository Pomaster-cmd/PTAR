#pragma once

#include <windows.h>

struct PTARRollingRate
{
    LARGE_INTEGER frequency;
    LONGLONG stamps[256];
    unsigned int writeIndex;
    unsigned int count;
    bool initialized;
};

static void PtRollingRateInit(PTARRollingRate* rate)
{
    if(!rate || rate->initialized)
        return;

    QueryPerformanceFrequency(&rate->frequency);
    if(rate->frequency.QuadPart<=0)
        rate->frequency.QuadPart=1;

    rate->writeIndex=0;
    rate->count=0;
    rate->initialized=true;
}

static void PtRollingRateReset(PTARRollingRate* rate)
{
    if(!rate)
        return;

    PtRollingRateInit(rate);
    rate->writeIndex=0;
    rate->count=0;
    for(unsigned int i=0;i<256;++i)
        rate->stamps[i]=0;
}

static void PtRollingRateRecordAt(PTARRollingRate* rate,LONGLONG qpc)
{
    if(!rate)
        return;

    PtRollingRateInit(rate);

    rate->stamps[rate->writeIndex]=qpc;
    rate->writeIndex=(rate->writeIndex+1u)&255u;
    if(rate->count<256u)
        ++rate->count;
}

static LONGLONG PtRollingRateNow()
{
    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

static void PtRollingRateRecord(PTARRollingRate* rate)
{
    PtRollingRateRecordAt(rate,PtRollingRateNow());
}

static double PtRollingRateValueAt(
    const PTARRollingRate* rate,
    LONGLONG now)
{
    if(!rate || !rate->initialized || rate->count<2u ||
       rate->frequency.QuadPart<=0)
        return 0.0;

    const LONGLONG oneSecond=rate->frequency.QuadPart;
    const LONGLONG cutoff=now-oneSecond;

    unsigned int kept=0;
    LONGLONG oldest=0;
    LONGLONG newest=0;

    for(unsigned int i=0;i<rate->count;++i)
    {
        const unsigned int index=
            (rate->writeIndex+256u-rate->count+i)&255u;
        const LONGLONG stamp=rate->stamps[index];
        if(stamp<=0 || stamp<cutoff || stamp>now)
            continue;

        if(kept==0u)
            oldest=stamp;
        newest=stamp;
        ++kept;
    }

    if(kept<2u || newest<=oldest)
        return 0.0;

    // Wall-clock rolling throughput: number of delivered frame intervals over
    // their actual elapsed QPC span. A long hitch therefore lowers the number
    // instead of being hidden by an instantaneous-FPS EMA.
    const LONGLONG elapsed=now-oldest;
    if(elapsed<=0)
        return 0.0;

    return
        (double)(kept-1u)*
        (double)rate->frequency.QuadPart/
        (double)elapsed;
}

static double PtRollingRateValue(const PTARRollingRate* rate)
{
    return PtRollingRateValueAt(rate,PtRollingRateNow());
}
