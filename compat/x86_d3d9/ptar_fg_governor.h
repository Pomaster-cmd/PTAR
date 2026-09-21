#pragma once

#include <windows.h>
#include "ptar_diag.h"

// GW16I source-side REAL governor.
//
// Production contract mirrored from the supplied D3D11 model:
//   FG OFF / startup : target120 policy -> up to 60 REAL frames/s.
//   FG ON HIGH       : target60 policy  -> up to 30 REAL frames/s.
//
// Crucially, this governor controls only source REAL production. Physical
// display Present and GENERATED work live on the isolated presenter device.
// Therefore a wait here is an intentional source cap, not a VBlank wait leaking
// back from the presenter.

struct PTARRealGovernor
{
    LARGE_INTEGER frequency;
    LONGLONG nextQpc;
    unsigned int targetRealHz;
    unsigned long waits;
    unsigned long resyncs;
    bool initialized;
};

static PTARRealGovernor g_ptarRealGovernor={};

static void PtRealGovernorInit()
{
    if(g_ptarRealGovernor.initialized)
        return;

    QueryPerformanceFrequency(&g_ptarRealGovernor.frequency);
    if(g_ptarRealGovernor.frequency.QuadPart<=0)
        g_ptarRealGovernor.frequency.QuadPart=1;

    g_ptarRealGovernor.initialized=true;
}

static LONGLONG PtRealGovernorNow()
{
    LARGE_INTEGER q={};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static unsigned int PtRealGovernorTarget(bool fgEnabled)
{
    return fgEnabled?30u:60u;
}

static void PtRealGovernorReset(bool fgEnabled)
{
    PtRealGovernorInit();
    g_ptarRealGovernor.targetRealHz=
        PtRealGovernorTarget(fgEnabled);
    g_ptarRealGovernor.nextQpc=0;
    g_ptarRealGovernor.waits=0;
    g_ptarRealGovernor.resyncs=0;

    PtDiagLogA(
        "REAL_GOVERNOR_RESET fg=%s target_real_hz=%u policy=%s",
        fgEnabled?"ON":"OFF",
        g_ptarRealGovernor.targetRealHz,
        fgEnabled?"GW16I_TARGET60_REAL30":"GW16I_TARGET120_REAL60");
}

static void PtRealGovernorSyncMode(bool fgEnabled)
{
    PtRealGovernorInit();

    const unsigned int wanted=
        PtRealGovernorTarget(fgEnabled);

    if(g_ptarRealGovernor.targetRealHz!=wanted)
    {
        g_ptarRealGovernor.targetRealHz=wanted;
        g_ptarRealGovernor.nextQpc=0;

        PtDiagLogA(
            "REAL_GOVERNOR_MODE fg=%s target_real_hz=%u",
            fgEnabled?"ON":"OFF",
            wanted);
    }
}

static void PtRealGovernorWaitAfterSourceFrame(bool fgEnabled)
{
    PtRealGovernorSyncMode(fgEnabled);

    const LONGLONG freq=
        g_ptarRealGovernor.frequency.QuadPart;
    const unsigned int hz=
        g_ptarRealGovernor.targetRealHz?
            g_ptarRealGovernor.targetRealHz:60u;

    const LONGLONG period=
        (LONGLONG)((double)freq/(double)hz+0.5);

    LONGLONG now=PtRealGovernorNow();

    if(g_ptarRealGovernor.nextQpc<=0)
    {
        g_ptarRealGovernor.nextQpc=now+period;
        return;
    }

    // If the game/source is already slower than the target, never add latency.
    // Re-anchor after a miss larger than one source period instead of trying to
    // repay historical deadlines.
    if(now>g_ptarRealGovernor.nextQpc+period)
    {
        ++g_ptarRealGovernor.resyncs;
        g_ptarRealGovernor.nextQpc=now+period;
        return;
    }

    while(now<g_ptarRealGovernor.nextQpc)
    {
        const LONGLONG remaining=
            g_ptarRealGovernor.nextQpc-now;

        const double ms=
            (double)remaining*1000.0/(double)freq;

        if(ms>2.0)
        {
            DWORD sleepMs=(DWORD)(ms-1.0);
            if(sleepMs>8u)
                sleepMs=8u;
            Sleep(sleepMs);
            ++g_ptarRealGovernor.waits;
        }
        else
        {
            SwitchToThread();
        }

        now=PtRealGovernorNow();
    }

    g_ptarRealGovernor.nextQpc+=period;
}

static unsigned int PtRealGovernorCurrentTarget()
{
    PtRealGovernorInit();
    return g_ptarRealGovernor.targetRealHz?
        g_ptarRealGovernor.targetRealHz:60u;
}
