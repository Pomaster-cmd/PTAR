#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "../runtime_integration/d3d11/PTARD3D11GpuTimerRing.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

static const UINT PTAR_NVIDIA_VENDOR_ID = 0x10DEu;
static const UINT PTAR_PARITY_MAX_LSB = 1u;
static const UINT PTAR_WARMUP_FRAMES = 32u;
static const UINT PTAR_TIMING_SAMPLES = 512u;
static const DWORD PTAR_QUERY_WAIT_TIMEOUT_MS = 1000u;

struct ImageRGBA
{
    UINT width;
    UINT height;
    std::vector<unsigned char> pixels;
    ImageRGBA() : width(0), height(0) {}
};

struct TimingStats
{
    double median;
    double p90;
    double p95;
    double p99;
    double maximum;
    UINT validSamples;
    TimingStats() : median(0), p90(0), p95(0), p99(0), maximum(0), validSamples(0) {}
};

struct TimingPair
{
    UINT pairIndex;
    bool k185First;
    double k185Ms;
    double bilinearMs;
    double deltaMs;
    TimingPair() : pairIndex(0), k185First(true), k185Ms(0), bilinearMs(0), deltaMs(0) {}
};

class ComApartmentGuard
{
public:
    explicit ComApartmentGuard(DWORD flags)
        : m_hr(CoInitializeEx(0,flags)), m_initialized(SUCCEEDED(m_hr))
    {
    }

    ~ComApartmentGuard()
    {
        if(m_initialized)
            CoUninitialize();
    }

    HRESULT Result() const { return m_hr; }

private:
    HRESULT m_hr;
    bool m_initialized;

    ComApartmentGuard(const ComApartmentGuard&);
    ComApartmentGuard& operator=(const ComApartmentGuard&);
};

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    if (a[a.size()-1] == L'\\' || a[a.size()-1] == L'/') return a + b;
    return a + L"\\" + b;
}

static bool WideToUtf8(const std::wstring& input, std::string& output)
{
    output.clear();
    if (input.empty()) return true;

    if (input.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;

    const int inputLength=static_cast<int>(input.size());

    const int required=WideCharToMultiByte(
        CP_UTF8,
        0,
        input.data(),
        inputLength,
        0,
        0,
        0,
        0);

    if (required<=0) return false;

    output.resize(static_cast<size_t>(required));
    const int written=WideCharToMultiByte(
        CP_UTF8,
        0,
        input.data(),
        inputLength,
        &output[0],
        required,
        0,
        0);

    if (written!=required)
    {
        output.clear();
        return false;
    }

    return true;
}

static bool DirectoryExists(const std::wstring& p)
{
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool EnsureDirectoryRecursive(const std::wstring& path)
{
    if (path.empty() || DirectoryExists(path)) return true;
    std::wstring parent = path;
    size_t pos = parent.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        parent.resize(pos);
        if (!parent.empty() && !DirectoryExists(parent))
        {
            if (!EnsureDirectoryRecursive(parent)) return false;
        }
    }
    if (CreateDirectoryW(path.c_str(), 0)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::wstring BaseNameWithoutSuffix(const std::wstring& file, const std::wstring& suffix)
{
    if (file.size() >= suffix.size() && file.compare(file.size()-suffix.size(), suffix.size(), suffix) == 0)
        return file.substr(0, file.size()-suffix.size());
    return file;
}

static bool ReadBinaryFile(const std::wstring& path, std::vector<unsigned char>& out)
{
    FILE* f = 0;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    _fseeki64(f, 0, SEEK_END);
    __int64 n = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = fread(&out[0], 1, (size_t)n, f);
    fclose(f);
    return got == (size_t)n;
}

static HRESULT WriteBlobFile(const std::wstring& path, ID3DBlob* blob)
{
    if (!blob) return E_INVALIDARG;
    FILE* f=0;
    if (_wfopen_s(&f,path.c_str(),L"wb")!=0 || !f) return E_FAIL;
    const size_t n=blob->GetBufferSize();
    const size_t wrote=fwrite(blob->GetBufferPointer(),1,n,f);
    fclose(f);
    return wrote==n ? S_OK : E_FAIL;
}

static HRESULT CompileHlslFile(
    const std::wstring& path,
    const char* entry,
    const char* target,
    ID3DBlob** outBlob)
{
    if (!outBlob) return E_INVALIDARG;
    *outBlob=0;

    UINT flags=D3DCOMPILE_ENABLE_STRICTNESS |
               D3DCOMPILE_WARNINGS_ARE_ERRORS |
               D3DCOMPILE_OPTIMIZATION_LEVEL3;

    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    HRESULT hr=D3DCompileFromFile(
        path.c_str(),
        0,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry,
        target,
        flags,
        0,
        &code,
        &errors);

    if (errors && errors->GetBufferSize())
    {
        const char* p=(const char*)errors->GetBufferPointer();
        std::cerr << "[HLSL] " << path.size() << " wchar path: "
                  << std::string(p,p+errors->GetBufferSize()) << std::endl;
    }

    if (FAILED(hr)) return hr;
    *outBlob=code.Detach();
    return S_OK;
}

static size_t CountToken(const std::string& text,const std::string& token)
{
    size_t count=0;
    size_t pos=0;
    while((pos=text.find(token,pos))!=std::string::npos)
    {
        ++count;
        pos += token.size();
    }
    return count;
}

static HRESULT AuditK185Bytecode(ID3DBlob* shaderBlob,const std::wstring& asmPath)
{
    if (!shaderBlob) return E_INVALIDARG;
    ComPtr<ID3DBlob> disasm;
    HRESULT hr=D3DDisassemble(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING,
        0,
        &disasm);
    if (FAILED(hr)) return hr;

    hr=WriteBlobFile(asmPath,disasm.Get());
    if (FAILED(hr)) return hr;

    const char* p=(const char*)disasm->GetBufferPointer();
    std::string text(p,p+disasm->GetBufferSize());

    const size_t gather=CountToken(text,"gather4");
    const size_t sampleL=CountToken(text,"sample_l");
    const size_t uav=
        CountToken(text,"store_uav")+
        CountToken(text,"ld_uav")+
        CountToken(text,"dcl_uav");

    std::cout << "[DXBC] gather4=" << gather
              << " sample_l=" << sampleL
              << " uav_tokens=" << uav << std::endl;

    if (gather!=1u) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (sampleL!=4u) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (uav!=0u) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

    std::cout << "[PASS] DXBC audit: 1 gather4 + 4 sample_l + 0 UAV." << std::endl;
    return S_OK;
}

static HRESULT LoadPngRGBA(IWICImagingFactory* factory, const std::wstring& path, ImageRGBA& out)
{
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(
        path.c_str(), 0, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return hr;

    UINT w=0,h=0;
    hr = frame->GetSize(&w,&h);
    if (FAILED(hr)) return hr;

    ComPtr<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr)) return hr;
    hr = conv->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, 0, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    out.width=w; out.height=h;
    out.pixels.resize((size_t)w*(size_t)h*4u);
    hr = conv->CopyPixels(0, w*4u, (UINT)out.pixels.size(), &out.pixels[0]);
    return hr;
}

static HRESULT GetFileSize64(const std::wstring& path, unsigned long long& bytes)
{
    bytes=0;
    WIN32_FILE_ATTRIBUTE_DATA data={};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data))
        return HRESULT_FROM_WIN32(GetLastError());

    ULARGE_INTEGER size={};
    size.HighPart=data.nFileSizeHigh;
    size.LowPart=data.nFileSizeLow;
    bytes=size.QuadPart;
    return S_OK;
}

