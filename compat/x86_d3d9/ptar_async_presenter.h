#pragma once

#include <windows.h>
#include <d3d9.h>
#include "ptar_diag.h"
#include "ptar_fg_pacer.h"

// D3D9 async mailbox presenter.
//
// The game renders to PTAR's virtual source target. HookPresent reconstructs a
// REAL frame (and, when useful, one GENERATED frame), copies each completed
// output into a mailbox slot and returns to the game without waiting for scanout.
//
// A dedicated presenter thread owns the final backbuffer copy + real Present.
// The underlying D3D9 device is created with D3DPRESENT_INTERVAL_IMMEDIATE so
// Present itself never imposes a full VBlank stall while holding the D3D9
// multithreaded runtime lock. The thread paces scanout submissions with QPC.
//
// BeginScene/EndScene hooks and HookPresent use the same device gate so the
// presenter never calls Present while the game has an active scene.

typedef HRESULT (STDMETHODCALLTYPE *PTAR_PFN_PRESENT)(
    IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);

enum PTARAsyncSlotState
{
    PTAR_SLOT_FREE=0,
    PTAR_SLOT_WRITING=1,
    PTAR_SLOT_READY=2,
    PTAR_SLOT_PRESENTING=3
};

struct PTARAsyncSlot
{
    IDirect3DTexture9* texture;
    IDirect3DSurface9* surface;
    PTARAsyncSlotState state;
    bool generated;
    unsigned long sequence;
    LONGLONG readyQpc;
};

struct PTARAsyncPresenter
{
    IDirect3DDevice9* device;
    IDirect3DSurface9* backBuffer;
    PTAR_PFN_PRESENT realPresent;

    PTARAsyncSlot slots[6];

    HANDLE wakeEvent;
    HANDLE stopEvent;
    HANDLE thread;

    CRITICAL_SECTION queueLock;
    bool queueLockInitialized;

    LARGE_INTEGER frequency;
    double targetHz;
    LONGLONG periodTicks;
    LONGLONG nextTickQpc;

    volatile LONG running;
    volatile LONG enabled;

    HWND latestHwnd;

    unsigned long lastPresentedRealSequence;
    unsigned long presentedGenerated;
    unsigned long presentedReal;
    unsigned long droppedGenerated;
    unsigned long droppedReal;
    unsigned long reclaimedGenerated;
    unsigned long reclaimedReal;
    unsigned long presentErrors;
};

static PTARAsyncPresenter g_ptarAsyncPresenter={};

static INIT_ONCE g_ptarDeviceGateOnce=INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_ptarDeviceGate;

static BOOL CALLBACK PtAsyncInitDeviceGate(
    PINIT_ONCE,
    PVOID,
    PVOID*)
{
    InitializeCriticalSection(&g_ptarDeviceGate);
    return TRUE;
}

static void PtAsyncEnsureDeviceGate()
{
    InitOnceExecuteOnce(
        &g_ptarDeviceGateOnce,
        PtAsyncInitDeviceGate,
        0,0);
}

static void PtAsyncEnterDevice()
{
    PtAsyncEnsureDeviceGate();
    EnterCriticalSection(&g_ptarDeviceGate);
}

static void PtAsyncLeaveDevice()
{
    LeaveCriticalSection(&g_ptarDeviceGate);
}

