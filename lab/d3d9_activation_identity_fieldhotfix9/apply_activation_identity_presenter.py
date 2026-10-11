from pathlib import Path
import re

hp=Path('compat/x86_d3d9/ptar_d3d9_isolated_presenter.h')
pp=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp')
h=hp.read_text(encoding='utf-8')
p=pp.read_text(encoding='utf-8')

# FIELDHOTFIX9: retain the D3D11 GW12->GW16 isolated-presenter/mailbox
# topology, but adapt two D3D9-specific lifetime/Optimus requirements exposed
# by the HF8 field result (CTRL+F6 immediately returns to OFF):
# 1) Direct3D9 device creation belongs on the source/focus-window thread, not
#    inside the display worker. Create/open the D3D9Ex presenter resources on
#    the caller thread; the D3DCREATE_MULTITHREADED worker owns Present only.
# 2) IDirect3D9 and IDirect3D9Ex adapter ordinals are not assumed equivalent on
#    hybrid systems. Remember the producer D3DADAPTER_IDENTIFIER9 and map the
#    Ex presenter by VendorId/DeviceId/SubSysId/Revision, with ordinal fallback.
# 3) Match the D3D11 activation lifetime: do not prepare the isolated presenter
#    while the game's CreateDevice is still unwinding. Prepare it lazily on the
#    first FG-enabled Present; after success soft-OFF keeps it armed.

anchor='static HWND g_ptarD3D9IsoPreferredWindow=0;\n'
if anchor not in h:
    raise SystemExit('preferred-window anchor missing')