static HRESULT SavePngRGBA(
    IWICImagingFactory* factory,
    const std::wstring& path,
    const ImageRGBA& img,
    unsigned long long* outputBytes)
{
    if(outputBytes) *outputBytes=0;
    if(!factory || img.width==0 || img.height==0 || img.pixels.empty())
        return E_INVALIDARG;

    const size_t expectedBytes=(size_t)img.width*(size_t)img.height*4u;
    if(img.pixels.size()!=expectedBytes)
        return E_INVALIDARG;

    size_t slash=path.find_last_of(L"\\/");
    if(slash!=std::wstring::npos)
    {
        if(!EnsureDirectoryRecursive(path.substr(0,slash))) return E_FAIL;
    }

    // Windows' native PNG encoder uses BGRA reliably. The validator's
    // in-memory/readback representation is RGBA, therefore convert explicitly.
    std::vector<BYTE> bgra(expectedBytes);
    for(size_t p=0;p<(size_t)img.width*(size_t)img.height;++p)
    {
        bgra[p*4u+0u]=img.pixels[p*4u+2u];
        bgra[p*4u+1u]=img.pixels[p*4u+1u];
        bgra[p*4u+2u]=img.pixels[p*4u+0u];
        bgra[p*4u+3u]=img.pixels[p*4u+3u];
    }

    ComPtr<IWICStream> stream;
    HRESULT hr=factory->CreateStream(&stream);
    if(FAILED(hr)) return hr;
    hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
    if(FAILED(hr)) return hr;

    ComPtr<IWICBitmapEncoder> enc;
    hr=factory->CreateEncoder(GUID_ContainerFormatPng,0,&enc);
    if(FAILED(hr)) return hr;
    hr=enc->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(FAILED(hr)) return hr;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr=enc->CreateNewFrame(&frame,&props);
    if(FAILED(hr)) return hr;
    hr=frame->Initialize(props.Get());
    if(FAILED(hr)) return hr;
    hr=frame->SetSize(img.width,img.height);
    if(FAILED(hr)) return hr;

    WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;
    hr=frame->SetPixelFormat(&fmt);
    if(FAILED(hr)) return hr;
    if(fmt!=GUID_WICPixelFormat32bppBGRA)
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;

    if(img.width > (std::numeric_limits<UINT>::max)()/4u)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    const UINT stride=img.width*4u;
    if(bgra.size() > (std::numeric_limits<UINT>::max)())
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    hr=frame->WritePixels(
        img.height,
        stride,
        static_cast<UINT>(bgra.size()),
        &bgra[0]);
    if(FAILED(hr)) return hr;

    hr=frame->Commit();
    if(FAILED(hr)) return hr;
    hr=enc->Commit();
    if(FAILED(hr)) return hr;

    // Release WIC file objects before checking the resulting file size.
    frame.Reset();
    props.Reset();
    enc.Reset();
    stream.Reset();

    unsigned long long bytes=0;
    hr=GetFileSize64(path,bytes);
    if(FAILED(hr)) return hr;
    if(bytes==0)
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);

    if(outputBytes) *outputBytes=bytes;
    return S_OK;
}

