from pathlib import Path
import re

p=Path('compat/x86_d3d9/ptar_d3d9_isolated_presenter.h')
s=p.read_text(encoding='utf-8')

# D3D11 producer semantics are nonblocking: render/copy -> Flush -> publish,
# while consumer-side ownership synchronization prevents source-thread display
# waits. D3D9 has no keyed mutex on this path, so adapt that existing contract
# with an EVENT query whose completion is promoted opportunistically on later
# source-thread entries. Never spin/wait for GPU completion in HookPresent.

states='''#define PTAR_D3D9_ISO_FREE 0
#define PTAR_D3D9_ISO_WRITING 1
#define PTAR_D3D9_ISO_READY 2
#define PTAR_D3D9_ISO_DISPLAYING 3'''
new_states='''#define PTAR_D3D9_ISO_FREE 0
#define PTAR_D3D9_ISO_WRITING 1
#define PTAR_D3D9_ISO_GPU_PENDING 2
#define PTAR_D3D9_ISO_READY 3
#define PTAR_D3D9_ISO_DISPLAYING 4'''
if states not in s:
    raise SystemExit('mailbox state anchor missing')
s=s.replace(states,new_states,1)

pat=re.compile(r'''static bool PtD3D9IsoFenceProducer\(PTARD3D9IsoSlot\* slot\)\n\{.*?\n\}\n\nstatic int PtD3D9IsoAcquireWriteSlot''',re.S)
m=pat.search(s)
if not m:
    raise SystemExit('blocking producer fence function missing')
new='''static bool PtD3D9IsoIssueProducerFence(PTARD3D9IsoSlot* slot)
{
    if(!slot || !slot->producerFence)
        return false;

    HRESULT hr=slot->producerFence->Issue(D3DISSUE_END);
    if(FAILED(hr))
        return false;

    // D3DGETDATA_FLUSH submits queued producer work but S_FALSE is accepted.
    // This is deliberately one nonblocking probe, not a wait loop.
    hr=slot->producerFence->GetData(0,0,D3DGETDATA_FLUSH);
    if(hr==S_OK)
    {
        if(InterlockedCompareExchange(
            &slot->state,PTAR_D3D9_ISO_READY,
            PTAR_D3D9_ISO_GPU_PENDING)==PTAR_D3D9_ISO_GPU_PENDING)
            SetEvent(g_ptarD3D9Iso.readyEvent);
        return true;
    }
    return hr==S_FALSE;
}

static void PtD3D9IsoPromotePendingSlots()
{
    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[i];
        if(InterlockedCompareExchange(
            &slot->state,PTAR_D3D9_ISO_GPU_PENDING,
            PTAR_D3D9_ISO_GPU_PENDING)!=PTAR_D3D9_ISO_GPU_PENDING)
            continue;

        HRESULT hr=slot->producerFence->GetData(0,0,D3DGETDATA_FLUSH);
        if(hr==S_OK)
        {
            if(InterlockedCompareExchange(
                &slot->state,PTAR_D3D9_ISO_READY,
                PTAR_D3D9_ISO_GPU_PENDING)==PTAR_D3D9_ISO_GPU_PENDING)
                SetEvent(g_ptarD3D9Iso.readyEvent);
        }
        else if(FAILED(hr))
        {
            if(InterlockedCompareExchange(
                &slot->state,PTAR_D3D9_ISO_FREE,
                PTAR_D3D9_ISO_GPU_PENDING)==PTAR_D3D9_ISO_GPU_PENDING)
            {
                InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
                PtDiagLogA(
                    "ISO_PRESENTER_PRODUCER_FENCE_FAIL slot=%d hr=0x%08lX",
                    i,(unsigned long)hr);
            }
        }
    }
}

static int PtD3D9IsoAcquireWriteSlot'''
s=s[:m.start()]+new+s[m.end():]

# Promote already-submitted GPU work before attempting to reserve a slot.
anchor='''    if(!realSurface)
        return true;

    const int i=PtD3D9IsoAcquireWriteSlot();'''
repl='''    if(!realSurface)
        return true;

    PtD3D9IsoPromotePendingSlots();
    const int i=PtD3D9IsoAcquireWriteSlot();'''
if anchor not in s:
    raise SystemExit('submit acquire anchor missing')
s=s.replace(anchor,repl,1)

old='''    if(SUCCEEDED(hr) && !PtD3D9IsoFenceProducer(slot))
    {
        PtDiagLogA("ISO_PRESENTER_PRODUCER_FENCE_TIMEOUT slot=%d",i);
        hr=E_FAIL;
    }

    if(FAILED(hr))
    {
        InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);
        InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
        return true;
    }

    slot->sequence=InterlockedIncrement(&g_ptarD3D9Iso.writeSequence);
    MemoryBarrier();
    InterlockedExchange(&slot->state,PTAR_D3D9_ISO_READY);
    SetEvent(g_ptarD3D9Iso.readyEvent);
    return true;'''
new='''    if(FAILED(hr))
    {
        InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);
        InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
        return true;
    }

    slot->sequence=InterlockedIncrement(&g_ptarD3D9Iso.writeSequence);
    MemoryBarrier();
    InterlockedExchange(&slot->state,PTAR_D3D9_ISO_GPU_PENDING);

    if(!PtD3D9IsoIssueProducerFence(slot))
    {
        InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);
        InterlockedIncrement(&g_ptarD3D9Iso.mailboxDrops);
        PtDiagLogA("ISO_PRESENTER_PRODUCER_FENCE_ISSUE_FAIL slot=%d",i);
        return true;
    }

    // A second zero-wait promotion probe is harmless if Issue/GetData already
    // completed and helps low-latency cases without ever spinning the source.
    PtD3D9IsoPromotePendingSlots();
    return true;'''
if old not in s:
    raise SystemExit('blocking submit fence block missing')
s=s.replace(old,new,1)

p.write_text(s,encoding='utf-8',newline='\n')

# Regression gates: there must be no source-side polling loop or timeout wait.
t=p.read_text(encoding='utf-8')
if 'PtD3D9IsoFenceProducer' in t:
    raise SystemExit('obsolete blocking fence path remains')
if 'PTAR_D3D9_ISO_GPU_PENDING' not in t or 'PtD3D9IsoPromotePendingSlots' not in t:
    raise SystemExit('pending mailbox promotion missing')
segment=t[t.index('static bool PtD3D9IsoIssueProducerFence'):t.index('static int PtD3D9IsoAcquireWriteSlot')]
if 'for(;;)' in segment or 'GetTickCount()-start' in segment:
    raise SystemExit('source-side GPU wait loop remains')

print('D3D9_NONBLOCKING_MAILBOX_FENCE=PASS')
print('PRODUCER_FENCE=ISSUE_PLUS_ZERO_WAIT_PROMOTION')
print('SOURCE_GPU_WAIT_LOOP=NONE')
