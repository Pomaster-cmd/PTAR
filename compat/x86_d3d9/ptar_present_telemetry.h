#pragma once

#include <windows.h>

// Additive diagnostics transport for D3D9 CTRL+F1/F5 measurements.
//
// This deliberately does not touch the FG pacing policy.  The producer is
// called only after the real D3D9 Present succeeds.  A controller in another
// process reads the named shared-memory ring instead of attempting to capture
// an exclusive-fullscreen swap chain through GDI/BitBlt.
//
// Cross-bitness contract: all offsets are fixed and the event ring begins at
// byte 64.  The x86 runtime is the only writer.  Readers never write the map.

#define PTAR_PRESENT_TELEMETRY_MAGIC   0x31545039ul /* "9PT1" */
#define PTAR_PRESENT_TELEMETRY_VERSION 1ul
#define PTAR_PRESENT_TELEMETRY_CAPACITY 65536ul
#define PTAR_PRESENT_TELEMETRY_MAPPING L"Local\\PTAR_D3D9_PRESENT_TELEMETRY_V1"

#pragma pack(push,8)
struct PTARPresentTelemetryV1
{
    DWORD magic;               // 0
    DWORD version;             // 4
    DWORD structSize;          // 8
    DWORD capacity;            // 12
    volatile LONG sequence;    // 16, even when quiescent
    DWORD processId;           // 20
    LONGLONG qpcFrequency;     // 24
    volatile LONG writeIndex;  // 32, total events written (wraps only after ~2B)
    volatile LONG totalFrames; // 36
    volatile LONG realFrames;  // 40
    volatile LONG generatedFrames; // 44
    LONGLONG lastQpc;          // 48
    volatile LONG writerLock;  // 56, non-blocking producer guard
    DWORD reserved0;           // 60
    LONGLONG qpc[PTAR_PRESENT_TELEMETRY_CAPACITY];
    BYTE kind[PTAR_PRESENT_TELEMETRY_CAPACITY]; // 0=REAL, 1=GENERATED
};
#pragma pack(pop)

static HANDLE g_ptarPresentTelemetryMapping=0;
static PTARPresentTelemetryV1* g_ptarPresentTelemetry=0;
static volatile LONG g_ptarPresentTelemetryInit=0;

static void PtPresentTelemetryEnsure()
{
    if(g_ptarPresentTelemetry)
        return;

    if(InterlockedCompareExchange(&g_ptarPresentTelemetryInit,1,0)!=0)
        return;

    HANDLE mapping=CreateFileMappingW(
        INVALID_HANDLE_VALUE,0,PAGE_READWRITE,0,
        (DWORD)sizeof(PTARPresentTelemetryV1),
        PTAR_PRESENT_TELEMETRY_MAPPING);
    if(!mapping)
    {
        InterlockedExchange(&g_ptarPresentTelemetryInit,0);
        return;
    }

    PTARPresentTelemetryV1* p=(PTARPresentTelemetryV1*)MapViewOfFile(
        mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(PTARPresentTelemetryV1));
    if(!p)
    {
        CloseHandle(mapping);
        InterlockedExchange(&g_ptarPresentTelemetryInit,0);
        return;
    }

    // Reset the map for this runtime instance.  A controller may already hold
    // a view left from the previous game process; processId lets it detect the
    // new producer without any filesystem rendezvous.
    ZeroMemory(p,sizeof(*p));
    LARGE_INTEGER freq={};
    QueryPerformanceFrequency(&freq);
    if(freq.QuadPart<=0) freq.QuadPart=1;

    p->magic=PTAR_PRESENT_TELEMETRY_MAGIC;
    p->version=PTAR_PRESENT_TELEMETRY_VERSION;
    p->structSize=(DWORD)sizeof(*p);
    p->capacity=PTAR_PRESENT_TELEMETRY_CAPACITY;
    p->processId=GetCurrentProcessId();
    p->qpcFrequency=freq.QuadPart;
    MemoryBarrier();

    g_ptarPresentTelemetryMapping=mapping;
    g_ptarPresentTelemetry=p;
}

static void PtPresentTelemetryRecord(bool generated)
{
    PtPresentTelemetryEnsure();
    PTARPresentTelemetryV1* p=g_ptarPresentTelemetry;
    if(!p)
        return;

    // D3D9 normally presents on one render thread.  If a title violates that,
    // diagnostics drop the colliding sample rather than ever stalling the game.
    if(InterlockedCompareExchange(&p->writerLock,1,0)!=0)
        return;

    LARGE_INTEGER now={};
    QueryPerformanceCounter(&now);

    InterlockedIncrement(&p->sequence); // odd: update in progress
    MemoryBarrier();

    LONG wi=p->writeIndex;
    const DWORD slot=((DWORD)wi)%PTAR_PRESENT_TELEMETRY_CAPACITY;
    p->qpc[slot]=now.QuadPart;
    p->kind[slot]=generated?1:0;
    p->lastQpc=now.QuadPart;
    p->writeIndex=wi+1;
    p->totalFrames=p->totalFrames+1;
    if(generated)
        p->generatedFrames=p->generatedFrames+1;
    else
        p->realFrames=p->realFrames+1;

    MemoryBarrier();
    InterlockedIncrement(&p->sequence); // even: committed
    InterlockedExchange(&p->writerLock,0);
}