extra=r'''
static IDirect3D9Ex* g_ptarD3D9IsoDisplayD3D=0;
static IDirect3DDevice9Ex* g_ptarD3D9IsoDisplayDevice=0;
static IDirect3DTexture9* g_ptarD3D9IsoDisplayRealTex[PTAR_D3D9_ISO_SLOT_COUNT]={};
static IDirect3DSurface9* g_ptarD3D9IsoDisplayRealSurf[PTAR_D3D9_ISO_SLOT_COUNT]={};
static IDirect3DTexture9* g_ptarD3D9IsoDisplayGenTex[PTAR_D3D9_ISO_SLOT_COUNT]={};
static IDirect3DSurface9* g_ptarD3D9IsoDisplayGenSurf[PTAR_D3D9_ISO_SLOT_COUNT]={};
static D3DADAPTER_IDENTIFIER9 g_ptarD3D9IsoSourceAdapterId={};
static bool g_ptarD3D9IsoSourceAdapterIdValid=false;
static UINT g_ptarD3D9IsoSourceAdapterOrdinal=0;
static D3DDEVTYPE g_ptarD3D9IsoSourceDeviceType=D3DDEVTYPE_HAL;
static DWORD g_ptarD3D9IsoSourceCreationThreadId=0;

static void PtD3D9IsolatedPresenterRememberSourceAdapter(
    IDirect3D9* d3d,
    UINT adapter,
    D3DDEVTYPE type,
    HWND hwnd)
{
    g_ptarD3D9IsoSourceAdapterOrdinal=adapter;
    g_ptarD3D9IsoSourceDeviceType=type;
    g_ptarD3D9IsoSourceCreationThreadId=GetCurrentThreadId();
    if(hwnd)
        g_ptarD3D9IsoPreferredWindow=hwnd;

    ZeroMemory(&g_ptarD3D9IsoSourceAdapterId,sizeof(g_ptarD3D9IsoSourceAdapterId));
    g_ptarD3D9IsoSourceAdapterIdValid=false;
    if(d3d)
    {
        HRESULT hr=d3d->GetAdapterIdentifier(
            adapter,0,&g_ptarD3D9IsoSourceAdapterId);
        if(SUCCEEDED(hr))
        {
            g_ptarD3D9IsoSourceAdapterIdValid=true;
            PtDiagLogA(
                "ISO_PRESENTER_SOURCE_ID adapter=%u vendor=0x%04lX device=0x%04lX subsys=0x%08lX thread=%lu",
                adapter,
                (unsigned long)g_ptarD3D9IsoSourceAdapterId.VendorId,
                (unsigned long)g_ptarD3D9IsoSourceAdapterId.DeviceId,
                (unsigned long)g_ptarD3D9IsoSourceAdapterId.SubSysId,
                (unsigned long)g_ptarD3D9IsoSourceCreationThreadId);
        }
        else
        {
            PtDiagLogA(
                "ISO_PRESENTER_SOURCE_ID_FAIL adapter=%u hr=0x%08lX",
                adapter,(unsigned long)hr);
        }
    }
}

static bool PtD3D9IsoSameAdapterIdentity(
    const D3DADAPTER_IDENTIFIER9& a,
    const D3DADAPTER_IDENTIFIER9& b)
{
    return a.VendorId==b.VendorId &&
           a.DeviceId==b.DeviceId &&
           a.SubSysId==b.SubSysId &&
           a.Revision==b.Revision;
}

static UINT PtD3D9IsoResolvePresenterAdapter(IDirect3D9Ex* d3d,UINT fallback)
{
    if(!d3d)
        return fallback;

    const UINT count=d3d->GetAdapterCount();
    if(g_ptarD3D9IsoSourceAdapterIdValid)
    {
        for(UINT i=0;i<count;++i)
        {
            D3DADAPTER_IDENTIFIER9 id={};
            if(SUCCEEDED(d3d->GetAdapterIdentifier(i,0,&id)) &&
               PtD3D9IsoSameAdapterIdentity(id,g_ptarD3D9IsoSourceAdapterId))
            {
                PtDiagLogA(
                    "ISO_PRESENTER_ADAPTER_MATCH source_ordinal=%u ex_ordinal=%u vendor=0x%04lX device=0x%04lX",
                    fallback,i,(unsigned long)id.VendorId,(unsigned long)id.DeviceId);
                return i;
            }
        }
        PtDiagLogA(
            "ISO_PRESENTER_ADAPTER_IDENTITY_NOT_FOUND source_ordinal=%u ex_count=%u fallback=ordinal",
            fallback,count);
    }

    return fallback<count?fallback:D3DADAPTER_DEFAULT;
}

static void PtD3D9IsoReleaseDisplayResources()
{
    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        if(g_ptarD3D9IsoDisplayGenSurf[i]){g_ptarD3D9IsoDisplayGenSurf[i]->Release();g_ptarD3D9IsoDisplayGenSurf[i]=0;}
        if(g_ptarD3D9IsoDisplayGenTex[i]){g_ptarD3D9IsoDisplayGenTex[i]->Release();g_ptarD3D9IsoDisplayGenTex[i]=0;}
        if(g_ptarD3D9IsoDisplayRealSurf[i]){g_ptarD3D9IsoDisplayRealSurf[i]->Release();g_ptarD3D9IsoDisplayRealSurf[i]=0;}
        if(g_ptarD3D9IsoDisplayRealTex[i]){g_ptarD3D9IsoDisplayRealTex[i]->Release();g_ptarD3D9IsoDisplayRealTex[i]=0;}
    }
    if(g_ptarD3D9IsoDisplayDevice){g_ptarD3D9IsoDisplayDevice->Release();g_ptarD3D9IsoDisplayDevice=0;}
    if(g_ptarD3D9IsoDisplayD3D){g_ptarD3D9IsoDisplayD3D->Release();g_ptarD3D9IsoDisplayD3D=0;}
}
'''
h=h.replace(anchor,anchor+extra,1)

thread_start=h.find('static DWORD WINAPI PtD3D9IsoPresenterThread')
owns_start=h.find('static bool PtD3D9IsolatedPresenterOwnsDisplay')
if thread_start<0 or owns_start<0 or owns_start<=thread_start:
    raise SystemExit('presenter thread/start block anchors missing')
