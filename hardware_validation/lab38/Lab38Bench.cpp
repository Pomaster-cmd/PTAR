#include "Lab38Bench.h"

#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"bcrypt.lib")

using Microsoft::WRL::ComPtr;

namespace ptar_lab38 {
namespace {

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") || !f) return false;
    _fseeki64(f, 0, SEEK_END);
    const __int64 n = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    const size_t got = fread(out.data(), 1, static_cast<size_t>(n), f);
    fclose(f);
    return got == static_cast<size_t>(n);
}

std::string Hex(const unsigned char* p, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s(n * 2u, '0');
    for (size_t i = 0; i < n; ++i) {
        s[i * 2] = h[(p[i] >> 4) & 15];
        s[i * 2 + 1] = h[p[i] & 15];
    }
    return s;
}

HRESULT Sha256(const std::wstring& path, std::string& out) {
    std::vector<unsigned char> data;
    if (!ReadFileBytes(path, data)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objN = 0, hashN = 0, cb = 0;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (st < 0) return E_FAIL;
    st = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objN), sizeof(objN), &cb, 0);
    if (st < 0) { BCryptCloseAlgorithmProvider(alg, 0); return E_FAIL; }
    st = BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashN), sizeof(hashN), &cb, 0);
    if (st < 0 || hashN != 32u) { BCryptCloseAlgorithmProvider(alg, 0); return E_FAIL; }

    std::vector<unsigned char> obj(objN), digest(hashN);
    st = BCryptCreateHash(alg, &hash, obj.data(), objN, nullptr, 0, 0);
    if (st < 0) { BCryptCloseAlgorithmProvider(alg, 0); return E_FAIL; }
    size_t off = 0;
    while (off < data.size()) {
        const size_t rem = data.size() - off;
        const ULONG chunk = static_cast<ULONG>((std::min)(rem, static_cast<size_t>(0x40000000u)));
        st = BCryptHashData(hash, data.data() + off, chunk, 0);
        if (st < 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(alg, 0);
            return E_FAIL;
        }
        off += chunk;
    }
    st = BCryptFinishHash(hash, digest.data(), hashN, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st < 0) return E_FAIL;
    out = Hex(digest.data(), digest.size());
    return S_OK;
}

HRESULT Verify(const std::wstring& path, const char* expected, const char* label) {
    std::string got;
    const HRESULT hr = Sha256(path, got);
    if (FAILED(hr)) return hr;
    if (got != expected) {
        std::cerr << "[FAIL] SHA256 " << label << " expected=" << expected << " got=" << got << std::endl;
        return HRESULT_FROM_WIN32(ERROR_CRC);
    }
    std::cout << "[PASS] SHA256 " << label << "=" << got << std::endl;
    return S_OK;
}

bool HasDxbcSignature(const std::vector<unsigned char>& bytes) {
    return bytes.size() >= 4u && memcmp(bytes.data(), "DXBC", 4) == 0;
}

} // namespace

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

Bench::Bench() : m_fl(D3D_FEATURE_LEVEL_9_1) {}
Bench::~Bench() { if (m_ctx) m_ctx->ClearState(); }

HRESULT Bench::Init(bool warp, std::wstring& adapterName) {
    HRESULT hr = S_OK;
    if (warp) {
        D3D_FEATURE_LEVEL req[] = { D3D_FEATURE_LEVEL_11_0 };
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, req, 1, D3D11_SDK_VERSION, &m_dev, &m_fl, &m_ctx);
        adapterName = L"WARP";
    } else {
        ComPtr<IDXGIFactory1> factory;
        hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
        if (FAILED(hr)) return hr;
        ComPtr<IDXGIAdapter1> chosen;
        DXGI_ADAPTER_DESC1 chosenDesc = {};
        std::wcout << L"[GPU] adapters:" << std::endl;
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d = {};
            adapter->GetDesc1(&d);
            std::wcout << L"  [" << i << L"] " << d.Description << L" vendor=0x" << std::hex << d.VendorId << std::dec << std::endl;
            if (!chosen && d.VendorId == kNvidiaVendor && wcsstr(d.Description, L"GTX 960M")) {
                chosen = adapter;
                chosenDesc = d;
            }
        }
        if (!chosen) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        D3D_FEATURE_LEVEL req[] = { D3D_FEATURE_LEVEL_11_0 };
        hr = D3D11CreateDevice(chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, req, 1, D3D11_SDK_VERSION, &m_dev, &m_fl, &m_ctx);
        adapterName = chosenDesc.Description;
    }
    if (FAILED(hr)) return hr;
    if (m_fl < D3D_FEATURE_LEVEL_11_0) return E_FAIL;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_dev->CreateSamplerState(&sd, &m_sampler);
    if (FAILED(hr)) return hr;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    hr = m_dev->CreateRasterizerState(&rd, &m_raster);
    if (FAILED(hr)) return hr;

    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    hr = m_dev->CreateDepthStencilState(&dd, &m_depth);
    if (FAILED(hr)) return hr;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_dev->CreateBuffer(&bd, nullptr, &m_cb);
    if (FAILED(hr)) return hr;

    return CreateTargets();
}

