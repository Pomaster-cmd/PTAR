from pathlib import Path
import hashlib, subprocess

hp=Path('compat/x86_d3d9/ptar_fg_pacer.h')
pp=Path('compat/x86_d3d9/ptar_d3d9_proxy.cpp')
if not hp.is_file() or not pp.is_file():
    raise SystemExit('HF6 source files missing')

# Guard the exact FIELDHOTFIX6 base.
expected_hf6_pacer='ec21a15f605da54054e495b3cf93055d09c8e7189d9cbc120bb19a536a2fce88'
got=hashlib.sha256(hp.read_bytes()).hexdigest()
if got != expected_hf6_pacer:
    raise SystemExit('HF6 pacer base mismatch: '+got)

# Reuse the exact already field-tested FIELDHOTFIX4 Sync1/no-software-wait pacer.
hf4_commit='b36e2be32678dcfb56b50cc4cc55d251d5c5f8b2'
hf4_pacer=subprocess.check_output(['git','show',hf4_commit+':compat/x86_d3d9/ptar_fg_pacer.h'])
if b'D3D11_PAIRBAL2_D3D9_SYNC1_PRESENT_ONLY' not in hf4_pacer:
    raise SystemExit('HF4 Sync1 pacer marker missing')
if b'PtFgPacerWaitUntil' in hf4_pacer or b'PtFgPacerWaitMemberSlots' in hf4_pacer:
    raise SystemExit('HF4 pacer unexpectedly contains software waits')
hp.write_bytes(hf4_pacer)

s=pp.read_text(encoding='utf-8')

# QF1 presentation queue: keep same working HF6 device and create two queued backs.
old='''    actual->EnableAutoDepthStencil=FALSE;\n}'''
new='''    actual->EnableAutoDepthStencil=FALSE;\n\n    // HF6 VISIBLE60 FASTBLIT2: preserve the proven same-device HF6 FG path.\n    // The implicit swapchain owns front + two queued backbuffers. Sync1 is\n    // the only cadence authority; no QPC member waits are introduced.\n    actual->BackBufferCount=2;\n    actual->SwapEffect=D3DSWAPEFFECT_DISCARD;\n    actual->PresentationInterval=D3DPRESENT_INTERVAL_ONE;\n}'''
if s.count(old)!=1:
    raise SystemExit('presentation parameter anchor mismatch')
s=s.replace(old,new,1)

# With DISCARD, backbuffer zero rotates after each Present. Always reacquire it.
anchor='''static HRESULT RenderTextureToBackBuffer(\n    IDirect3DDevice9* dev,\n    IDirect3DTexture9* texture)\n{'''
helper='''static HRESULT PtRefreshQueuedRealBackBuffer(IDirect3DDevice9* dev)\n{\n    if(!dev || !g_realGetBackBuffer)\n        return D3DERR_INVALIDCALL;\n\n    IDirect3DSurface9* current=0;\n    HRESULT hr=g_realGetBackBuffer(\n        dev,0,0,D3DBACKBUFFER_TYPE_MONO,&current);\n    if(FAILED(hr) || !current)\n    {\n        PtDiagLogA(\n            "VISIBLE60_BACKBUFFER_REFRESH_FAIL hr=0x%08lX",\n            (unsigned long)hr);\n        if(current) current->Release();\n        return FAILED(hr)?hr:E_FAIL;\n    }\n\n    if(g_ptar.realBackBuffer)\n        g_ptar.realBackBuffer->Release();\n    g_ptar.realBackBuffer=current;\n    return S_OK;\n}\n\nstatic HRESULT PtFastBlitSurfaceToBackBuffer(\n    IDirect3DDevice9* dev,\n    IDirect3DSurface9* source)\n{\n    if(!dev || !source || !g_ptar.realBackBuffer)\n        return D3DERR_INVALIDCALL;\n\n    D3DSURFACE_DESC srcDesc={};\n    D3DSURFACE_DESC dstDesc={};\n    HRESULT hr=source->GetDesc(&srcDesc);\n    if(FAILED(hr))\n        return hr;\n    hr=g_ptar.realBackBuffer->GetDesc(&dstDesc);\n    if(FAILED(hr))\n        return hr;\n\n    // FIELDHOTFIX6 creates currentReal/generated as same-format render targets\n    // at outputW/outputH and PTAR forces the device backbuffer non-MSAA. Only\n    // take the fast path for that exact copy-compatible case.\n    if(srcDesc.Width!=dstDesc.Width || srcDesc.Height!=dstDesc.Height ||\n       srcDesc.Format!=dstDesc.Format ||\n       srcDesc.MultiSampleType!=D3DMULTISAMPLE_NONE ||\n       dstDesc.MultiSampleType!=D3DMULTISAMPLE_NONE)\n        return D3DERR_INVALIDCALL;\n\n    // This replaces only the final bilinear fullscreen presentation copy.\n    // ME/refine/interpolation and REAL history remain byte-identical HF6.\n    return dev->StretchRect(\n        source,0,\n        g_ptar.realBackBuffer,0,\n        D3DTEXF_NONE);\n}\n\nstatic HRESULT RenderTextureToBackBuffer(\n    IDirect3DDevice9* dev,\n    IDirect3DTexture9* texture)\n{'''
if s.count(anchor)!=1:
    raise SystemExit('RenderTextureToBackBuffer anchor mismatch')