replacement=r'''static bool PtD3D9IsoCreateDisplayResourcesOnCallerThread()
{
    PtD3D9IsoReleaseDisplayResources();

    HRESULT hr=g_sysDirect3DCreate9Ex?
        g_sysDirect3DCreate9Ex(D3D_SDK_VERSION,&g_ptarD3D9IsoDisplayD3D):E_NOTIMPL;
    if(FAILED(hr) || !g_ptarD3D9IsoDisplayD3D)
    {
        PtDiagLogA("ISO_PRESENTER_CREATE9EX_FAIL hr=0x%08lX",(unsigned long)hr);
        PtD3D9IsoReleaseDisplayResources();
        return false;
    }

    g_ptarD3D9Iso.adapterOrdinal=PtD3D9IsoResolvePresenterAdapter(
        g_ptarD3D9IsoDisplayD3D,g_ptarD3D9Iso.adapterOrdinal);

    D3DDISPLAYMODE dm={};
    hr=g_ptarD3D9IsoDisplayD3D->GetAdapterDisplayMode(
        g_ptarD3D9Iso.adapterOrdinal,&dm);
    if(FAILED(hr))
        dm.Format=g_ptarD3D9Iso.format!=D3DFMT_UNKNOWN?
            g_ptarD3D9Iso.format:D3DFMT_X8R8G8B8;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=g_ptarD3D9Iso.width;
    pp.BackBufferHeight=g_ptarD3D9Iso.height;
    pp.BackBufferFormat=dm.Format;
    pp.BackBufferCount=3;
    pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality=0;
    pp.SwapEffect=D3DSWAPEFFECT_FLIPEX;
    pp.hDeviceWindow=g_ptarD3D9Iso.hwnd;
    pp.Windowed=TRUE;
    pp.EnableAutoDepthStencil=FALSE;
    pp.Flags=0;
    pp.FullScreen_RefreshRateInHz=0;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;

    DWORD behavior=
        D3DCREATE_HARDWARE_VERTEXPROCESSING|
        D3DCREATE_MULTITHREADED|
        D3DCREATE_FPU_PRESERVE;

    hr=g_ptarD3D9IsoDisplayD3D->CreateDeviceEx(
        g_ptarD3D9Iso.adapterOrdinal,g_ptarD3D9Iso.deviceType,
        g_ptarD3D9Iso.hwnd,behavior,&pp,0,&g_ptarD3D9IsoDisplayDevice);

    if(FAILED(hr) || !g_ptarD3D9IsoDisplayDevice)
    {
        PtDiagLogA(
            "ISO_PRESENTER_FLIPEX_CREATE_FAIL hr=0x%08lX retry=DISCARD adapter=%u thread=%lu",
            (unsigned long)hr,g_ptarD3D9Iso.adapterOrdinal,
            (unsigned long)GetCurrentThreadId());
        pp.BackBufferCount=1;
        pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
        behavior=
            D3DCREATE_SOFTWARE_VERTEXPROCESSING|
            D3DCREATE_MULTITHREADED|
            D3DCREATE_FPU_PRESERVE;
        hr=g_ptarD3D9IsoDisplayD3D->CreateDeviceEx(
            g_ptarD3D9Iso.adapterOrdinal,g_ptarD3D9Iso.deviceType,
            g_ptarD3D9Iso.hwnd,behavior,&pp,0,&g_ptarD3D9IsoDisplayDevice);
        InterlockedExchange(&g_ptarD3D9Iso.flipExActive,0);
    }
    else
    {
        InterlockedExchange(&g_ptarD3D9Iso.flipExActive,1);
    }

    if(FAILED(hr) || !g_ptarD3D9IsoDisplayDevice)
    {
        PtDiagLogA(
            "ISO_PRESENTER_DEVICE_CREATE_FAIL hr=0x%08lX adapter=%u type=%u thread=%lu",
            (unsigned long)hr,g_ptarD3D9Iso.adapterOrdinal,
            (unsigned)g_ptarD3D9Iso.deviceType,
            (unsigned long)GetCurrentThreadId());
        PtD3D9IsoReleaseDisplayResources();
        return false;
    }

    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
    {
        hr=PtD3D9IsoOpenSharedTexture(
            g_ptarD3D9IsoDisplayDevice,g_ptarD3D9Iso.slots[i].realHandle,
            &g_ptarD3D9IsoDisplayRealTex[i],&g_ptarD3D9IsoDisplayRealSurf[i]);
        if(FAILED(hr))
            break;
        hr=PtD3D9IsoOpenSharedTexture(
            g_ptarD3D9IsoDisplayDevice,g_ptarD3D9Iso.slots[i].generatedHandle,
            &g_ptarD3D9IsoDisplayGenTex[i],&g_ptarD3D9IsoDisplayGenSurf[i]);
        if(FAILED(hr))
            break;
    }

    if(FAILED(hr))
    {
        PtDiagLogA(
            "ISO_PRESENTER_SHARED_OPEN_FAIL hr=0x%08lX adapter=%u thread=%lu",
            (unsigned long)hr,g_ptarD3D9Iso.adapterOrdinal,
            (unsigned long)GetCurrentThreadId());
        PtD3D9IsoReleaseDisplayResources();
        return false;
    }

    return true;
}

static DWORD WINAPI PtD3D9IsoPresenterThread(LPVOID)
{
    IDirect3DDevice9Ex* dev=g_ptarD3D9IsoDisplayDevice;
    if(!dev)
    {
        InterlockedExchange(&g_ptarD3D9Iso.healthy,0);
        InterlockedExchange(&g_ptarD3D9Iso.active,0);
        if(g_ptarD3D9Iso.initEvent) SetEvent(g_ptarD3D9Iso.initEvent);
        return 1;
    }

    PtDiagLogA(
        "ISO_PRESENTER_READY api=D3D9EX mode=%s interval=ONE size=%ux%u mailbox=%d order=GENERATED_THEN_MATCHING_REAL adapter=%u type=%u create_thread=%lu present_thread=%lu",
        g_ptarD3D9Iso.flipExActive?"FLIPEX":"DISCARD",
        g_ptarD3D9Iso.width,g_ptarD3D9Iso.height,
        PTAR_D3D9_ISO_SLOT_COUNT,
        g_ptarD3D9Iso.adapterOrdinal,(unsigned)g_ptarD3D9Iso.deviceType,
        (unsigned long)g_ptarD3D9IsoSourceCreationThreadId,
        (unsigned long)GetCurrentThreadId());
    if(g_ptarD3D9Iso.initEvent) SetEvent(g_ptarD3D9Iso.initEvent);

    while(InterlockedCompareExchange(&g_ptarD3D9Iso.stopRequested,0,0)==0)
    {
        int slotIndex=PtD3D9IsoClaimLatestReady();
        if(slotIndex<0)
        {
            WaitForSingleObject(g_ptarD3D9Iso.readyEvent,50);
            continue;
        }

        PTARD3D9IsoSlot* slot=&g_ptarD3D9Iso.slots[slotIndex];
        HRESULT hr=S_OK;
        if(slot->hasGenerated)
        {
            hr=PtD3D9IsoPresentOne(
                dev,g_ptarD3D9IsoDisplayGenSurf[slotIndex],true,slot->generatedMarker);
        }
        if(SUCCEEDED(hr))
        {
            hr=PtD3D9IsoPresentOne(
                dev,g_ptarD3D9IsoDisplayRealSurf[slotIndex],false,slot->realMarker);
        }

        InterlockedExchange(&slot->state,PTAR_D3D9_ISO_FREE);

        if(FAILED(hr))
        {
            InterlockedIncrement(&g_ptarD3D9Iso.presentFailures);
            InterlockedExchange(&g_ptarD3D9Iso.healthy,0);
            InterlockedExchange(&g_ptarD3D9Iso.active,0);
            PtDiagLogA("ISO_PRESENTER_PRESENT_FAIL hr=0x%08lX",(unsigned long)hr);
            break;
        }
    }

    InterlockedExchange(&g_ptarD3D9Iso.active,0);
    InterlockedExchange(&g_ptarD3D9Iso.healthy,0);
    PtDiagLogA("ISO_PRESENTER_THREAD_EXIT");
    return 0;
}

static void PtD3D9IsolatedPresenterStop()
{
    InterlockedExchange(&g_ptarD3D9Iso.stopRequested,1);
    if(g_ptarD3D9Iso.readyEvent)
        SetEvent(g_ptarD3D9Iso.readyEvent);

    if(g_ptarD3D9Iso.thread)
    {
        DWORD wr=WaitForSingleObject(g_ptarD3D9Iso.thread,3000);
        if(wr!=WAIT_OBJECT_0)
        {
            PtDiagLogA("ISO_PRESENTER_STOP_TIMEOUT wait=%lu",(unsigned long)wr);
            return;
        }
        CloseHandle(g_ptarD3D9Iso.thread);
        g_ptarD3D9Iso.thread=0;
    }

    PtD3D9IsoReleaseDisplayResources();

    for(int i=0;i<PTAR_D3D9_ISO_SLOT_COUNT;++i)
        PtD3D9IsoReleaseProducerSlot(&g_ptarD3D9Iso.slots[i]);

    if(g_ptarD3D9Iso.readyEvent){CloseHandle(g_ptarD3D9Iso.readyEvent);g_ptarD3D9Iso.readyEvent=0;}
    if(g_ptarD3D9Iso.initEvent){CloseHandle(g_ptarD3D9Iso.initEvent);g_ptarD3D9Iso.initEvent=0;}

    InterlockedExchange(&g_ptarD3D9Iso.active,0);
    InterlockedExchange(&g_ptarD3D9Iso.healthy,0);
    InterlockedExchange(&g_ptarD3D9Iso.stopRequested,0);
    InterlockedExchange(&g_ptarD3D9Iso.writeSequence,0);
    InterlockedExchange(&g_ptarD3D9Iso.mailboxDrops,0);
    InterlockedExchange(&g_ptarD3D9Iso.presentFailures,0);
    InterlockedExchange(&g_ptarD3D9Iso.flipExActive,0);
    g_ptarD3D9Iso.hwnd=0;
    g_ptarD3D9Iso.width=0;
    g_ptarD3D9Iso.height=0;
    g_ptarD3D9Iso.format=D3DFMT_UNKNOWN;
    g_ptarD3D9Iso.adapterOrdinal=0;
    g_ptarD3D9Iso.deviceType=D3DDEVTYPE_HAL;
}

static bool PtD3D9IsolatedPresenterStart(HWND hwnd)
{
    PtD3D9IsolatedPresenterStop();

    if(hwnd)
        g_ptarD3D9IsoPreferredWindow=hwnd;
    if(!g_ptarD3D9IsoPreferredWindow || !g_ptar.device || !g_sysDirect3DCreate9Ex)
    {
        PtDiagLogA("ISO_PRESENTER_START_UNAVAILABLE hwnd=%p dev=%p create9ex=%p",
            g_ptarD3D9IsoPreferredWindow,g_ptar.device,(void*)g_sysDirect3DCreate9Ex);
        return false;
    }

    D3DDEVICE_CREATION_PARAMETERS producerCp={};
    HRESULT producerCpHr=g_ptar.device->GetCreationParameters(&producerCp);
    if(FAILED(producerCpHr))
    {
        PtDiagLogA(
            "ISO_PRESENTER_SOURCE_CREATION_PARAMS_FAIL hr=0x%08lX",
            (unsigned long)producerCpHr);
        return false;
    }

    g_ptarD3D9Iso.hwnd=g_ptarD3D9IsoPreferredWindow;
    g_ptarD3D9Iso.width=g_ptar.outputW;
    g_ptarD3D9Iso.height=g_ptar.outputH;
    g_ptarD3D9Iso.format=g_ptar.outputFormat;
    g_ptarD3D9Iso.adapterOrdinal=producerCp.AdapterOrdinal;
    g_ptarD3D9Iso.deviceType=producerCp.DeviceType;

    PtDiagLogA(
        "ISO_PRESENTER_ACTIVATION_PREP adapter_source=%u type=%u caller_thread=%lu create_thread=%lu focus=%p",
        g_ptarD3D9Iso.adapterOrdinal,(unsigned)g_ptarD3D9Iso.deviceType,
        (unsigned long)GetCurrentThreadId(),
        (unsigned long)g_ptarD3D9IsoSourceCreationThreadId,
        producerCp.hFocusWindow);

    g_ptarD3D9Iso.readyEvent=CreateEventW(0,FALSE,FALSE,0);
    g_ptarD3D9Iso.initEvent=CreateEventW(0,TRUE,FALSE,0);
    if(!g_ptarD3D9Iso.readyEvent || !g_ptarD3D9Iso.initEvent)
    {
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    if(!PtD3D9IsoCreateProducerMailbox())
    {
        PtDiagLogA("ISO_PRESENTER_MAILBOX_CREATE_FAIL");
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    if(!PtD3D9IsoCreateDisplayResourcesOnCallerThread())
    {
        PtDiagLogA("ISO_PRESENTER_CALLER_THREAD_PREP_FAIL");
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    InterlockedExchange(&g_ptarD3D9Iso.healthy,1);
    InterlockedExchange(&g_ptarD3D9Iso.active,1);
    g_ptarD3D9Iso.thread=CreateThread(
        0,0,PtD3D9IsoPresenterThread,0,0,0);
    if(!g_ptarD3D9Iso.thread)
    {
        InterlockedExchange(&g_ptarD3D9Iso.active,0);
        InterlockedExchange(&g_ptarD3D9Iso.healthy,0);
        PtDiagLogA("ISO_PRESENTER_THREAD_CREATE_FAIL gle=%lu",(unsigned long)GetLastError());
        PtD3D9IsolatedPresenterStop();
        return false;
    }

    return true;
}

'''
h=h[:thread_start]+replacement+h[owns_start:]