static std::vector<std::wstring> EnumerateCases(const std::wstring& inputDir)
{
    std::vector<std::wstring> ids;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(inputDir, L"*_LR.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return ids;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            ids.push_back(BaseNameWithoutSuffix(fd.cFileName, L"_LR.png"));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(ids.begin(), ids.end());
    return ids;
}

class D3D11Harness
{
public:
    D3D11Harness() : m_featureLevel(D3D_FEATURE_LEVEL_9_1) {}

    ~D3D11Harness()
    {
        if(m_context)
            m_context->ClearState();
    }

    HRESULT Initialize(bool allowAnyAdapter, std::wstring& adapterName)
    {
        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
        if (FAILED(hr)) return hr;

        ComPtr<IDXGIAdapter1> chosen;
        DXGI_ADAPTER_DESC1 chosenDesc = {};
        std::wcout << L"[GPU] Adapters detectes:" << std::endl;
        for (UINT i=0;;++i)
        {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d = {};
            a->GetDesc1(&d);
            std::wcout << L"  [" << i << L"] " << d.Description
                       << L" vendor=0x" << std::hex << d.VendorId << std::dec
                       << L" dedicatedMB=" << (unsigned long long)(d.DedicatedVideoMemory/(1024ull*1024ull))
                       << std::endl;
            if (!chosen && d.VendorId == PTAR_NVIDIA_VENDOR_ID)
            {
                chosen = a;
                chosenDesc = d;
            }
            if (!chosen && allowAnyAdapter)
            {
                chosen = a;
                chosenDesc = d;
            }
        }
        if (!chosen) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0 };
        hr = D3D11CreateDevice(
            chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, 0, 0,
            requested, 1, D3D11_SDK_VERSION,
            &m_device, &m_featureLevel, &m_context);
        if (FAILED(hr)) return hr;
        if (m_featureLevel < D3D_FEATURE_LEVEL_11_0) return E_FAIL;

        adapterName = chosenDesc.Description;

        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = m_device->CreateSamplerState(&sd, &m_sampler);
        if (FAILED(hr)) return hr;

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        hr = m_device->CreateRasterizerState(&rd, &m_raster);
        if (FAILED(hr)) return hr;

        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = FALSE;
        dd.StencilEnable = FALSE;
        hr = m_device->CreateDepthStencilState(&dd, &m_depthOff);
        if (FAILED(hr)) return hr;

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = m_device->CreateBuffer(&bd, 0, &m_constants);
        return hr;
    }

    HRESULT CompileAndLoadShaders(
        const std::wstring& vsPath,
        const std::wstring& psPath,
        const std::wstring& bypassPath,
        const std::wstring& artifactDir)
    {
        ComPtr<ID3DBlob> v,p,b;

        std::cout << "[HLSL] Compiling VS via D3DCompileFromFile / vs_5_0" << std::endl;
        HRESULT hr=CompileHlslFile(vsPath,"main","vs_5_0",&v);
        if (FAILED(hr)) return hr;

        std::cout << "[HLSL] Compiling K185 PS via D3DCompileFromFile / ps_5_0" << std::endl;
        hr=CompileHlslFile(psPath,"main","ps_5_0",&p);
        if (FAILED(hr)) return hr;

        std::cout << "[HLSL] Compiling bilinear PS via D3DCompileFromFile / ps_5_0" << std::endl;
        hr=CompileHlslFile(bypassPath,"main","ps_5_0",&b);
        if (FAILED(hr)) return hr;

        hr=WriteBlobFile(JoinPath(artifactDir,L"ptar_vs.cso"),v.Get());
        if (FAILED(hr)) return hr;
        hr=WriteBlobFile(JoinPath(artifactDir,L"ptar_k185_ps.cso"),p.Get());
        if (FAILED(hr)) return hr;
        hr=WriteBlobFile(JoinPath(artifactDir,L"ptar_bilinear_ps.cso"),b.Get());
        if (FAILED(hr)) return hr;

        hr=AuditK185Bytecode(p.Get(),JoinPath(artifactDir,L"ptar_k185_ps.asm"));
        if (FAILED(hr)) return hr;

        hr=m_device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),0,&m_vs);
        if (FAILED(hr)) return hr;
        hr=m_device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),0,&m_psK185);
        if (FAILED(hr)) return hr;
        return m_device->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),0,&m_psBypass);
    }

    HRESULT RenderK185(const ImageRGBA& input, UINT outW, UINT outH, ImageRGBA& output)
    {
        ComPtr<ID3D11Texture2D> inTex, outTex, staging;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11RenderTargetView> rtv;
        HRESULT hr = CreateInputTexture(input, &inTex, &srv);
        if (FAILED(hr)) return hr;
        hr = CreateOutputTargets(outW,outH,&outTex,&rtv,&staging);
        if (FAILED(hr)) return hr;

        SetPipeline(srv.Get(),rtv.Get(),input.width,input.height,outW,outH,m_psK185.Get());
        m_context->Draw(3,0);
        ID3D11ShaderResourceView* nullSrv=0;
        m_context->PSSetShaderResources(0,1,&nullSrv);
        m_context->CopyResource(staging.Get(),outTex.Get());

        D3D11_MAPPED_SUBRESOURCE map={};
        hr=m_context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
        if (FAILED(hr)) return hr;
        output.width=outW; output.height=outH;
        output.pixels.resize((size_t)outW*(size_t)outH*4u);
        for(UINT y=0;y<outH;++y)
            memcpy(&output.pixels[(size_t)y*outW*4u],
                   (const unsigned char*)map.pData + (size_t)y*map.RowPitch,
                   (size_t)outW*4u);
        m_context->Unmap(staging.Get(),0);
        return S_OK;
    }

    HRESULT DeviceRemovedReason() const
    {
        return m_device ? m_device->GetDeviceRemovedReason() : E_POINTER;
    }

    HRESULT Benchmark(UINT inW, UINT inH, UINT outW, UINT outH,
                      TimingStats& k185, TimingStats& bypass,
                      std::vector<TimingPair>& pairs)
    {
        ImageRGBA pattern;
        pattern.width=inW; pattern.height=inH;
        pattern.pixels.resize((size_t)inW*(size_t)inH*4u);
        for(UINT y=0;y<inH;++y)
        for(UINT x=0;x<inW;++x)
        {
            size_t i=((size_t)y*inW+x)*4u;
            pattern.pixels[i+0]=(unsigned char)((x*17u+y*13u+(x^y))&255u);
            pattern.pixels[i+1]=(unsigned char)((x*5u+y*29u+(x*3u^y*7u))&255u);
            pattern.pixels[i+2]=(unsigned char)((x*31u+y*3u+(x+y)*11u)&255u);
            pattern.pixels[i+3]=255u;
        }

        ComPtr<ID3D11Texture2D> inTex,outTex;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11RenderTargetView> rtv;
        HRESULT hr=CreateInputTexture(pattern,&inTex,&srv);
        if(FAILED(hr)) return hr;
        hr=CreateOutputTargets(outW,outH,&outTex,&rtv,0);
        if(FAILED(hr)) return hr;

        return MeasureInterleaved(
            srv.Get(),rtv.Get(),inW,inH,outW,outH,
            m_psK185.Get(),m_psBypass.Get(),k185,bypass,pairs);
    }

