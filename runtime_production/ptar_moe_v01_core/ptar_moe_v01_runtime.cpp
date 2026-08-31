#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "ptar_moe_v01_runtime.h"
#include "ptar_moe_v01_embedded_dxbc.h"

static void PTARZeroRuntime(PTARMoeRuntime* r)
{
    if(!r) return;
    r->vs=0;
    r->ps=0;
    r->constants=0;
    r->linearClamp=0;
    r->ready=0u;
}

void PTARMoeRelease(PTARMoeRuntime* r)
{
    if(!r) return;
    if(r->linearClamp) { r->linearClamp->Release(); r->linearClamp=0; }
    if(r->constants)   { r->constants->Release();   r->constants=0; }
    if(r->ps)          { r->ps->Release();          r->ps=0; }
    if(r->vs)          { r->vs->Release();          r->vs=0; }
    r->ready=0u;
}

HRESULT PTARMoeInitialize(ID3D11Device* device, PTARMoeRuntime* r)
{
    if(!device || !r) return E_INVALIDARG;
    PTARZeroRuntime(r);

    HRESULT hr=device->CreateVertexShader(
        g_ptarMoeVsDxbc,g_ptarMoeVsDxbcSize,0,&r->vs);
    if(FAILED(hr)) { PTARMoeRelease(r); return hr; }

    hr=device->CreatePixelShader(
        g_ptarMoePsDxbc,g_ptarMoePsDxbcSize,0,&r->ps);
    if(FAILED(hr)) { PTARMoeRelease(r); return hr; }

    D3D11_BUFFER_DESC bd={};
    bd.ByteWidth=(UINT)sizeof(PTARMoeConstants);
    bd.Usage=D3D11_USAGE_DYNAMIC;
    bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    hr=device->CreateBuffer(&bd,0,&r->constants);
    if(FAILED(hr)) { PTARMoeRelease(r); return hr; }

    D3D11_SAMPLER_DESC sd={};
    sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD=D3D11_FLOAT32_MAX;
    hr=device->CreateSamplerState(&sd,&r->linearClamp);
    if(FAILED(hr)) { PTARMoeRelease(r); return hr; }

    r->ready=1u;
    return S_OK;
}

bool PTARMoeExactScale15(UINT srcW,UINT srcH,UINT dstW,UINT dstH)
{
    if(!srcW || !srcH || !dstW || !dstH) return false;
    /* Integer identity avoids float comparison:
       dst/src == 3/2 in both axes. */
    return ((unsigned long long)dstW*2ull==(unsigned long long)srcW*3ull) &&
           ((unsigned long long)dstH*2ull==(unsigned long long)srcH*3ull);
}

HRESULT PTARMoeRender15(
    ID3D11DeviceContext* context,
    PTARMoeRuntime* r,
    ID3D11ShaderResourceView* sourceSrv,
    ID3D11RenderTargetView* outputRtv,
    UINT srcW,UINT srcH,
    UINT dstW,UINT dstH)
{
    if(!context || !r || !sourceSrv || !outputRtv) return E_INVALIDARG;
    if(!r->ready || !r->vs || !r->ps || !r->constants || !r->linearClamp)
        return E_UNEXPECTED;

    if(!PTARMoeExactScale15(srcW,srcH,dstW,dstH))
        return S_FALSE;

    D3D11_MAPPED_SUBRESOURCE mapped={};
    HRESULT hr=context->Map(r->constants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
    if(FAILED(hr)) return hr;

    PTARMoeConstants* c=(PTARMoeConstants*)mapped.pData;
    c->inputSize[0]=(float)srcW;
    c->inputSize[1]=(float)srcH;
    c->outputSize[0]=(float)dstW;
    c->outputSize[1]=(float)dstH;
    context->Unmap(r->constants,0);

    D3D11_VIEWPORT vp={};
    vp.TopLeftX=0.0f;
    vp.TopLeftY=0.0f;
    vp.Width=(float)dstW;
    vp.Height=(float)dstH;
    vp.MinDepth=0.0f;
    vp.MaxDepth=1.0f;

    context->OMSetRenderTargets(1,&outputRtv,0);
    context->RSSetViewports(1,&vp);
    context->IASetInputLayout(0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(r->vs,0,0);
    context->PSSetShader(r->ps,0,0);
    context->PSSetShaderResources(0,1,&sourceSrv);
    context->PSSetSamplers(0,1,&r->linearClamp);
    context->PSSetConstantBuffers(0,1,&r->constants);
    context->Draw(3,0);

    /* Do not leave source bound when caller may use it as an RTV later. */
    ID3D11ShaderResourceView* nullSrv=0;
    context->PSSetShaderResources(0,1,&nullSrv);

    return S_OK;
}
