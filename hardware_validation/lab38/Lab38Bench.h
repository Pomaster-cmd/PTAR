#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <string>
#include <vector>

#include "Lab38Variants.h"
#include "PTARD3D11GpuTimerRing.h"

namespace ptar_lab38 {

class Bench {
public:
    Bench();
    ~Bench();

    HRESULT Init(bool warp, std::wstring& adapterName);
    HRESULT Load(const std::wstring& shaderDir);
    HRESULT Smoke();
    HRESULT Run(std::vector<SampleRow>& rows);

private:
    struct Constants { float inputSize[2]; float outputSize[2]; };

    HRESULT CreateTargets();
    void SetPipe(int variant);
    HRESULT Timed(PTARD3D11GpuTimerRing& timer, int variant, double& ms);

    Microsoft::WRL::ComPtr<ID3D11Device> m_dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_ctx;
    D3D_FEATURE_LEVEL m_fl;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_raster;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depth;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cb;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_in;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_out;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_labVs;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_kVs;
    std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, 5> m_labPs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_kPs;
};

std::wstring Join(const std::wstring& a, const std::wstring& b);

} // namespace ptar_lab38
