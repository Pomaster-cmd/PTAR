#pragma once
#ifndef PTAR_MOE_V01_RUNTIME_H
#define PTAR_MOE_V01_RUNTIME_H

#include <d3d11.h>

enum PTARMoeMode
{
    PTAR_MOE_MODE_POINT = 0,
    PTAR_MOE_MODE_BILINEAR = 1,
    PTAR_MOE_MODE_PTAR = 2
};

struct PTARMoeRuntime
{
    ID3D11VertexShader* vs;
    ID3D11PixelShader* ps;
    ID3D11Buffer* constants;
    ID3D11SamplerState* linearClamp;
    UINT ready;
};

struct PTARMoeConstants
{
    float inputSize[2];
    float outputSize[2];
};

HRESULT PTARMoeInitialize(ID3D11Device* device, PTARMoeRuntime* runtime);
void PTARMoeRelease(PTARMoeRuntime* runtime);

bool PTARMoeExactScale15(UINT srcW, UINT srcH, UINT dstW, UINT dstH);

/* Renders PTAR only for exact x1.5 geometry.
 * Returns S_FALSE when geometry is outside PTAR's validated contract.
 * Caller must then use its existing POINT/BILINEAR/direct path.
 *
 * The caller owns presenter state ordering. This function intentionally does
 * not save/restore the complete D3D11 pipeline because the production
 * presenter already establishes its own scaling stage followed by HUD.
 */
HRESULT PTARMoeRender15(
    ID3D11DeviceContext* context,
    PTARMoeRuntime* runtime,
    ID3D11ShaderResourceView* sourceSrv,
    ID3D11RenderTargetView* outputRtv,
    UINT srcW, UINT srcH,
    UINT dstW, UINT dstH);

#endif