private:
    struct Constants { float inputSize[2]; float outputSize[2]; };

    HRESULT CreateInputTexture(const ImageRGBA& img, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)
    {
        D3D11_TEXTURE2D_DESC d={};
        d.Width=img.width; d.Height=img.height; d.MipLevels=1; d.ArraySize=1;
        d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init={};
        init.pSysMem=&img.pixels[0]; init.SysMemPitch=img.width*4u;
        HRESULT hr=m_device->CreateTexture2D(&d,&init,tex);
        if(FAILED(hr)) return hr;
        return m_device->CreateShaderResourceView(*tex,0,srv);
    }

    HRESULT CreateOutputTargets(UINT w,UINT h,ID3D11Texture2D** tex,ID3D11RenderTargetView** rtv,ID3D11Texture2D** staging)
    {
        D3D11_TEXTURE2D_DESC d={};
        d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1;
        d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_RENDER_TARGET;
        HRESULT hr=m_device->CreateTexture2D(&d,0,tex);
        if(FAILED(hr)) return hr;
        hr=m_device->CreateRenderTargetView(*tex,0,rtv);
        if(FAILED(hr)) return hr;
        if(staging)
        {
            D3D11_TEXTURE2D_DESC s=d;
            s.Usage=D3D11_USAGE_STAGING; s.BindFlags=0; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            hr=m_device->CreateTexture2D(&s,0,staging);
        }
        return hr;
    }

    void SetPipeline(ID3D11ShaderResourceView* srv, ID3D11RenderTargetView* rtv,
                     UINT inW,UINT inH,UINT outW,UINT outH,ID3D11PixelShader* ps)
    {
        Constants c={{(float)inW,(float)inH},{(float)outW,(float)outH}};
        m_context->UpdateSubresource(m_constants.Get(),0,0,&c,0,0);
        D3D11_VIEWPORT vp={0,0,(FLOAT)outW,(FLOAT)outH,0,1};
        m_context->RSSetViewports(1,&vp);
        m_context->RSSetState(m_raster.Get());
        m_context->OMSetDepthStencilState(m_depthOff.Get(),0);
        m_context->OMSetRenderTargets(1,&rtv,0);
        m_context->IASetInputLayout(0);
        m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs.Get(),0,0);
        m_context->PSSetShader(ps,0,0);
        m_context->PSSetShaderResources(0,1,&srv);
        ID3D11SamplerState* sam=m_sampler.Get();
        m_context->PSSetSamplers(0,1,&sam);
        ID3D11Buffer* cb=m_constants.Get();
        m_context->PSSetConstantBuffers(0,1,&cb);
    }

    static double Percentile(std::vector<double> v,double p)
    {
        if(v.empty()) return 0.0;
        std::sort(v.begin(),v.end());
        double pos=(v.size()-1)*p;
        size_t lo=(size_t)pos;
        size_t hi=(lo+1<v.size())?lo+1:lo;
        double t=pos-(double)lo;
        return v[lo]+(v[hi]-v[lo])*t;
    }

    HRESULT WaitForTimerSample(
        PTARD3D11GpuTimerRing& timer,
        double& milliseconds,
        bool& valid)
    {
        const ULONGLONG start=GetTickCount64();

        for(;;)
        {
            bool ready=false;
            valid=false;

            // flags=0 is deliberate. It lets the D3D11 runtime make command
            // buffer progress if required, while PTAR still never calls
            // ID3D11DeviceContext::Flush explicitly.
            HRESULT hr=timer.TryResolveEx(
                m_context.Get(),&milliseconds,0u,&ready,&valid);

            if(FAILED(hr)) return hr;
            if(ready) return S_OK;

            const HRESULT deviceReason=m_device->GetDeviceRemovedReason();
            if(FAILED(deviceReason)) return deviceReason;

            if(GetTickCount64()-start >= PTAR_QUERY_WAIT_TIMEOUT_MS)
                return HRESULT_FROM_WIN32(ERROR_TIMEOUT);

            Sleep(1);
        }
    }

    HRESULT SubmitAndWaitTimedDraw(
        PTARD3D11GpuTimerRing& timer,
        double& milliseconds,
        bool& valid)
    {
        if(!timer.Begin(m_context.Get())) return E_UNEXPECTED;
        m_context->Draw(3,0);
        if(!timer.End(m_context.Get())) return E_UNEXPECTED;
        return WaitForTimerSample(timer,milliseconds,valid);
    }

    HRESULT TimedShaderDraw(
        PTARD3D11GpuTimerRing& timer,
        ID3D11ShaderResourceView* srv,
        ID3D11RenderTargetView* rtv,
        UINT inW,UINT inH,UINT outW,UINT outH,
        ID3D11PixelShader* ps,
        double& milliseconds)
    {
        SetPipeline(srv,rtv,inW,inH,outW,outH,ps);

        for(;;)
        {
            bool valid=false;
            HRESULT hr=SubmitAndWaitTimedDraw(timer,milliseconds,valid);
            if(FAILED(hr)) return hr;
            if(valid) return S_OK;
        }
    }

    void FillTimingStats(const std::vector<double>& samples,TimingStats& stats)
    {
        stats.validSamples=(UINT)samples.size();
        stats.median=Percentile(samples,0.50);
        stats.p90=Percentile(samples,0.90);
        stats.p95=Percentile(samples,0.95);
        stats.p99=Percentile(samples,0.99);
        stats.maximum=samples.empty()?0.0:*std::max_element(samples.begin(),samples.end());
    }

    HRESULT MeasureInterleaved(
        ID3D11ShaderResourceView* srv,ID3D11RenderTargetView* rtv,
        UINT inW,UINT inH,UINT outW,UINT outH,
        ID3D11PixelShader* k185Ps,ID3D11PixelShader* bilinearPs,
        TimingStats& k185,TimingStats& bilinear,
        std::vector<TimingPair>& pairs)
    {
        PTARD3D11GpuTimerRing timer;
        HRESULT hr=timer.Initialize(m_device.Get());
        if(FAILED(hr)) return hr;

        // Paired warm-up. Order alternates to avoid systematically favoring
        // the shader that always runs second after clocks/caches are warm.
        for(UINT i=0;i<PTAR_WARMUP_FRAMES;++i)
        {
            double a=0.0,b=0.0;
            if((i&1u)==0u)
            {
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,k185Ps,a);
                if(FAILED(hr)) return hr;
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,bilinearPs,b);
                if(FAILED(hr)) return hr;
            }
            else
            {
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,bilinearPs,b);
                if(FAILED(hr)) return hr;
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,k185Ps,a);
                if(FAILED(hr)) return hr;
            }
        }

        std::vector<double> kSamples,bSamples;
        kSamples.reserve(PTAR_TIMING_SAMPLES);
        bSamples.reserve(PTAR_TIMING_SAMPLES);
        pairs.clear();
        pairs.reserve(PTAR_TIMING_SAMPLES);

        for(UINT i=0;i<PTAR_TIMING_SAMPLES;++i)
        {
            TimingPair pair;
            pair.pairIndex=i;
            pair.k185First=((i&1u)==0u);

            if(pair.k185First)
            {
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,k185Ps,pair.k185Ms);
                if(FAILED(hr)) return hr;
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,bilinearPs,pair.bilinearMs);
                if(FAILED(hr)) return hr;
            }
            else
            {
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,bilinearPs,pair.bilinearMs);
                if(FAILED(hr)) return hr;
                hr=TimedShaderDraw(timer,srv,rtv,inW,inH,outW,outH,k185Ps,pair.k185Ms);
                if(FAILED(hr)) return hr;
            }

            pair.deltaMs=pair.k185Ms-pair.bilinearMs;
            kSamples.push_back(pair.k185Ms);
            bSamples.push_back(pair.bilinearMs);
            pairs.push_back(pair);
        }

        ID3D11ShaderResourceView* nullSrv=0;
        m_context->PSSetShaderResources(0,1,&nullSrv);

        FillTimingStats(kSamples,k185);
        FillTimingStats(bSamples,bilinear);
        return S_OK;
    }

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    D3D_FEATURE_LEVEL m_featureLevel;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_psK185;
    ComPtr<ID3D11PixelShader> m_psBypass;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_raster;
    ComPtr<ID3D11DepthStencilState> m_depthOff;
    ComPtr<ID3D11Buffer> m_constants;
};