# HookCreateDevice has the second `original=*pp` occurrence (HookReset is first).
orig='    const D3DPRESENT_PARAMETERS original=*pp;\n'
positions=[m.start() for m in re.finditer(re.escape(orig),p)]
if len(positions)!=2:
    raise SystemExit('expected exactly HookReset + HookCreateDevice original PP anchors')
second=positions[1]
insert='''    const D3DPRESENT_PARAMETERS original=*pp;\n    PtD3D9IsolatedPresenterRememberSourceAdapter(\n        self,adapter,type,\n        original.hDeviceWindow?original.hDeviceWindow:focus);\n'''
p=p[:second]+p[second:].replace(orig,insert,1)

# Remove eager presenter creation from Reset/CreateDevice. Initial preparation
# now happens only after a real CTRL+F6 activation request on a stable device.
old_reset='''    PtD3D9IsolatedPresenterStart(original.hDeviceWindow);\n    return S_OK;\n}'''
new_reset='''    if(original.hDeviceWindow)\n        g_ptarD3D9IsoPreferredWindow=original.hDeviceWindow;\n    return S_OK;\n}'''
if old_reset not in p:
    raise SystemExit('eager Reset start anchor missing')
p=p.replace(old_reset,new_reset,1)

old_create='''    PtD3D9IsolatedPresenterStart(\n        original.hDeviceWindow?original.hDeviceWindow:focus);\n\n    *pp=original;\n    return S_OK;'''
new_create='''    if(original.hDeviceWindow)\n        g_ptarD3D9IsoPreferredWindow=original.hDeviceWindow;\n    else if(focus)\n        g_ptarD3D9IsoPreferredWindow=focus;\n\n    *pp=original;\n    return S_OK;'''
if old_create not in p:
    raise SystemExit('eager CreateDevice start anchor missing')
