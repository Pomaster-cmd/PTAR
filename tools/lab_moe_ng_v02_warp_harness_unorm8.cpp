#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static bool ReadAll(const char* path, std::vector<std::uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff n = f.tellg();
    if (n < 0) return false;
    out.resize(static_cast<std::size_t>(n));
    f.seekg(0, std::ios::beg);
    if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
    return !!f;
}

static bool WriteAll(const char* path, const void* data, std::size_t bytes)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    if (bytes) f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    return !!f;
}

static bool Check(HRESULT hr, const char* what)
{
    if (SUCCEEDED(hr)) return true;
    std::cerr << what << " failed, HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
    return false;
}

struct alignas(16) Constants
{
    float inputSize[2];
    float outputSize[2];
};

int main(int argc, char** argv)
{
    if (argc != 9)
    {
        std::cerr << "usage: warp_harness_unorm8 input.f32 inW inH outW outH pixel.cso vertex.cso output.rgba8\n";
        return 2;
    }

    const char* inputPath = argv[1];
    const UINT inW = static_cast<UINT>(std::stoul(argv[2]));
    const UINT inH = static_cast<UINT>(std::stoul(argv[3]));
    const UINT outW = static_cast<UINT>(std::stoul(argv[4]));
    const UINT outH = static_cast<UINT>(std::stoul(argv[5]));
    const char* psPath = argv[6];
    const char* vsPath = argv[7];
    const char* outputPath = argv[8];

    std::vector<std::uint8_t> inputBytes, psBytes, vsBytes;
    if (!ReadAll(inputPath, inputBytes) || !ReadAll(psPath, psBytes) || !ReadAll(vsPath, vsBytes))
    {
        std::cerr << "failed to read input or shader bytecode\n";
        return 3;
    }
    const std::size_t expectedInput = static_cast<std::size_t>(inW) * inH * 4u * sizeof(float);
    if (inputBytes.size() != expectedInput)
    {
        std::cerr << "input byte size mismatch\n";
        return 4;
    }

    D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL actual = D3D_FEATURE_LEVEL_9_1;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    HRESULT hr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,requested,1,D3D11_SDK_VERSION,&dev,&actual,&ctx);
    if (!Check(hr, "D3D11CreateDevice(WARP)")) return 5;
    if (actual < D3D_FEATURE_LEVEL_11_0) return 6;

    D3D11_TEXTURE2D_DESC inDesc{};
    inDesc.Width=inW; inDesc.Height=inH; inDesc.MipLevels=1; inDesc.ArraySize=1;
    inDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; inDesc.SampleDesc.Count=1;
    inDesc.Usage=D3D11_USAGE_DEFAULT; inDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{}; init.pSysMem=inputBytes.data(); init.SysMemPitch=inW*4u*sizeof(float);
    ComPtr<ID3D11Texture2D> inputTex;
    if (!Check(dev->CreateTexture2D(&inDesc,&init,&inputTex),"CreateTexture2D(input)")) return 7;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (!Check(dev->CreateShaderResourceView(inputTex.Get(),nullptr,&srv),"CreateShaderResourceView")) return 8;

    D3D11_TEXTURE2D_DESC outDesc{};
    outDesc.Width=outW; outDesc.Height=outH; outDesc.MipLevels=1; outDesc.ArraySize=1;
    outDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; outDesc.SampleDesc.Count=1;
    outDesc.Usage=D3D11_USAGE_DEFAULT; outDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> outputTex;
    if (!Check(dev->CreateTexture2D(&outDesc,nullptr,&outputTex),"CreateTexture2D(output)")) return 9;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (!Check(dev->CreateRenderTargetView(outputTex.Get(),nullptr,&rtv),"CreateRenderTargetView")) return 10;

    D3D11_TEXTURE2D_DESC stageDesc=outDesc;
    stageDesc.Usage=D3D11_USAGE_STAGING; stageDesc.BindFlags=0; stageDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (!Check(dev->CreateTexture2D(&stageDesc,nullptr,&staging),"CreateTexture2D(staging)")) return 11;

    D3D11_SAMPLER_DESC samp{};
    samp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU=samp.AddressV=samp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.MinLOD=0.0f; samp.MaxLOD=D3D11_FLOAT32_MAX; samp.MaxAnisotropy=1; samp.ComparisonFunc=D3D11_COMPARISON_NEVER;
    ComPtr<ID3D11SamplerState> sampler;
    if (!Check(dev->CreateSamplerState(&samp,&sampler),"CreateSamplerState")) return 12;

    Constants constants{};
    constants.inputSize[0]=static_cast<float>(inW); constants.inputSize[1]=static_cast<float>(inH);
    constants.outputSize[0]=static_cast<float>(outW); constants.outputSize[1]=static_cast<float>(outH);
    D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth=sizeof(Constants); cbd.Usage=D3D11_USAGE_DEFAULT; cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbInit{}; cbInit.pSysMem=&constants;
    ComPtr<ID3D11Buffer> cb;
    if (!Check(dev->CreateBuffer(&cbd,&cbInit,&cb),"CreateBuffer(constants)")) return 13;

    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    if (!Check(dev->CreateVertexShader(vsBytes.data(),vsBytes.size(),nullptr,&vs),"CreateVertexShader")) return 14;
    if (!Check(dev->CreatePixelShader(psBytes.data(),psBytes.size(),nullptr,&ps),"CreatePixelShader")) return 15;
    D3D11_RASTERIZER_DESC rsd{}; rsd.FillMode=D3D11_FILL_SOLID; rsd.CullMode=D3D11_CULL_NONE; rsd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> rs;
    if (!Check(dev->CreateRasterizerState(&rsd,&rs),"CreateRasterizerState")) return 16;

    D3D11_VIEWPORT vp{}; vp.Width=static_cast<float>(outW); vp.Height=static_cast<float>(outH); vp.MinDepth=0.0f; vp.MaxDepth=1.0f;
    ID3D11RenderTargetView* rtvs[]={rtv.Get()}; ID3D11ShaderResourceView* srvs[]={srv.Get()}; ID3D11SamplerState* samplers[]={sampler.Get()}; ID3D11Buffer* cbs[]={cb.Get()};
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->RSSetState(rs.Get()); ctx->RSSetViewports(1,&vp); ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(ps.Get(),nullptr,0);
    ctx->PSSetShaderResources(0,1,srvs); ctx->PSSetSamplers(0,1,samplers); ctx->PSSetConstantBuffers(0,1,cbs); ctx->OMSetRenderTargets(1,rtvs,nullptr);
    const float clear[4]={0,0,0,0}; ctx->ClearRenderTargetView(rtv.Get(),clear); ctx->Draw(3,0);
    ID3D11ShaderResourceView* nullSrv[]={nullptr}; ctx->PSSetShaderResources(0,1,nullSrv); ctx->CopyResource(staging.Get(),outputTex.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!Check(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map(staging)")) return 17;
    const std::size_t rowBytes=static_cast<std::size_t>(outW)*4u;
    std::vector<std::uint8_t> contiguous(rowBytes*outH);
    for(UINT y=0;y<outH;++y)
    {
        const auto* src=static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch;
        std::memcpy(contiguous.data()+static_cast<std::size_t>(y)*rowBytes,src,rowBytes);
    }
    ctx->Unmap(staging.Get(),0);
    if (!WriteAll(outputPath,contiguous.data(),contiguous.size())) return 18;
    std::cout << "runtime_pass=1 format=R8G8B8A8_UNORM input=" << inW << "x" << inH << " output=" << outW << "x" << outH << " bytes=" << contiguous.size() << "\n";
    return 0;
}