static void WriteTimingCsv(const std::wstring& path,const TimingStats& k,const TimingStats& b)
{
    FILE* f=0;
    if(_wfopen_s(&f,path.c_str(),L"wb")!=0 || !f) return;
    fprintf(f,"variant,samples,median_ms,p90_ms,p95_ms,p99_ms,max_ms\r\n");
    fprintf(f,"EDGE_NG_V03_K185,%u,%.9f,%.9f,%.9f,%.9f,%.9f\r\n",
            k.validSamples,k.median,k.p90,k.p95,k.p99,k.maximum);
    fprintf(f,"BILINEAR_CONTROL,%u,%.9f,%.9f,%.9f,%.9f,%.9f\r\n",
            b.validSamples,b.median,b.p90,b.p95,b.p99,b.maximum);
    fclose(f);
}


static double Mean(const std::vector<double>& values)
{
    if(values.empty()) return 0.0;
    return std::accumulate(values.begin(),values.end(),0.0)/(double)values.size();
}

static double MedianCopy(std::vector<double> values)
{
    if(values.empty()) return 0.0;
    std::sort(values.begin(),values.end());
    const size_t n=values.size();
    if((n&1u)!=0u) return values[n/2u];
    return 0.5*(values[n/2u-1u]+values[n/2u]);
}

