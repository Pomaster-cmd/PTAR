from pathlib import Path
import hashlib, subprocess

hp=Path('compat/x86_d3d9/ptar_fg_pacer.h')
pp=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp')
if not hp.is_file() or not pp.is_file():
    raise SystemExit('HF6 source files missing')

expected_hf6_pacer='ec21a15f605da54054e495b3cf93055d09c8e7189d9cbc120bb19a536a2fce88'
got=hashlib.sha256(hp.read_bytes()).hexdigest()
if got != expected_hf6_pacer:
    raise SystemExit('HF6 pacer base mismatch: '+got)

# Reuse the exact already-field-tested FIELDHOTFIX4 Sync1/no-software-wait pacer.
# Pin the commit, never the moving branch.
hf4_commit='b36e2be32678dcfb56b50cc4cc55d251d5c5f8b2'
hf4_path='compat/x86_d3d9/ptar_fg_pacer.h'
hf4_pacer=subprocess.check_output(['git','show',hf4_commit+':'+hf4_path])
if b'D3D11_PAIRBAL2_D3D9_SYNC1_PRESENT_ONLY' not in hf4_pacer:
    raise SystemExit('pinned HF4 pacer semantic marker missing')
if b'PtFgPacerWaitUntil' in hf4_pacer or b'PtFgPacerWaitMemberSlots' in hf4_pacer:
    raise SystemExit('pinned HF4 pacer unexpectedly contains software waits')
hp.write_bytes(hf4_pacer)
print('HF4_PACER_SHA256='+hashlib.sha256(hf4_pacer).hexdigest())

s=pp.read_text(encoding='utf-8')
old='''    actual->EnableAutoDepthStencil=FALSE;\n}'''
new='''    actual->EnableAutoDepthStencil=FALSE;\n\n    // HF6_VISIBLE60_QUEUEFIX1: preserve the proven HF6 same-device FG path,\n    // but give GENERATED and REAL distinct queued D3D9 backbuffers.\n    // D3D9 BackBufferCount excludes the front buffer, so 2 = triple buffering.\n    // Sync1 is the only cadence authority; no QPC member waits are added.\n    actual->BackBufferCount=2;\n    actual->SwapEffect=D3DSWAPEFFECT_DISCARD;\n    actual->PresentationInterval=D3DPRESENT_INTERVAL_ONE;\n}'''
if s.count(old)!=1:
    raise SystemExit('presentation parameter anchor mismatch')
s=s.replace(old,new,1)

anchor='''static HRESULT RenderTextureToBackBuffer(\n    IDirect3DDevice9* dev,\n    IDirect3DTexture9* texture)\n{'''
helper='''static HRESULT PtRefreshQueuedRealBackBuffer(IDirect3DDevice9* dev)\n{\n    if(!dev || !g_realGetBackBuffer)\n        return D3DERR_INVALIDCALL;\n\n    IDirect3DSurface9* current=0;\n    HRESULT hr=g_realGetBackBuffer(\n        dev,0,0,D3DBACKBUFFER_TYPE_MONO,&current);\n    if(FAILED(hr) || !current)\n    {\n        PtDiagLogA(\n            "VISIBLE60_BACKBUFFER_REFRESH_FAIL hr=0x%08lX",\n            (unsigned long)hr);\n        if(current) current->Release();\n        return FAILED(hr)?hr:E_FAIL;\n    }\n\n    if(g_ptar.realBackBuffer)\n        g_ptar.realBackBuffer->Release();\n    g_ptar.realBackBuffer=current;\n    return S_OK;\n}\n\nstatic HRESULT RenderTextureToBackBuffer(\n    IDirect3DDevice9* dev,\n    IDirect3DTexture9* texture)\n{'''
if s.count(anchor)!=1:
    raise SystemExit('RenderTextureToBackBuffer anchor mismatch')
s=s.replace(anchor,helper,1)

old_present='''{\n    HRESULT hr=RenderTextureToBackBuffer(dev,texture);\n    if(FAILED(hr))\n        return hr;\n'''
new_present='''{\n    // DISCARD rotates buffer 0 after each successful Present. Reacquire it\n    // before every GENERATED and REAL submission; otherwise G/R can target\n    // one stale physical surface and collapse into one actually scanned frame.\n    HRESULT refreshHr=PtRefreshQueuedRealBackBuffer(dev);\n    if(FAILED(refreshHr))\n        return refreshHr;\n\n    HRESULT hr=RenderTextureToBackBuffer(dev,texture);\n    if(FAILED(hr))\n        return hr;\n'''
if s.count(old_present)!=1:
    raise SystemExit('PresentPTARTexture anchor mismatch')
s=s.replace(old_present,new_present,1)

for bad in ('ptar_d3d9_isolated_presenter.h','CreateAdditionalSwapChain','IDirect3DDevice9Ex* g_ptarD3D9IsoDisplayDevice','PtD3D9IsolatedPresenterStart('):
    if bad in s:
        raise SystemExit('unexpected post-HF6 topology token: '+bad)
pp.write_text(s,encoding='utf-8',newline='\n')

print('HF6_VISIBLE60_QUEUEFIX1=PASS')
print('BASE=FIELDHOTFIX6_WORKING_SAME_DEVICE_FG')
print('FG_CORE_CHANGE=NONE')
print('PACER=EXACT_PINNED_FIELDHOTFIX4_SYNC1_NO_SOFTWARE_WAIT')
print('D3D9_BACKBUFFER_COUNT=2_FRONT_PLUS_TWO_BACKS')
print('D3D9_SWAP_EFFECT=DISCARD')
print('D3D9_PRESENT_INTERVAL=ONE')
print('BACKBUFFER_REFRESH=BEFORE_EACH_GENERATED_AND_REAL_PRESENT')
print('SECOND_DEVICE=NONE')
print('ASYNC_WORKER=NONE')
