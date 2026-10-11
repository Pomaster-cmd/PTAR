from pathlib import Path
import base64, gzip, hashlib

hp=Path("compat/x86_d3d9/ptar_fg_pacer.h")
pp=Path("compat/x86_d3d9/ptar_d3d9_proxy.cpp")

if not hp.is_file() or not pp.is_file():
    raise SystemExit("HF6 source files missing")

expected_hf6_pacer="ec21a15f605da54054e495b3cf93055d09c8e7189d9cbc120bb19a536a2fce88"
got=hashlib.sha256(hp.read_bytes()).hexdigest()
if got != expected_hf6_pacer:
    raise SystemExit("HF6 pacer base mismatch: "+got)

hf4_pacer=gzip.decompress(base64.b64decode("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"))
if hashlib.sha256(hf4_pacer).hexdigest()!="9cbc2098f5737698e38c4003664366dce2ce7965b4d517b6cffd0e2a240e86d4":
    raise SystemExit("embedded HF4 pacer corrupt")
hp.write_bytes(hf4_pacer)

s=pp.read_text(encoding="utf-8")

old='''    actual->EnableAutoDepthStencil=FALSE;
}'''
new='''    actual->EnableAutoDepthStencil=FALSE;

    // HF6_VISIBLE60_QUEUEFIX1: preserve the proven HF6 same-device FG path,
    // but give GENERATED and REAL distinct queued D3D9 backbuffers.
    // D3D9 BackBufferCount excludes the front buffer, so 2 = triple buffering.
    // Sync1 is the only cadence authority; no QPC member waits are added.
    actual->BackBufferCount=2;
    actual->SwapEffect=D3DSWAPEFFECT_DISCARD;
    actual->PresentationInterval=D3DPRESENT_INTERVAL_ONE;
}'''
if s.count(old)!=1:
    raise SystemExit("presentation parameter anchor mismatch")
s=s.replace(old,new,1)

anchor='''static HRESULT RenderTextureToBackBuffer(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* texture)
{'''
helper='''static HRESULT PtRefreshQueuedRealBackBuffer(IDirect3DDevice9* dev)
{
    if(!dev || !g_realGetBackBuffer)
        return D3DERR_INVALIDCALL;

    IDirect3DSurface9* current=0;
    HRESULT hr=g_realGetBackBuffer(
        dev,0,0,D3DBACKBUFFER_TYPE_MONO,&current);
    if(FAILED(hr) || !current)
    {
        PtDiagLogA(
            "VISIBLE60_BACKBUFFER_REFRESH_FAIL hr=0x%08lX",
            (unsigned long)hr);
        if(current) current->Release();
        return FAILED(hr)?hr:E_FAIL;
    }

    if(g_ptar.realBackBuffer)
        g_ptar.realBackBuffer->Release();
    g_ptar.realBackBuffer=current;
    return S_OK;
}

static HRESULT RenderTextureToBackBuffer(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* texture)
{'''
if s.count(anchor)!=1:
    raise SystemExit("RenderTextureToBackBuffer anchor mismatch")
s=s.replace(anchor,helper,1)

old_present='''{
    HRESULT hr=RenderTextureToBackBuffer(dev,texture);
    if(FAILED(hr))
        return hr;
'''
new_present='''{
    // With DISCARD + two backbuffers, GetBackBuffer(0) changes after Present.
    // Reacquire the current physical backbuffer before EACH G/R submission so
    // the two frames cannot be rendered into the same stale surface.
    HRESULT refreshHr=PtRefreshQueuedRealBackBuffer(dev);
    if(FAILED(refreshHr))
        return refreshHr;

    HRESULT hr=RenderTextureToBackBuffer(dev,texture);
    if(FAILED(hr))
        return hr;
'''
if s.count(old_present)!=1:
    raise SystemExit("PresentPTARTexture anchor mismatch")
s=s.replace(old_present,new_present,1)

for bad in (
    "ptar_d3d9_isolated_presenter.h",
    "CreateAdditionalSwapChain",
    "IDirect3DDevice9Ex* g_ptarD3D9IsoDisplayDevice",
    "PtD3D9IsolatedPresenterStart(",
):
    if bad in s:
        raise SystemExit("unexpected post-HF6 topology token: "+bad)

pp.write_text(s,encoding="utf-8",newline="\n")

print("HF6_VISIBLE60_QUEUEFIX1=PASS")
print("BASE=FIELDHOTFIX6_WORKING_SAME_DEVICE_FG")
print("FG_CORE_CHANGE=NONE")
print("PACER=EXISTING_FIELDHOTFIX4_SYNC1_NO_SOFTWARE_WAIT")
print("D3D9_BACKBUFFER_COUNT=2_FRONT_PLUS_TWO_BACKS")
print("D3D9_SWAP_EFFECT=DISCARD")
print("D3D9_PRESENT_INTERVAL=ONE")
print("BACKBUFFER_REFRESH=BEFORE_EACH_GENERATED_AND_REAL_PRESENT")
print("SECOND_DEVICE=NONE")
print("ASYNC_WORKER=NONE")