static void WriteTimingPairsCsv(const std::wstring& path,const std::vector<TimingPair>& pairs)
{
    FILE* f=0;
    if(_wfopen_s(&f,path.c_str(),L"wb")!=0 || !f) return;
    fprintf(f,"pair_index,order,k185_ms,bilinear_ms,delta_k185_minus_bilinear_ms\r\n");
    for(size_t i=0;i<pairs.size();++i)
    {
        const TimingPair& p=pairs[i];
        fprintf(f,"%u,%s,%.9f,%.9f,%.9f\r\n",
                p.pairIndex,p.k185First?"K185_FIRST":"BILINEAR_FIRST",
                p.k185Ms,p.bilinearMs,p.deltaMs);
    }
    fclose(f);
}

static void ComputePairedDeltaSummary(
    const std::vector<TimingPair>& pairs,
    double& meanDelta,double& medianDelta,
    UINT& k185Faster,UINT& bilinearFaster,UINT& ties)
{
    std::vector<double> deltas;
    deltas.reserve(pairs.size());
    k185Faster=bilinearFaster=ties=0u;
    for(size_t i=0;i<pairs.size();++i)
    {
        const double d=pairs[i].deltaMs;
        deltas.push_back(d);
        if(d<0.0) ++k185Faster;
        else if(d>0.0) ++bilinearFaster;
        else ++ties;
    }
    meanDelta=Mean(deltas);
    medianDelta=MedianCopy(deltas);
}

static int Usage()
{
    std::wcout << L"Usage: ptar_k185_hw_validation.exe --root <PTAR_PROJECT_MASTER_v0_9_0> "
                  L"--vs-src <vs.hlsl> --ps-src <k185.hlsl> --bypass-src <bypass.hlsl> --out <results> [--allow-any-adapter]"
               << std::endl;
    return 2;
}

