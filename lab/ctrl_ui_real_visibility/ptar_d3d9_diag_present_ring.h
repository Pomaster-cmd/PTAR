#pragma once

#include <windows.h>

#define PTAR_D3D9_DIAG_MAGIC 0x39444750ul
#define PTAR_D3D9_DIAG_VERSION 1ul
#define PTAR_D3D9_DIAG_CAPACITY 256ul

#pragma pack(push,4)
struct PTARD3D9DiagPresentEvent
{
    volatile LONG sequence;
    DWORD markerSerial;
    DWORD generated;
    LONGLONG qpc;
    DWORD realPresents;
    DWORD generatedPresents;
    DWORD resyncs;
    DWORD lateSkips;
};

struct PTARD3D9DiagPresentState
{
    DWORD magic;
    DWORD version;
    DWORD structSize;
    DWORD capacity;
    volatile LONGLONG qpcFrequency;
    volatile LONG writeSerial;
    DWORD reserved;
    PTARD3D9DiagPresentEvent events[PTAR_D3D9_DIAG_CAPACITY];
};
#pragma pack(pop)

extern "C" PTARD3D9DiagPresentState PTAR_D3D9_DIAG_STATE =
{
    PTAR_D3D9_DIAG_MAGIC,
    PTAR_D3D9_DIAG_VERSION,
    sizeof(PTARD3D9DiagPresentState),
    PTAR_D3D9_DIAG_CAPACITY,
    0,
    0,
    0,
    {}
};

#pragma comment(linker, "/EXPORT:PTAR_D3D9_DIAG_STATE=_PTAR_D3D9_DIAG_STATE,DATA")

static void PtD3D9DiagEnsureFrequency()
{
    if(PTAR_D3D9_DIAG_STATE.qpcFrequency>0)
        return;
    LARGE_INTEGER f={};
    if(!QueryPerformanceFrequency(&f) || f.QuadPart<=0)
        f.QuadPart=1;
    InterlockedCompareExchange64(
        const_cast<volatile LONGLONG*>(&PTAR_D3D9_DIAG_STATE.qpcFrequency),
        f.QuadPart,
        0);
}

static void PtD3D9DiagRecordPresent(
    bool generated,
    unsigned long markerSerial,
    LONGLONG visibleQpc,
    unsigned long realPresents,
    unsigned long generatedPresents,
    unsigned long resyncs,
    unsigned long lateSkips)
{
    PtD3D9DiagEnsureFrequency();
    const LONG serial=InterlockedIncrement(&PTAR_D3D9_DIAG_STATE.writeSerial);
    const unsigned long slot=((unsigned long)(serial-1)) & (PTAR_D3D9_DIAG_CAPACITY-1ul);
    PTARD3D9DiagPresentEvent* e=&PTAR_D3D9_DIAG_STATE.events[slot];
    InterlockedExchange(&e->sequence,-serial);
    MemoryBarrier();
    e->markerSerial=(DWORD)(markerSerial & 4095ul);
    e->generated=generated?1ul:0ul;
    e->qpc=visibleQpc;
    e->realPresents=(DWORD)realPresents;
    e->generatedPresents=(DWORD)generatedPresents;
    e->resyncs=(DWORD)resyncs;
    e->lateSkips=(DWORD)lateSkips;
    MemoryBarrier();
    InterlockedExchange(&e->sequence,serial);
}
