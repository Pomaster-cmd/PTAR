#pragma once

// PTAR R8 TSR1 runtime state. Implementations live in ptar_tsr1_runtime.inl,
// included by the proxy only after its fullscreen-pass helpers are defined.

struct PTARTSR1State
{
    IDirect3DPixelShader9* coarseShader;
    IDirect3DPixelShader9* refineShader;
    IDirect3DPixelShader9* subpixelShader;
    IDirect3DPixelShader9* resolveShader;

    IDirect3DTexture9* sourceHistoryTexture;
    IDirect3DSurface9* sourceHistorySurface;
    IDirect3DTexture9* coarseTexture;
    IDirect3DSurface9* coarseSurface;
    IDirect3DTexture9* fineTexture;
    IDirect3DSurface9* fineSurface;
    IDirect3DTexture9* subpixelTexture;
    IDirect3DSurface9* subpixelSurface;

    UINT sourceW;
    UINT sourceH;
    UINT outputW;
    UINT outputH;
    UINT coarseW;
    UINT coarseH;
    UINT fineW;
    UINT fineH;
    D3DFORMAT format;
    bool initialized;
    bool historyValid;
};

static PTARTSR1State g_ptarTsr1={};

static void PtTsr1Release();
static void PtTsr1InvalidateHistory();
static HRESULT PtTsr1Initialize(
    IDirect3DDevice9* dev,
    UINT sourceW,UINT sourceH,
    UINT outputW,UINT outputH,
    D3DFORMAT format);
static bool PtTsr1CanResolve();
static HRESULT PtTsr1Resolve(
    IDirect3DDevice9* dev,
    IDirect3DTexture9* currentSource,
    IDirect3DSurface9* resolvedTarget);
static HRESULT PtTsr1StoreSource(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* currentSourceSurface,
    IDirect3DTexture9* currentSourceTexture,
    IDirect3DPixelShader9* copyShader);