p=p.replace(old_create,new_create,1)

anchor='''    g_ptar.inPresent=true;\n    PtHudFrameTick();\n\n    HRESULT result=S_OK;'''
repl='''    g_ptar.inPresent=true;\n    PtHudFrameTick();\n\n    // D3D11 lifetime port: first FG activation prepares the separate presenter\n    // after CreateDevice/Reset have fully returned. On D3D9 this also ensures\n    // CreateDeviceEx/open-shared-resource work runs on the source caller thread\n    // rather than the display worker.\n    if(PtHudFgEnabled() && !PtD3D9IsolatedPresenterOwnsDisplay())\n    {\n        HWND presenterHwnd=hwnd?hwnd:g_ptarD3D9IsoPreferredWindow;\n        if(!PtD3D9IsolatedPresenterStart(presenterHwnd))\n        {\n            g_ptarHudFgEnabled=false;\n            PtFgPacerReset();\n            PtHudFeedback(13,0,0,0);\n            PtDiagLogA(\n                "FG_ACTIVATION_PRESENTER_PREP_FAIL hwnd=%p fallback=REAL_ONLY",\n                presenterHwnd);\n        }\n        else\n        {\n            PtDiagLogA(\n                "FG_ACTIVATION_PRESENTER_PREP_OK hwnd=%p adapter=%u",\n                presenterHwnd,g_ptarD3D9Iso.adapterOrdinal);\n        }\n    }\n\n    HRESULT result=S_OK;'''
if anchor not in p:
    raise SystemExit('HookPresent activation insertion anchor missing')