HRESULT Bench::Load(const std::wstring& dir) {
    const std::wstring labVsPath = Join(dir, L"lab_fullscreen_vs.cso");
    const std::wstring kVsPath = Join(dir, L"ptar_vs.cso");
    HRESULT hr = Verify(labVsPath, kLabVsSha256, "lab_fullscreen_vs.cso");
    if (FAILED(hr)) return hr;
    hr = Verify(kVsPath, kK185VsSha256, "ptar_vs.cso");
    if (FAILED(hr)) return hr;

    std::vector<unsigned char> labVsBytes, kVsBytes;
    if (!ReadFileBytes(labVsPath, labVsBytes) || !ReadFileBytes(kVsPath, kVsBytes)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (!HasDxbcSignature(labVsBytes) || !HasDxbcSignature(kVsBytes)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    hr = m_dev->CreateVertexShader(labVsBytes.data(), labVsBytes.size(), nullptr, &m_labVs);
    if (FAILED(hr)) return hr;
    hr = m_dev->CreateVertexShader(kVsBytes.data(), kVsBytes.size(), nullptr, &m_kVs);
    if (FAILED(hr)) return hr;

    for (int v = LAB37B; v <= LAB38D; ++v) {
        const std::wstring path = Join(dir, kShaderFiles[v]);
        hr = Verify(path, kShaderSha256[v], kNames[v]);
        if (FAILED(hr)) return hr;
        std::vector<unsigned char> bytes;
        if (!ReadFileBytes(path, bytes)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (!HasDxbcSignature(bytes)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        hr = m_dev->CreatePixelShader(bytes.data(), bytes.size(), nullptr, &m_labPs[static_cast<size_t>(v)]);
        if (FAILED(hr)) return hr;
    }

    const std::wstring kPath = Join(dir, kShaderFiles[K185]);
    hr = Verify(kPath, kShaderSha256[K185], kNames[K185]);
    if (FAILED(hr)) return hr;
    std::vector<unsigned char> kBytes;
    if (!ReadFileBytes(kPath, kBytes)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (!HasDxbcSignature(kBytes)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    return m_dev->CreatePixelShader(kBytes.data(), kBytes.size(), nullptr, &m_kPs);
}

HRESULT Bench::Smoke() {
    for (int v = 0; v < VARIANT_COUNT; ++v) {
        SetPipe(v);
        m_ctx->Draw(3, 0);
    }
    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_ctx->PSSetShaderResources(0, 1, &nullSrv);
    return m_dev->GetDeviceRemovedReason();
}

HRESULT Bench::Run(std::vector<SampleRow>& rows) {
    PTARD3D11GpuTimerRing timer;
    HRESULT hr = timer.Initialize(m_dev.Get());
    if (FAILED(hr)) return hr;

    for (UINT i = 0; i < kWarmupGroups; ++i) {
        const int* order = kOrders[i % VARIANT_COUNT];
        for (int pos = 0; pos < VARIANT_COUNT; ++pos) {
            double ms = 0.0;
            hr = Timed(timer, order[pos], ms);
            if (FAILED(hr)) return hr;
        }
    }

    rows.clear();
    rows.reserve(kSamples);
    for (UINT i = 0; i < kSamples; ++i) {
        SampleRow row;
        row.index = i;
        const int* order = kOrders[i % VARIANT_COUNT];
        for (int pos = 0; pos < VARIANT_COUNT; ++pos) {
            row.order[static_cast<size_t>(pos)] = order[pos];
            double ms = 0.0;
            hr = Timed(timer, order[pos], ms);
            if (FAILED(hr)) return hr;
            row.ms[static_cast<size_t>(order[pos])] = ms;
        }
        rows.push_back(row);
    }

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_ctx->PSSetShaderResources(0, 1, &nullSrv);
    return S_OK;
}

HRESULT Bench::CreateTargets() {
    std::vector<unsigned char> px(static_cast<size_t>(kInW) * kInH * 4u);
    for (UINT y = 0; y < kInH; ++y) {
        for (UINT x = 0; x < kInW; ++x) {
            const size_t i = (static_cast<size_t>(y) * kInW + x) * 4u;
            px[i] = static_cast<unsigned char>((x * 17u + y * 13u + (x ^ y)) & 255u);
            px[i + 1] = static_cast<unsigned char>((x * 5u + y * 29u + ((x * 3u) ^ (y * 7u))) & 255u);
            px[i + 2] = static_cast<unsigned char>((x * 31u + y * 3u + (x + y) * 11u) & 255u);
            px[i + 3] = 255u;
        }
    }

    D3D11_TEXTURE2D_DESC d = {};
    d.Width = kInW;
    d.Height = kInH;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = px.data();
    init.SysMemPitch = kInW * 4u;
    HRESULT hr = m_dev->CreateTexture2D(&d, &init, &m_in);
    if (FAILED(hr)) return hr;
    hr = m_dev->CreateShaderResourceView(m_in.Get(), nullptr, &m_srv);
    if (FAILED(hr)) return hr;

    d.Width = kOutW;
    d.Height = kOutH;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = m_dev->CreateTexture2D(&d, nullptr, &m_out);
    if (FAILED(hr)) return hr;
    return m_dev->CreateRenderTargetView(m_out.Get(), nullptr, &m_rtv);
}

void Bench::SetPipe(int variant) {
    Constants c = {};
    if (variant == K185) {
        c.inputSize[0] = static_cast<float>(kInW);
        c.inputSize[1] = static_cast<float>(kInH);
        c.outputSize[0] = static_cast<float>(kOutW);
        c.outputSize[1] = static_cast<float>(kOutH);
    } else {
        c.inputSize[0] = 1.0f / static_cast<float>(kInW);
        c.inputSize[1] = 1.0f / static_cast<float>(kInH);
        c.outputSize[0] = 0.5f / static_cast<float>(kInW);
        c.outputSize[1] = 0.5f / static_cast<float>(kInH);
    }
    m_ctx->UpdateSubresource(m_cb.Get(), 0, nullptr, &c, 0, 0);

    D3D11_VIEWPORT vp = { 0, 0, static_cast<FLOAT>(kOutW), static_cast<FLOAT>(kOutH), 0, 1 };
    m_ctx->RSSetViewports(1, &vp);
    m_ctx->RSSetState(m_raster.Get());
    m_ctx->OMSetDepthStencilState(m_depth.Get(), 0);
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    m_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    m_ctx->IASetInputLayout(nullptr);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->VSSetShader(variant == K185 ? m_kVs.Get() : m_labVs.Get(), nullptr, 0);
    ID3D11PixelShader* ps = variant == K185 ? m_kPs.Get() : m_labPs[static_cast<size_t>(variant)].Get();
    m_ctx->PSSetShader(ps, nullptr, 0);
    ID3D11ShaderResourceView* srv = m_srv.Get();
    m_ctx->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* sampler = m_sampler.Get();
    m_ctx->PSSetSamplers(0, 1, &sampler);
    ID3D11Buffer* cb = m_cb.Get();
    m_ctx->PSSetConstantBuffers(0, 1, &cb);
}

HRESULT Bench::Timed(PTARD3D11GpuTimerRing& timer, int variant, double& ms) {
    SetPipe(variant);
    for (;;) {
        if (!timer.Begin(m_ctx.Get())) return E_UNEXPECTED;
        m_ctx->Draw(3, 0);
        if (!timer.End(m_ctx.Get())) return E_UNEXPECTED;
        const ULONGLONG start = GetTickCount64();
        for (;;) {
            bool ready = false, valid = false;
            const HRESULT hr = timer.TryResolveEx(m_ctx.Get(), &ms, 0u, &ready, &valid);
            if (FAILED(hr)) return hr;
            if (ready) {
                if (valid) return S_OK;
                break;
            }
            const HRESULT removed = m_dev->GetDeviceRemovedReason();
            if (FAILED(removed)) return removed;
            if (GetTickCount64() - start >= kQueryTimeoutMs) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            Sleep(1);
        }
    }
}

} // namespace ptar_lab38