static LONGLONG PtAsyncNow()
{
    LARGE_INTEGER q={};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static void PtAsyncWaitUntil(
    LONGLONG target,
    HANDLE stopEvent,
    HANDLE wakeEvent)
{
    const LONGLONG freq=
        g_ptarAsyncPresenter.frequency.QuadPart>0?
        g_ptarAsyncPresenter.frequency.QuadPart:1;

    for(;;)
    {
        const LONGLONG now=PtAsyncNow();
        if(now>=target)
            return;

        const double ms=
            (double)(target-now)*1000.0/(double)freq;

        if(ms>2.0)
        {
            DWORD waitMs=(DWORD)(ms-1.0);
            if(waitMs>20) waitMs=20;

            HANDLE events[2]={stopEvent,wakeEvent};
            DWORD wr=WaitForMultipleObjects(
                2,events,FALSE,waitMs);
            if(wr==WAIT_OBJECT_0)
                return;
        }
        else
        {
            SwitchToThread();
        }
    }
}

static void PtAsyncFreeSlotLocked(int index)
{
    if(index<0 || index>=6)
        return;
    g_ptarAsyncPresenter.slots[index].state=PTAR_SLOT_FREE;
}

static int PtAsyncFindFreeSlotLocked()
{
    for(int i=0;i<6;++i)
        if(g_ptarAsyncPresenter.slots[i].state==PTAR_SLOT_FREE)
            return i;
    return -1;
}

static int PtAsyncFindOldestReadyLocked(bool generatedOnly)
{
    int found=-1;
    unsigned long seq=0xFFFFFFFFul;
    for(int i=0;i<6;++i)
    {
        const PTARAsyncSlot& s=g_ptarAsyncPresenter.slots[i];
        if(s.state!=PTAR_SLOT_READY)
            continue;
        if(generatedOnly && !s.generated)
            continue;
        if(s.sequence<seq)
        {
            seq=s.sequence;
            found=i;
        }
    }
    return found;
}

static int PtAsyncFindNewestReadyRealLocked()
{
    int found=-1;
    unsigned long seq=0;
    for(int i=0;i<6;++i)
    {
        const PTARAsyncSlot& s=g_ptarAsyncPresenter.slots[i];
        if(s.state==PTAR_SLOT_READY && !s.generated)
        {
            if(found<0 || s.sequence>=seq)
            {
                seq=s.sequence;
                found=i;
            }
        }
    }
    return found;
}

static int PtAsyncFindReadyGeneratedForSequenceLocked(
    unsigned long sequence)
{
    for(int i=0;i<6;++i)
    {
        const PTARAsyncSlot& s=g_ptarAsyncPresenter.slots[i];
        if(s.state==PTAR_SLOT_READY &&
           s.generated &&
           s.sequence==sequence)
            return i;
    }
    return -1;
}

static void PtAsyncDiscardStaleLocked(unsigned long throughSequence)
{
    for(int i=0;i<6;++i)
    {
        PTARAsyncSlot& s=g_ptarAsyncPresenter.slots[i];
        if(s.state!=PTAR_SLOT_READY)
            continue;

        if(s.sequence<=throughSequence)
        {
            if(s.generated)
                ++g_ptarAsyncPresenter.droppedGenerated;
            else
                ++g_ptarAsyncPresenter.droppedReal;
            s.state=PTAR_SLOT_FREE;
        }
    }
}

static int PtAsyncSelectForPresentLocked()
{
    // Remove frames older than the last REAL already shown.
    PtAsyncDiscardStaleLocked(
        g_ptarAsyncPresenter.lastPresentedRealSequence);

    const int newestReal=PtAsyncFindNewestReadyRealLocked();
    if(newestReal<0)
        return -1;

    const unsigned long realSeq=
        g_ptarAsyncPresenter.slots[newestReal].sequence;
    const unsigned long nextSeq=
        g_ptarAsyncPresenter.lastPresentedRealSequence+1ul;

    // If the producer has outrun the output clock, jump to the newest REAL.
    // This bounds latency and discards obsolete GENERATED frames instead of
    // slowing the game thread.
    if(realSeq>nextSeq)
    {
        for(int i=0;i<6;++i)
        {
            PTARAsyncSlot& s=g_ptarAsyncPresenter.slots[i];
            if(s.state==PTAR_SLOT_READY &&
               s.sequence<realSeq)
            {
                if(s.generated)
                    ++g_ptarAsyncPresenter.droppedGenerated;
                else
                    ++g_ptarAsyncPresenter.droppedReal;
                s.state=PTAR_SLOT_FREE;
            }
        }

        g_ptarAsyncPresenter.slots[newestReal].state=
            PTAR_SLOT_PRESENTING;
        return newestReal;
    }

    // For an in-order pair, GENERATED is shown first when it exists, followed
    // by the matching REAL on the next output tick.
    const int generated=
        PtAsyncFindReadyGeneratedForSequenceLocked(realSeq);
    if(generated>=0)
    {
        g_ptarAsyncPresenter.slots[generated].state=
            PTAR_SLOT_PRESENTING;
        return generated;
    }

    g_ptarAsyncPresenter.slots[newestReal].state=
        PTAR_SLOT_PRESENTING;
    return newestReal;
}

static DWORD WINAPI PtAsyncPresenterThread(LPVOID)
{
    PTARAsyncPresenter& p=g_ptarAsyncPresenter;
    p.nextTickQpc=PtAsyncNow();

    PtDiagLogA(
        "ASYNC_PRESENTER_THREAD_START target_hz=%.3f period_ticks=%lld",
        p.targetHz,
        (long long)p.periodTicks);

    while(InterlockedCompareExchange(&p.running,1,1)!=0)
    {
        HANDLE events[2]={p.stopEvent,p.wakeEvent};
        DWORD wr=WaitForMultipleObjects(
            2,events,FALSE,10);

        if(wr==WAIT_OBJECT_0)
            break;

        if(InterlockedCompareExchange(&p.enabled,1,1)==0)
            continue;

        const LONGLONG now=PtAsyncNow();
        if(p.nextTickQpc<=0)
            p.nextTickQpc=now;

        if(now<p.nextTickQpc)
            PtAsyncWaitUntil(
                p.nextTickQpc,
                p.stopEvent,
                p.wakeEvent);

        if(WaitForSingleObject(p.stopEvent,0)==WAIT_OBJECT_0)
            break;

        int slotIndex=-1;
        EnterCriticalSection(&p.queueLock);
        slotIndex=PtAsyncSelectForPresentLocked();
        LeaveCriticalSection(&p.queueLock);

        if(slotIndex<0)
        {
            // No new output frame. Do not manufacture duplicate Presents.
            p.nextTickQpc=PtAsyncNow()+p.periodTicks;
            continue;
        }

        PTARAsyncSlot& slot=p.slots[slotIndex];

        PtAsyncEnterDevice();

        HRESULT copyHr=p.device->StretchRect(
            slot.surface,0,
            p.backBuffer,0,
            D3DTEXF_NONE);

        HRESULT presentHr=copyHr;
        if(SUCCEEDED(copyHr))
            presentHr=p.realPresent(
                p.device,0,0,p.latestHwnd,0);

        PtAsyncLeaveDevice();

        if(SUCCEEDED(presentHr))
        {
            PtFgPacerRecordVisible(slot.generated);

            if(slot.generated)
                ++p.presentedGenerated;
            else
            {
                ++p.presentedReal;
                p.lastPresentedRealSequence=slot.sequence;
            }
        }
        else
        {
            ++p.presentErrors;
            PtDiagLogA(
                "ASYNC_PRESENTER_PRESENT_FAIL seq=%lu generated=%d "
                "copy_hr=0x%08lX present_hr=0x%08lX",
                slot.sequence,
                slot.generated?1:0,
                (unsigned long)copyHr,
                (unsigned long)presentHr);
        }

        EnterCriticalSection(&p.queueLock);
        slot.state=PTAR_SLOT_FREE;
        // A successfully displayed REAL makes older queued members obsolete.
        if(!slot.generated && SUCCEEDED(presentHr))
            PtAsyncDiscardStaleLocked(slot.sequence);
        LeaveCriticalSection(&p.queueLock);

        const LONGLONG after=PtAsyncNow();
        p.nextTickQpc+=p.periodTicks;
        if(after>p.nextTickQpc+p.periodTicks)
        {
            PtDiagLogA(
                "ASYNC_PRESENTER_RESYNC late_ticks=%lld",
                (long long)(after-p.nextTickQpc));
            p.nextTickQpc=after+p.periodTicks;
        }
    }

    PtDiagLogA(
        "ASYNC_PRESENTER_THREAD_STOP real=%lu gen=%lu drop_real=%lu "
        "drop_gen=%lu errors=%lu",
        p.presentedReal,
        p.presentedGenerated,
        p.droppedReal,
        p.droppedGenerated,
        p.presentErrors);

    return 0;
}

static void PtAsyncPresenterReleaseResources()
{
    PTARAsyncPresenter& p=g_ptarAsyncPresenter;

    if(p.thread)
    {
        SetEvent(p.stopEvent);
        InterlockedExchange(&p.running,0);
        WaitForSingleObject(p.thread,3000);
        CloseHandle(p.thread);
        p.thread=0;
    }

    if(p.wakeEvent){CloseHandle(p.wakeEvent);p.wakeEvent=0;}
    if(p.stopEvent){CloseHandle(p.stopEvent);p.stopEvent=0;}

    for(int i=0;i<6;++i)
    {
        if(p.slots[i].surface)
        {
            p.slots[i].surface->Release();
            p.slots[i].surface=0;
        }
        if(p.slots[i].texture)
        {
            p.slots[i].texture->Release();
            p.slots[i].texture=0;
        }
        p.slots[i].state=PTAR_SLOT_FREE;
    }

    if(p.backBuffer)
    {
        p.backBuffer->Release();
        p.backBuffer=0;
    }

    if(p.queueLockInitialized)
    {
        DeleteCriticalSection(&p.queueLock);
        p.queueLockInitialized=false;
    }

    p.device=0;
    p.realPresent=0;
    p.latestHwnd=0;
    p.targetHz=0.0;
    p.periodTicks=0;
    p.nextTickQpc=0;
    p.lastPresentedRealSequence=0;
    p.presentedGenerated=0;
    p.presentedReal=0;
    p.droppedGenerated=0;
    p.droppedReal=0;
    p.reclaimedGenerated=0;
    p.reclaimedReal=0;
    p.presentErrors=0;
    InterlockedExchange(&p.enabled,0);
}

static HRESULT PtAsyncPresenterInitialize(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* backBuffer,
    UINT width,
    UINT height,
    D3DFORMAT format,
    PTAR_PFN_PRESENT realPresent,
    double targetHz)
{
    PtAsyncPresenterReleaseResources();

    if(!dev || !backBuffer || !width || !height || !realPresent)
        return D3DERR_INVALIDCALL;

    PTARAsyncPresenter& p=g_ptarAsyncPresenter;

    QueryPerformanceFrequency(&p.frequency);
    if(p.frequency.QuadPart<=0)
        p.frequency.QuadPart=1;

    if(targetHz<30.0 || targetHz>360.0)
        targetHz=60.0;

    p.targetHz=targetHz;
    p.periodTicks=(LONGLONG)(
        (double)p.frequency.QuadPart/targetHz+0.5);
    if(p.periodTicks<1)
        p.periodTicks=1;

    p.device=dev;
    p.realPresent=realPresent;
    p.backBuffer=backBuffer;
    p.backBuffer->AddRef();

    InitializeCriticalSection(&p.queueLock);
    p.queueLockInitialized=true;

    for(int i=0;i<6;++i)
    {
        HRESULT hr=dev->CreateTexture(
            width,height,1,
            D3DUSAGE_RENDERTARGET,
            format,
            D3DPOOL_DEFAULT,
            &p.slots[i].texture,
            0);
        if(FAILED(hr) || !p.slots[i].texture)
        {
            PtDiagLogA(
                "ASYNC_PRESENTER_SLOT_TEXTURE_FAIL index=%d hr=0x%08lX",
                i,(unsigned long)hr);
            PtAsyncPresenterReleaseResources();
            return FAILED(hr)?hr:E_FAIL;
        }

        hr=p.slots[i].texture->GetSurfaceLevel(
            0,&p.slots[i].surface);
        if(FAILED(hr) || !p.slots[i].surface)
        {
            PtDiagLogA(
                "ASYNC_PRESENTER_SLOT_SURFACE_FAIL index=%d hr=0x%08lX",
                i,(unsigned long)hr);
            PtAsyncPresenterReleaseResources();
            return FAILED(hr)?hr:E_FAIL;
        }

        p.slots[i].state=PTAR_SLOT_FREE;
    }

    p.wakeEvent=CreateEventW(0,FALSE,FALSE,0);
    p.stopEvent=CreateEventW(0,TRUE,FALSE,0);
    if(!p.wakeEvent || !p.stopEvent)
    {
        HRESULT hr=HRESULT_FROM_WIN32(GetLastError());
        PtAsyncPresenterReleaseResources();
        return hr;
    }

    InterlockedExchange(&p.running,1);
    InterlockedExchange(&p.enabled,1);

    p.thread=CreateThread(
        0,0,
        PtAsyncPresenterThread,
        0,0,0);
    if(!p.thread)
    {
        HRESULT hr=HRESULT_FROM_WIN32(GetLastError());
        InterlockedExchange(&p.running,0);
        PtAsyncPresenterReleaseResources();
        return hr;
    }

    PtDiagLogA(
        "ASYNC_PRESENTER_INIT width=%u height=%u format=%u "
        "target_hz=%.3f slots=6 interval=IMMEDIATE",
        width,height,(unsigned)format,targetHz);

    return S_OK;
}

static bool PtAsyncPresenterAvailable()
{
    return g_ptarAsyncPresenter.thread!=0;
}

static bool PtAsyncPresenterIsActive()
{
    return
        PtAsyncPresenterAvailable() &&
        InterlockedCompareExchange(
            &g_ptarAsyncPresenter.enabled,1,1)!=0;
}

static void PtAsyncPresenterSetEnabled(bool enabled)
{
    InterlockedExchange(
        &g_ptarAsyncPresenter.enabled,
        enabled?1:0);

    if(enabled && g_ptarAsyncPresenter.wakeEvent)
        SetEvent(g_ptarAsyncPresenter.wakeEvent);

    PtDiagLogA(
        "ASYNC_PRESENTER_ENABLED=%d",
        enabled?1:0);
}

static int PtAsyncPresenterAcquireSlot(
    bool generated,
    unsigned long sequence)
{
    PTARAsyncPresenter& p=g_ptarAsyncPresenter;
    if(!p.queueLockInitialized)
        return -1;

    EnterCriticalSection(&p.queueLock);

    int index=PtAsyncFindFreeSlotLocked();

    if(index<0)
    {
        // GENERATED never blocks the producer. If the mailbox is full, drop
        // it first because preserving REAL source throughput has priority.
        if(generated)
        {
            ++p.droppedGenerated;
            LeaveCriticalSection(&p.queueLock);
            return -1;
        }

        index=PtAsyncFindOldestReadyLocked(true);
        if(index>=0)
        {
            ++p.reclaimedGenerated;
            p.slots[index].state=PTAR_SLOT_FREE;
        }
        else
        {
            index=PtAsyncFindOldestReadyLocked(false);
            if(index>=0)
            {
                ++p.reclaimedReal;
                p.slots[index].state=PTAR_SLOT_FREE;
            }
        }
    }

    if(index>=0)
    {
        PTARAsyncSlot& slot=p.slots[index];
        slot.state=PTAR_SLOT_WRITING;
        slot.generated=generated;
        slot.sequence=sequence;
        slot.readyQpc=0;
    }

    LeaveCriticalSection(&p.queueLock);
    return index;
}

static IDirect3DSurface9* PtAsyncPresenterSlotSurface(int index)
{
    if(index<0 || index>=6)
        return 0;
    return g_ptarAsyncPresenter.slots[index].surface;
}

static void PtAsyncPresenterCancelSlot(int index)
{
    PTARAsyncPresenter& p=g_ptarAsyncPresenter;
    if(index<0 || index>=6 || !p.queueLockInitialized)
        return;

    EnterCriticalSection(&p.queueLock);
    p.slots[index].state=PTAR_SLOT_FREE;
    LeaveCriticalSection(&p.queueLock);
}

static void PtAsyncPresenterCommitSlot(
    int index,
    HWND hwnd)
{
    PTARAsyncPresenter& p=g_ptarAsyncPresenter;
    if(index<0 || index>=6 || !p.queueLockInitialized)
        return;

    EnterCriticalSection(&p.queueLock);
    PTARAsyncSlot& slot=p.slots[index];
    if(slot.state==PTAR_SLOT_WRITING)
    {
        slot.readyQpc=PtAsyncNow();
        slot.state=PTAR_SLOT_READY;
        p.latestHwnd=hwnd;
    }
    LeaveCriticalSection(&p.queueLock);

    if(p.wakeEvent)
        SetEvent(p.wakeEvent);
}

static unsigned long PtAsyncPresenterPresentedReal()
{
    return g_ptarAsyncPresenter.presentedReal;
}

static unsigned long PtAsyncPresenterPresentedGenerated()
{
    return g_ptarAsyncPresenter.presentedGenerated;
}

static unsigned long PtAsyncPresenterDroppedReal()
{
    return g_ptarAsyncPresenter.droppedReal+
           g_ptarAsyncPresenter.reclaimedReal;
}

static unsigned long PtAsyncPresenterDroppedGenerated()
{
    return g_ptarAsyncPresenter.droppedGenerated+
           g_ptarAsyncPresenter.reclaimedGenerated;
}
