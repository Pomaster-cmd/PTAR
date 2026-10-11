#pragma once

#include <windows.h>

// Pure deadline scheduler used by the D3D9 temporal presenter.  It deliberately
// has no D3D9 calls and no frame-generation logic so the exact same code can be
// exercised by the lab harness.
//
// Contract:
//   * target cadence is expressed as an integer refresh rate (60 Hz here);
//   * deadlines are absolute QPC deadlines with rational remainder carry;
//   * there is never a catch-up burst after a late tick;
//   * if a tick is late, the next deadline is at least one full period after
//     the observed completion time.
//
// The no-catch-up rule is essential for PTAR: missing a refresh is preferable
// to issuing G/R back-to-back and recreating the field 31 ms / 0.5 ms cadence.

struct PTARTemporalPairScheduler
{
    LONGLONG qpcFrequency;
    LONGLONG periodBase;
    LONGLONG periodRemainder;
    LONGLONG remainderAccumulator;
    LONGLONG nextDeadlineQpc;
    unsigned int refreshHz;
    unsigned long ticks;
    unsigned long lateResyncs;
    bool started;
};

static void PtTemporalSchedulerInit(
    PTARTemporalPairScheduler* s,
    LONGLONG qpcFrequency,
    unsigned int refreshHz)
{
    if(!s)
        return;

    if(qpcFrequency<=0)
        qpcFrequency=1;
    if(refreshHz==0)
        refreshHz=60;

    ZeroMemory(s,sizeof(*s));
    s->qpcFrequency=qpcFrequency;
    s->refreshHz=refreshHz;
    s->periodBase=qpcFrequency/(LONGLONG)refreshHz;
    s->periodRemainder=qpcFrequency%(LONGLONG)refreshHz;
    if(s->periodBase<=0)
        s->periodBase=1;
}

static LONGLONG PtTemporalSchedulerOnePeriod(
    const PTARTemporalPairScheduler* s)
{
    if(!s || s->qpcFrequency<=0 || s->refreshHz==0)
        return 1;
    return s->qpcFrequency/(LONGLONG)s->refreshHz;
}

static void PtTemporalSchedulerStart(
    PTARTemporalPairScheduler* s,
    LONGLONG nowQpc)
{
    if(!s)
        return;

    s->nextDeadlineQpc=nowQpc;
    s->remainderAccumulator=0;
    s->ticks=0;
    s->lateResyncs=0;
    s->started=true;
}

static void PtTemporalSchedulerStop(PTARTemporalPairScheduler* s)
{
    if(!s)
        return;
    s->nextDeadlineQpc=0;
    s->remainderAccumulator=0;
    s->started=false;
}

static void PtTemporalSchedulerAddOnePeriod(PTARTemporalPairScheduler* s)
{
    if(!s)
        return;

    s->nextDeadlineQpc+=s->periodBase;
    s->remainderAccumulator+=s->periodRemainder;
    if(s->remainderAccumulator>=(LONGLONG)s->refreshHz)
    {
        const LONGLONG carry=
            s->remainderAccumulator/(LONGLONG)s->refreshHz;
        s->nextDeadlineQpc+=carry;
        s->remainderAccumulator-=
            carry*(LONGLONG)s->refreshHz;
    }
}

static void PtTemporalSchedulerAdvanceAfterTick(
    PTARTemporalPairScheduler* s,
    LONGLONG observedAfterQpc)
{
    if(!s || !s->started)
        return;

    ++s->ticks;
    PtTemporalSchedulerAddOnePeriod(s);

    // Never catch up by emitting another frame immediately.  If the scheduled
    // next tick has already passed, restart one full refresh after the observed
    // completion.  This is the hard anti-microburst invariant.
    if(s->nextDeadlineQpc<=observedAfterQpc)
    {
        ++s->lateResyncs;
        s->nextDeadlineQpc=observedAfterQpc;
        s->remainderAccumulator=0;
        PtTemporalSchedulerAddOnePeriod(s);
    }
}