s=s.replace(anchor,helper,1)

# Extend only the final presentation helper with the already-existing surface.
sig='''static HRESULT PresentPTARTexture(\n    IDirect3DDevice9* dev,\n    IDirect3DTexture9* texture,\n    HWND hwnd,'''
sig2='''static HRESULT PresentPTARTexture(\n    IDirect3DDevice9* dev,\n    IDirect3DTexture9* texture,\n    IDirect3DSurface9* surface,\n    HWND hwnd,'''
if s.count(sig)!=1:
    raise SystemExit('PresentPTARTexture signature anchor mismatch')
s=s.replace(sig,sig2,1)

old_body='''{\n    HRESULT hr=RenderTextureToBackBuffer(dev,texture);\n    if(FAILED(hr))\n        return hr;\n'''
new_body='''{\n    // QF1/QF2: DISCARD rotates buffer zero. Reacquire before every G/R.\n    HRESULT hr=PtRefreshQueuedRealBackBuffer(dev);\n    if(FAILED(hr))\n        return hr;\n\n    // FASTBLIT2: remove the redundant fullscreen bilinear shader pass from\n    // the hot presentation path. StretchRect is a same-device render-target\n    // copy and therefore does not change generated pixels. Fail open to the\n    // exact HF6 shader copy if the driver rejects the fast path.\n    hr=PtFastBlitSurfaceToBackBuffer(dev,surface);\n    if(FAILED(hr))\n    {\n        PtDiagLogA(\n            "VISIBLE60_FASTBLIT_FALLBACK hr=0x%08lX frame=%s",\n            (unsigned long)hr,generatedFrame?"GENERATED":"REAL");\n        hr=RenderTextureToBackBuffer(dev,texture);\n    }\n    if(FAILED(hr))\n        return hr;\n'''
if s.count(old_body)!=1:
    raise SystemExit('PresentPTARTexture body anchor mismatch')
s=s.replace(old_body,new_body,1)

# Pass the already existing exact surfaces. No FG algorithm/order change.
old_g='''PresentPTARTexture(\n                        self,g_ptar.generatedTexture,hwnd,dirty,true,true)'''
new_g='''PresentPTARTexture(\n                        self,g_ptar.generatedTexture,g_ptar.generatedSurface,\n                        hwnd,dirty,true,true)'''
if s.count(old_g)!=1:
    raise SystemExit('generated Present call anchor mismatch')
s=s.replace(old_g,new_g,1)

old_r='''PresentPTARTexture(\n            self,g_ptar.currentRealTexture,hwnd,dirty,false,fgProducing)'''
new_r='''PresentPTARTexture(\n            self,g_ptar.currentRealTexture,g_ptar.currentRealSurface,\n            hwnd,dirty,false,fgProducing)'''
if s.count(old_r)!=1:
    raise SystemExit('real Present call anchor mismatch')
s=s.replace(old_r,new_r,1)

# Hard gates: absolutely no HF7+ topology may leak back in.
for bad in (
    'ptar_d3d9_isolated_presenter.h',
    'CreateAdditionalSwapChain',
    'IDirect3DDevice9Ex* g_ptarD3D9IsoDisplayDevice',
    'PtD3D9IsolatedPresenterStart(',
    'D3DCREATE_MULTITHREADED',
):
    if bad in s:
        raise SystemExit('unexpected post-HF6 topology token: '+bad)

pp.write_text(s,encoding='utf-8',newline='\n')

print('HF6_VISIBLE60_FASTBLIT2=PASS')
print('BASE=FIELDHOTFIX6_WORKING_SAME_DEVICE_FG')
print('FG_ME_REFINE_INTERPOLATE=UNCHANGED')
print('DISPLAY_ORDER=GENERATED_THEN_REAL_UNCHANGED')
print('PACER=EXACT_FIELDHOTFIX4_SYNC1_NO_SOFTWARE_WAIT')
print('BACKBUFFER_COUNT=2')
print('SWAP_EFFECT=DISCARD')
print('PRESENT_INTERVAL=ONE')
print('BACKBUFFER_REACQUIRE=BEFORE_EACH_G_R')
print('PRESENT_COPY=STRETCHRECT_FASTPATH_WITH_EXACT_HF6_SHADER_FALLBACK')
print('SECOND_DEVICE=NONE')
print('ASYNC_WORKER=NONE')
print('ADDITIONAL_SWAPCHAIN=NONE')