p=p.replace(anchor,repl,1)

hp.write_text(h,encoding='utf-8',newline='\n')
pp.write_text(p,encoding='utf-8',newline='\n')

# Regression gates.
h=hp.read_text(encoding='utf-8')
p=pp.read_text(encoding='utf-8')
if 'PtD3D9IsoResolvePresenterAdapter' not in h:
    raise SystemExit('identity adapter resolver absent')
if 'ISO_PRESENTER_CALLER_THREAD_PREP_FAIL' not in h:
    raise SystemExit('caller-thread prep absent')
if 'WaitForSingleObject(g_ptarD3D9Iso.initEvent,5000)' in h:
    raise SystemExit('old worker-thread init wait remains')
thread=h[h.index('static DWORD WINAPI PtD3D9IsoPresenterThread'):h.index('static void PtD3D9IsolatedPresenterStop')]
if 'CreateDeviceEx' in thread or 'Direct3DCreate9Ex' in thread:
    raise SystemExit('D3D9 device creation still occurs on display worker')
if p.count('PtD3D9IsolatedPresenterStart(')!=1:
    raise SystemExit('presenter must start only from lazy FG activation path')
if 'PtD3D9IsolatedPresenterRememberSourceAdapter(' not in p:
    raise SystemExit('producer adapter identity capture missing')
if 'FG_ACTIVATION_PRESENTER_PREP_OK' not in p:
    raise SystemExit('lazy FG activation prep missing')

print('D3D9_FIELDHOTFIX9_ACTIVATION_IDENTITY_PORT=PASS')
print('PRESENTER_DEVICE_CREATION_THREAD=SOURCE_CALLER')
print('PRESENTER_PRESENT_THREAD=SEPARATE_MULTITHREADED_DEVICE')
print('PRESENTER_ADAPTER_MATCH=IDENTITY_THEN_ORDINAL')
print('INITIAL_PREP=FIRST_FG_ENABLED_PRESENT')