int wmain(int argc,wchar_t** argv)
{
    std::wstring root,vsPath,psPath,bypassPath,outDir;
    bool allowAny=false;
    for(int i=1;i<argc;++i)
    {
        std::wstring a=argv[i];
        if(a==L"--root" && i+1<argc) root=argv[++i];
        else if(a==L"--vs-src" && i+1<argc) vsPath=argv[++i];
        else if(a==L"--ps-src" && i+1<argc) psPath=argv[++i];
        else if(a==L"--bypass-src" && i+1<argc) bypassPath=argv[++i];
        else if(a==L"--out" && i+1<argc) outDir=argv[++i];
        else if(a==L"--allow-any-adapter") allowAny=true;
        else return Usage();
    }
    if(root.empty()||vsPath.empty()||psPath.empty()||bypassPath.empty()||outDir.empty()) return Usage();
    if(!EnsureDirectoryRecursive(outDir)) return 3;

    // Declare the apartment guard BEFORE every COM object.
    // Destruction order is reverse declaration order, so all WIC/D3D objects
    // are released before CoUninitialize() is called by the guard.
    ComApartmentGuard com(COINIT_MULTITHREADED);
    HRESULT hr=com.Result();
    if(FAILED(hr)) { std::wcerr<<L"[FAIL] CoInitializeEx 0x"<<std::hex<<hr<<std::endl; return 4; }

    ComPtr<IWICImagingFactory> wic;
    hr=CoCreateInstance(CLSID_WICImagingFactory,0,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic));
    if(FAILED(hr)) {  return 5; }

    D3D11Harness gpu;
    std::wstring adapter;
    hr=gpu.Initialize(allowAny,adapter);
    if(FAILED(hr))
    {
        std::wcerr<<L"[FAIL] Aucun GPU NVIDIA D3D11 FL11_0 selectionnable. HRESULT=0x"<<std::hex<<hr<<std::endl;
        
        return 30;
    }
    std::wcout<<L"[GPU] Selection: "<<adapter<<std::endl;

    hr=gpu.CompileAndLoadShaders(vsPath,psPath,bypassPath,outDir);
    if(FAILED(hr)) { std::wcerr<<L"[FAIL] chargement shaders 0x"<<std::hex<<hr<<std::endl;  return 31; }

    const std::wstring inputDir=JoinPath(root,L"corpus\\current\\PTAR_PERCEPTUAL_V1_B_GRID\\input_lr");
    const std::wstring expectedDir=JoinPath(root,L"benchmarks\\results\\current\\EDGE_NG_V03_K185\\K185_SEMANTIC_FLOAT32_OUTPUTS");
    const std::wstring gpuOutDir=JoinPath(outDir,L"gpu_outputs");
    EnsureDirectoryRecursive(gpuOutDir);

    std::vector<std::wstring> ids=EnumerateCases(inputDir);
    if(ids.size()!=42u)
    {
        std::wcerr<<L"[FAIL] Corpus: 42 cas attendus, "<<ids.size()<<L" trouves."<<std::endl;
        
        return 32;
    }

    std::wstring parityPath=JoinPath(outDir,L"parity.csv");
    FILE* parity=0;
    _wfopen_s(&parity,parityPath.c_str(),L"wb");
    if(!parity) {  return 33; }
    fprintf(parity,"case_id,max_abs_lsb,mismatched_pixels,pixels,mismatch_fraction,gpu_png_bytes,status\r\n");

    UINT globalMax=0;
    unsigned long long totalPixels=0,totalMismatch=0;
    UINT failedCases=0;
    for(size_t ci=0;ci<ids.size();++ci)
    {
        const std::wstring& id=ids[ci];
        ImageRGBA input,expected,got;
        hr=LoadPngRGBA(wic.Get(),JoinPath(inputDir,id+L"_LR.png"),input);
        if(FAILED(hr)) { fclose(parity);  return 34; }
        hr=LoadPngRGBA(wic.Get(),JoinPath(expectedDir,id+L".png"),expected);
        if(FAILED(hr)) { fclose(parity);  return 35; }
        hr=gpu.RenderK185(input,expected.width,expected.height,got);
        if(FAILED(hr)) { fclose(parity);  return 36; }
        if(got.width!=expected.width || got.height!=expected.height) { fclose(parity);  return 37; }

        UINT maxLsb=0;
        unsigned long long mismatch=0;
        const size_t pixels=(size_t)got.width*got.height;
        for(size_t p=0;p<pixels;++p)
        {
            bool bad=false;
            for(int c=0;c<3;++c)
            {
                int d=(int)got.pixels[p*4+c]-(int)expected.pixels[p*4+c];
                if(d<0) d=-d;
                if((UINT)d>maxLsb) maxLsb=(UINT)d;
                if(d!=0) bad=true;
            }
            if(bad) ++mismatch;
        }
        globalMax=(std::max)(globalMax,maxLsb);
        totalPixels+=(unsigned long long)pixels;
        totalMismatch+=mismatch;
        bool pass=maxLsb<=PTAR_PARITY_MAX_LSB;
        if(!pass) ++failedCases;
        double frac=pixels?((double)mismatch/(double)pixels):0.0;
        std::string id8;
        if (!WideToUtf8(id,id8))
        {
            fclose(parity);
            std::wcerr<<L"[FAIL] UTF-16 -> UTF-8 conversion failed for case id: "<<id<<std::endl;
            
            return 47;
        }
        // Keep every GPU output for visual/forensic comparison. Persistence is
        // now a hard validation gate: a zero-byte or failed PNG is not ignored.
        const std::wstring gpuPngPath=JoinPath(gpuOutDir,id+L".png");
        unsigned long long gpuPngBytes=0;
        hr=SavePngRGBA(wic.Get(),gpuPngPath,got,&gpuPngBytes);
        if(FAILED(hr) || gpuPngBytes==0)
        {
            fclose(parity);
            std::wcerr<<L"[FAIL] GPU PNG persistence failed for "<<id
                      <<L" HRESULT=0x"<<std::hex<<hr<<std::dec
                      <<L" bytes="<<gpuPngBytes<<std::endl;
            return 48;
        }

        fprintf(parity,"%s,%u,%llu,%llu,%.12f,%llu,%s\r\n",
                id8.c_str(),maxLsb,mismatch,(unsigned long long)pixels,frac,
                gpuPngBytes,pass?"PASS":"FAIL");

        std::wcout<<L"[PARITY] "<<id<<L" maxLSB="<<maxLsb
                  <<L" mismatch="<<std::fixed<<std::setprecision(6)<<(frac*100.0)
                  <<L"% pngBytes="<<gpuPngBytes
                  <<(pass?L" PASS":L" FAIL")<<std::endl;
    }
    fclose(parity);

    TimingStats tk,tb;
    std::vector<TimingPair> timingPairs;
    hr=gpu.Benchmark(1280,720,1920,1080,tk,tb,timingPairs);
    if(FAILED(hr))
    {
        const HRESULT removed=gpu.DeviceRemovedReason();
        std::wcerr<<L"[FAIL] Timing D3D11 borne: HRESULT=0x"<<std::hex<<hr
                  <<L" deviceReason=0x"<<removed<<std::endl;
        
        return 50;
    }
    WriteTimingCsv(JoinPath(outDir,L"timing.csv"),tk,tb);
    WriteTimingPairsCsv(JoinPath(outDir,L"timing_pairs.csv"),timingPairs);

    double pairedMeanDelta=0.0,pairedMedianDelta=0.0;
    UINT pairedK185Faster=0,pairedBilinearFaster=0,pairedTies=0;
    ComputePairedDeltaSummary(
        timingPairs,pairedMeanDelta,pairedMedianDelta,
        pairedK185Faster,pairedBilinearFaster,pairedTies);

    FILE* summary=0;
    _wfopen_s(&summary,JoinPath(outDir,L"hardware_summary.txt").c_str(),L"wb");
    if(summary)
    {
        std::string adapter8;
        if (!WideToUtf8(adapter,adapter8))
            adapter8="<UTF8_CONVERSION_FAILED>";
        fprintf(summary,"PTAR EDGE-NG v03 K185 HARDWARE VALIDATION\r\n");
        fprintf(summary,"adapter=%s\r\n",adapter8.c_str());
        fprintf(summary,"parity_cases=42\r\n");
        fprintf(summary,"parity_failed_cases=%u\r\n",failedCases);
        fprintf(summary,"parity_global_max_abs_lsb=%u\r\n",globalMax);
        fprintf(summary,"parity_global_mismatch_fraction=%.12f\r\n",totalPixels?((double)totalMismatch/(double)totalPixels):0.0);
        fprintf(summary,"k185_median_ms=%.9f\r\n",tk.median);
        fprintf(summary,"k185_p95_ms=%.9f\r\n",tk.p95);
        fprintf(summary,"k185_p99_ms=%.9f\r\n",tk.p99);
        fprintf(summary,"bilinear_median_ms=%.9f\r\n",tb.median);
        fprintf(summary,"median_delta_ms=%.9f\r\n",tk.median-tb.median);
        fprintf(summary,"paired_samples=%u\r\n",(UINT)timingPairs.size());
        fprintf(summary,"paired_mean_delta_ms=%.9f\r\n",pairedMeanDelta);
        fprintf(summary,"paired_median_delta_ms=%.9f\r\n",pairedMedianDelta);
        fprintf(summary,"paired_k185_faster_count=%u\r\n",pairedK185Faster);
        fprintf(summary,"paired_bilinear_faster_count=%u\r\n",pairedBilinearFaster);
        fprintf(summary,"paired_tie_count=%u\r\n",pairedTies);
        fprintf(summary,"gpu_png_persistence=PASS_42_NONZERO\r\n");
        fclose(summary);
    }

    std::wcout<<L"[RESULT] parity maxLSB="<<globalMax<<L" failedCases="<<failedCases<<std::endl;
    std::wcout<<L"[TIMING] K185 median="<<tk.median<<L" ms P95="<<tk.p95<<L" P99="<<tk.p99<<std::endl;
    std::wcout<<L"[TIMING] bilinear median="<<tb.median<<L" ms delta="<<(tk.median-tb.median)<<L" ms"<<std::endl;
    std::wcout<<L"[TIMING-PAIRED] meanDelta="<<pairedMeanDelta
              <<L" ms medianDelta="<<pairedMedianDelta
              <<L" ms K185faster="<<pairedK185Faster
              <<L" bilinearFaster="<<pairedBilinearFaster
              <<L" ties="<<pairedTies<<std::endl;
    std::wcout<<L"[PNG] 42/42 GPU forensic PNG outputs saved and verified non-zero."<<std::endl;
    std::wcout<<L"[CLEANUP] Validation complete; COM/D3D resources will be released before COM apartment shutdown."<<std::endl;

    return failedCases==0u ? 0 : 40;
}
