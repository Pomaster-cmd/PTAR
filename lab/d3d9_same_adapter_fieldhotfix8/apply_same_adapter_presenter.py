from pathlib import Path

p=Path('compat/x86_d3d9/ptar_d3d9_isolated_presenter.h')
s=p.read_text(encoding='utf-8')

# FIELDHOTFIX8: preserve the existing D3D11 isolated-presenter topology, but
# bind the D3D9Ex display device to the exact adapter/device type used by the
# game's producer device. This is essential on Optimus/hybrid systems where
# D3DADAPTER_DEFAULT can resolve to the Intel display adapter while the game
# producer and its shared resources live on NVIDIA.

old='''    UINT width;\n    UINT height;\n    D3DFORMAT format;\n    volatile LONG stopRequested;'''
new='''    UINT width;\n    UINT height;\n    D3DFORMAT format;\n    UINT adapterOrdinal;\n    D3DDEVTYPE deviceType;\n    volatile LONG stopRequested;'''
if old not in s:
    raise SystemExit('presenter struct anchor missing')
s=s.replace(old,new,1)

# Presenter must use the producer adapter, never D3DADAPTER_DEFAULT.
s=s.replace('hr=d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&dm);',
            'hr=d3d->GetAdapterDisplayMode(g_ptarD3D9Iso.adapterOrdinal,&dm);',1)
old_create='''    hr=d3d->CreateDeviceEx(\n        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,\n        g_ptarD3D9Iso.hwnd,behavior,&pp,0,&dev);'''
new_create='''    hr=d3d->CreateDeviceEx(\n        g_ptarD3D9Iso.adapterOrdinal,g_ptarD3D9Iso.deviceType,\n        g_ptarD3D9Iso.hwnd,behavior,&pp,0,&dev);'''
if old_create not in s:
    raise SystemExit('primary CreateDeviceEx anchor missing')
s=s.replace(old_create,new_create,1)
old_retry='''        hr=d3d->CreateDeviceEx(\n            D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,\n            g_ptarD3D9Iso.hwnd,behavior,&pp,0,&dev);'''
new_retry='''        hr=d3d->CreateDeviceEx(\n            g_ptarD3D9Iso.adapterOrdinal,g_ptarD3D9Iso.deviceType,\n            g_ptarD3D9Iso.hwnd,behavior,&pp,0,&dev);'''
if old_retry not in s:
    raise SystemExit('retry CreateDeviceEx anchor missing')
s=s.replace(old_retry,new_retry,1)

old_ready='''    PtDiagLogA(\n        "ISO_PRESENTER_READY api=D3D9EX mode=%s interval=ONE size=%ux%u mailbox=%d order=GENERATED_THEN_MATCHING_REAL",\n        g_ptarD3D9Iso.flipExActive?"FLIPEX":"DISCARD",\n        g_ptarD3D9Iso.width,g_ptarD3D9Iso.height,\n        PTAR_D3D9_ISO_SLOT_COUNT);'''
new_ready='''    PtDiagLogA(\n        "ISO_PRESENTER_READY api=D3D9EX mode=%s interval=ONE size=%ux%u mailbox=%d order=GENERATED_THEN_MATCHING_REAL adapter=%u type=%u",\n        g_ptarD3D9Iso.flipExActive?"FLIPEX":"DISCARD",\n        g_ptarD3D9Iso.width,g_ptarD3D9Iso.height,\n        PTAR_D3D9_ISO_SLOT_COUNT,\n        g_ptarD3D9Iso.adapterOrdinal,(unsigned)g_ptarD3D9Iso.deviceType);'''
if old_ready not in s:
    raise SystemExit('ready log anchor missing')
s=s.replace(old_ready,new_ready,1)

# Capture the producer's actual creation parameters before any shared resource
# or presenter device is created.
anchor='''    g_ptarD3D9Iso.hwnd=g_ptarD3D9IsoPreferredWindow;\n    g_ptarD3D9Iso.width=g_ptar.outputW;\n    g_ptarD3D9Iso.height=g_ptar.outputH;\n    g_ptarD3D9Iso.format=g_ptar.outputFormat;'''
repl='''    D3DDEVICE_CREATION_PARAMETERS producerCp={};\n    HRESULT producerCpHr=g_ptar.device->GetCreationParameters(&producerCp);\n    if(FAILED(producerCpHr))\n    {\n        PtDiagLogA(\n            "ISO_PRESENTER_SOURCE_CREATION_PARAMS_FAIL hr=0x%08lX",\n            (unsigned long)producerCpHr);\n        return false;\n    }\n\n    g_ptarD3D9Iso.hwnd=g_ptarD3D9IsoPreferredWindow;\n    g_ptarD3D9Iso.width=g_ptar.outputW;\n    g_ptarD3D9Iso.height=g_ptar.outputH;\n    g_ptarD3D9Iso.format=g_ptar.outputFormat;\n    g_ptarD3D9Iso.adapterOrdinal=producerCp.AdapterOrdinal;\n    g_ptarD3D9Iso.deviceType=producerCp.DeviceType;\n\n    PtDiagLogA(\n        "ISO_PRESENTER_BIND_SOURCE_ADAPTER adapter=%u type=%u focus=%p behavior=0x%08lX",\n        g_ptarD3D9Iso.adapterOrdinal,(unsigned)g_ptarD3D9Iso.deviceType,\n        producerCp.hFocusWindow,(unsigned long)producerCp.BehaviorFlags);'''
if anchor not in s:
    raise SystemExit('presenter start geometry anchor missing')
s=s.replace(anchor,repl,1)

old_reset='''    g_ptarD3D9Iso.hwnd=0;\n    g_ptarD3D9Iso.width=0;\n    g_ptarD3D9Iso.height=0;\n    g_ptarD3D9Iso.format=D3DFMT_UNKNOWN;'''
new_reset='''    g_ptarD3D9Iso.hwnd=0;\n    g_ptarD3D9Iso.width=0;\n    g_ptarD3D9Iso.height=0;\n    g_ptarD3D9Iso.format=D3DFMT_UNKNOWN;\n    g_ptarD3D9Iso.adapterOrdinal=0;\n    g_ptarD3D9Iso.deviceType=D3DDEVTYPE_HAL;'''
if old_reset not in s:
    raise SystemExit('presenter reset anchor missing')
s=s.replace(old_reset,new_reset,1)

p.write_text(s,encoding='utf-8',newline='\n')

# Static regression gates.
t=p.read_text(encoding='utf-8')
thread=t[t.index('static DWORD WINAPI PtD3D9IsoPresenterThread'):t.index('static void PtD3D9IsolatedPresenterStop')]
if 'D3DADAPTER_DEFAULT' in thread:
    raise SystemExit('presenter thread still hardcodes D3DADAPTER_DEFAULT')
for needle in (
    'GetCreationParameters(&producerCp)',
    'g_ptarD3D9Iso.adapterOrdinal=producerCp.AdapterOrdinal',
    'g_ptarD3D9Iso.deviceType=producerCp.DeviceType',
    'GetAdapterDisplayMode(g_ptarD3D9Iso.adapterOrdinal',
    'g_ptarD3D9Iso.adapterOrdinal,g_ptarD3D9Iso.deviceType'):
    if needle not in t:
        raise SystemExit('same-adapter gate missing: '+needle)

print('D3D9_ISOLATED_PRESENTER_SAME_ADAPTER=PASS')
print('HYBRID_GPU_DEFAULT_ADAPTER_HAZARD=REMOVED')
print('PRESENTER_ADAPTER=PRODUCER_ADAPTER_ORDINAL')
