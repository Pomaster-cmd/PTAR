// PTAR-NG MoE v02 LAB18 autonomous runtime bench host
// Windows 8.1 x64 / D3D11 FL11_0 / precompiled DXBC only.
// No D3DCompiler API. No NIS. No FG/NVENC logic.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <bcrypt.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "PTARD3D11GpuTimerRing.h"
#include "PTARV02GeneratedHashes.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "bcrypt.lib")

using Microsoft::WRL::ComPtr;

static const UINT PTAR_NVIDIA_VENDOR_ID = 0x10DEu;
static const DWORD PTAR_QUERY_WAIT_TIMEOUT_MS = 1000u;

struct TimingStats
{
    double median,p90,p95,p99,maximum;
    UINT validSamples;
    TimingStats() : median(0),p90(0),p95(0),p99(0),maximum(0),validSamples(0) {}
};

struct TimingRow
{
    UINT sampleIndex;
    int order[3];
    double ms[3];
};

static std::wstring JoinPath(const std::wstring& a,const std::wstring& b)
{
    if(a.empty()) return b;
    const wchar_t c=a[a.size()-1];
    return (c==L'\\' || c==L'/') ? a+b : a+L"\\"+b;
}

static bool DirectoryExists(const std::wstring& p)
{
    const DWORD a=GetFileAttributesW(p.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY)!=0;
}

static bool EnsureDirectoryRecursive(const std::wstring& path)
{
    if(path.empty() || DirectoryExists(path)) return true;
    std::wstring parent=path;
    const size_t pos=parent.find_last_of(L"\\/");
    if(pos!=std::wstring::npos)
    {
        parent.resize(pos);
        if(!parent.empty() && !DirectoryExists(parent) && !EnsureDirectoryRecursive(parent)) return false;
    }
    if(CreateDirectoryW(path.c_str(),0)) return true;
    return GetLastError()==ERROR_ALREADY_EXISTS;
}

static bool ReadBinaryFile(const std::wstring& path,std::vector<unsigned char>& out)
{
    FILE* f=0;
    if(_wfopen_s(&f,path.c_str(),L"rb")!=0 || !f) return false;
    _fseeki64(f,0,SEEK_END);
    const __int64 n=_ftelli64(f);
    _fseeki64(f,0,SEEK_SET);
    if(n<=0) { fclose(f); return false; }
    out.resize((size_t)n);
    const size_t got=fread(&out[0],1,(size_t)n,f);
    fclose(f);
    return got==(size_t)n;
}

static std::string HexLower(const unsigned char* bytes,size_t n)
{
    static const char* h="0123456789abcdef";
    std::string out(n*2u,'0');
    for(size_t i=0;i<n;++i)
    {
        out[i*2u]=h[(bytes[i]>>4)&15u];
        out[i*2u+1u]=h[bytes[i]&15u];
    }
    return out;
}

static HRESULT Sha256File(const std::wstring& path,std::string& hex)
{
    std::vector<unsigned char> data;
    if(!ReadBinaryFile(path,data)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    BCRYPT_ALG_HANDLE alg=0;
    BCRYPT_HASH_HANDLE hash=0;
    DWORD objectBytes=0,hashBytes=0,cb=0;
    NTSTATUS st=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,0,0);
    if(st<0) return E_FAIL;
    st=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectBytes,sizeof(objectBytes),&cb,0);
    if(st<0) { BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
    st=BCryptGetProperty(alg,BCRYPT_HASH_LENGTH,(PUCHAR)&hashBytes,sizeof(hashBytes),&cb,0);
    if(st<0 || hashBytes!=32u) { BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }

    std::vector<unsigned char> object(objectBytes),digest(hashBytes);
    st=BCryptCreateHash(alg,&hash,&object[0],objectBytes,0,0,0);
    if(st<0) { BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
    size_t off=0;
    while(off<data.size())
    {
        const size_t remain=data.size()-off;
        const ULONG chunk=(ULONG)((std::min)(remain,(size_t)0x40000000u));
        st=BCryptHashData(hash,&data[off],chunk,0);
        if(st<0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
        off+=chunk;
    }
    st=BCryptFinishHash(hash,&digest[0],hashBytes,0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg,0);
    if(st<0) return E_FAIL;
    hex=HexLower(&digest[0],digest.size());
    return S_OK;
}

static HRESULT VerifySha(const std::wstring& path,const char* expected,const char* label)
{
    std::string got;
    HRESULT hr=Sha256File(path,got);
    if(FAILED(hr)) return hr;
    if(got!=expected)
    {
        std::cerr<<"[FAIL] SHA-256 "<<label<<" expected="<<expected<<" got="<<got<<std::endl;
        return HRESULT_FROM_WIN32(ERROR_CRC);
    }
    std::cout<<"[PASS] SHA-256 "<<label<<" = "<<got<<std::endl;
    return S_OK;
}

static unsigned long long Fnv1a64(const unsigned char* p,size_t n)
{
    unsigned long long h=1469598103934665603ull;
    for(size_t i=0;i<n;++i) { h^=(unsigned long long)p[i]; h*=1099511628211ull; }
    return h;
}

static double Percentile(std::vector<double> v,double p)
{
    if(v.empty()) return 0.0;
    std::sort(v.begin(),v.end());
    const double pos=(v.size()-1)*p;
    const size_t lo=(size_t)pos;
    const size_t hi=(lo+1<v.size())?lo+1:lo;
    const double t=pos-(double)lo;
    return v[lo]+(v[hi]-v[lo])*t;
}

static void FillStats(const std::vector<double>& v,TimingStats& s)
{
    s.validSamples=(UINT)v.size();
    s.median=Percentile(v,0.50);
    s.p90=Percentile(v,0.90);
    s.p95=Percentile(v,0.95);
    s.p99=Percentile(v,0.99);
    s.maximum=v.empty()?0.0:*std::max_element(v.begin(),v.end());
}

class Harness
{
public:
    Harness() : m_featureLevel(D3D_FEATURE_LEVEL_9_1) {}
    ~Harness() { if(m_context) m_context->ClearState(); }

    HRESULT Initialize(bool warp,bool allowAny,std::wstring& adapterName)
    {
        D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_0};
        HRESULT hr=E_FAIL;
        if(warp)
        {
            hr=D3D11CreateDevice(0,D3D_DRIVER_TYPE_WARP,0,0,requested,1,D3D11_SDK_VERSION,
                                 &m_device,&m_featureLevel,&m_context);
            adapterName=L"D3D11 WARP";
        }
        else
        {
            ComPtr<IDXGIFactory1> factory;
            hr=CreateDXGIFactory1(__uuidof(IDXGIFactory1),&factory);
            if(FAILED(hr)) return hr;
            ComPtr<IDXGIAdapter1> chosen;
            DXGI_ADAPTER_DESC1 chosenDesc={};
            for(UINT i=0;;++i)
            {
                ComPtr<IDXGIAdapter1> a;
                if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 d={};
                a->GetDesc1(&d);
                std::wcout<<L"[GPU] ["<<i<<L"] "<<d.Description<<L" vendor=0x"<<std::hex<<d.VendorId<<std::dec<<std::endl;
                if(!chosen && d.VendorId==PTAR_NVIDIA_VENDOR_ID) { chosen=a; chosenDesc=d; }
                if(!chosen && allowAny) { chosen=a; chosenDesc=d; }
            }
            if(!chosen) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            hr=D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,0,0,requested,1,D3D11_SDK_VERSION,
                                 &m_device,&m_featureLevel,&m_context);
            adapterName=chosenDesc.Description;
        }
        if(FAILED(hr)) return hr;
        if(m_featureLevel<D3D_FEATURE_LEVEL_11_0) return E_FAIL;

        D3D11_SAMPLER_DESC sd={};
        sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD=D3D11_FLOAT32_MAX;
        hr=m_device->CreateSamplerState(&sd,&m_sampler);
        if(FAILED(hr)) return hr;

        D3D11_RASTERIZER_DESC rd={};
        rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        hr=m_device->CreateRasterizerState(&rd,&m_raster);
        if(FAILED(hr)) return hr;

        D3D11_DEPTH_STENCIL_DESC dd={};
        dd.DepthEnable=FALSE; dd.StencilEnable=FALSE;
        hr=m_device->CreateDepthStencilState(&dd,&m_depthOff);
        if(FAILED(hr)) return hr;

        D3D11_BUFFER_DESC bd={};
        bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        return m_device->CreateBuffer(&bd,0,&m_constants);
    }

    HRESULT LoadShaders(const std::wstring& shaderDir)
    {
        const std::wstring vs=JoinPath(shaderDir,L"ptar_vs.cso");
        const std::wstring v02=JoinPath(shaderDir,L"ptar_moe_ng_v02_lab18_admission_ps.cso");
        const std::wstring v01=JoinPath(shaderDir,L"ptar_moe_ng_v01_sf5_ps.cso");
        const std::wstring k185=JoinPath(shaderDir,L"ptar_k185_control_ps.cso");

        HRESULT hr=VerifySha(vs,PTAR_SHA_VS,"ptar_vs.cso"); if(FAILED(hr)) return hr;
        hr=VerifySha(v02,PTAR_SHA_V02,"ptar_moe_ng_v02_lab18_admission_ps.cso"); if(FAILED(hr)) return hr;
        hr=VerifySha(v01,PTAR_SHA_V01,"ptar_moe_ng_v01_sf5_ps.cso"); if(FAILED(hr)) return hr;
        hr=VerifySha(k185,PTAR_SHA_K185,"ptar_k185_control_ps.cso"); if(FAILED(hr)) return hr;

        std::vector<unsigned char> bvs,b02,b01,bk;
        if(!ReadBinaryFile(vs,bvs)||!ReadBinaryFile(v02,b02)||!ReadBinaryFile(v01,b01)||!ReadBinaryFile(k185,bk)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if(bvs.size()<4u||b02.size()<4u||b01.size()<4u||bk.size()<4u||
           memcmp(&bvs[0],"DXBC",4)||memcmp(&b02[0],"DXBC",4)||memcmp(&b01[0],"DXBC",4)||memcmp(&bk[0],"DXBC",4))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

        hr=m_device->CreateVertexShader(&bvs[0],bvs.size(),0,&m_vs); if(FAILED(hr)) return hr;
        hr=m_device->CreatePixelShader(&b02[0],b02.size(),0,&m_ps[0]); if(FAILED(hr)) return hr;
        hr=m_device->CreatePixelShader(&b01[0],b01.size(),0,&m_ps[1]); if(FAILED(hr)) return hr;
        hr=m_device->CreatePixelShader(&bk[0],bk.size(),0,&m_ps[2]); if(FAILED(hr)) return hr;
        std::cout<<"[PASS] Four validated precompiled DXBC objects loaded; no runtime HLSL compilation."<<std::endl;
        return S_OK;
    }

    HRESULT Prepare(UINT inW,UINT inH,UINT outW,UINT outH)
    {
        m_inW=inW; m_inH=inH; m_outW=outW; m_outH=outH;
        std::vector<unsigned char> pixels((size_t)inW*(size_t)inH*4u);
        for(UINT y=0;y<inH;++y)
        for(UINT x=0;x<inW;++x)
        {
            const size_t i=((size_t)y*inW+x)*4u;
            pixels[i+0]=(unsigned char)((x*17u+y*13u+(x^y))&255u);
            pixels[i+1]=(unsigned char)((x*5u+y*29u+((x*3u)^(y*7u)))&255u);
            pixels[i+2]=(unsigned char)((x*31u+y*3u+(x+y)*11u)&255u);
            pixels[i+3]=255u;
        }
        D3D11_TEXTURE2D_DESC id={};
        id.Width=inW; id.Height=inH; id.MipLevels=1; id.ArraySize=1; id.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        id.SampleDesc.Count=1; id.Usage=D3D11_USAGE_DEFAULT; id.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init={}; init.pSysMem=&pixels[0]; init.SysMemPitch=inW*4u;
        HRESULT hr=m_device->CreateTexture2D(&id,&init,&m_input); if(FAILED(hr)) return hr;
        hr=m_device->CreateShaderResourceView(m_input.Get(),0,&m_srv); if(FAILED(hr)) return hr;

        D3D11_TEXTURE2D_DESC od={};
        od.Width=outW; od.Height=outH; od.MipLevels=1; od.ArraySize=1; od.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        od.SampleDesc.Count=1; od.Usage=D3D11_USAGE_DEFAULT; od.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr=m_device->CreateTexture2D(&od,0,&m_output); if(FAILED(hr)) return hr;
        hr=m_device->CreateRenderTargetView(m_output.Get(),0,&m_rtv); if(FAILED(hr)) return hr;
        od.Usage=D3D11_USAGE_STAGING; od.BindFlags=0; od.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        return m_device->CreateTexture2D(&od,0,&m_staging);
    }

    HRESULT SmokeHash(int which,unsigned long long& hash)
    {
        SetPipeline(m_ps[which].Get());
        m_context->Draw(3,0);
        ID3D11ShaderResourceView* nullSrv=0;
        m_context->PSSetShaderResources(0,1,&nullSrv);
        m_context->CopyResource(m_staging.Get(),m_output.Get());
        D3D11_MAPPED_SUBRESOURCE map={};
        HRESULT hr=m_context->Map(m_staging.Get(),0,D3D11_MAP_READ,0,&map);
        if(FAILED(hr)) return hr;
        hash=1469598103934665603ull;
        for(UINT y=0;y<m_outH;++y)
        {
            const unsigned char* row=(const unsigned char*)map.pData+(size_t)y*map.RowPitch;
            const unsigned long long rh=Fnv1a64(row,(size_t)m_outW*4u);
            hash^=rh; hash*=1099511628211ull;
        }
        m_context->Unmap(m_staging.Get(),0);
        return S_OK;
    }

    HRESULT Benchmark(UINT warmup,UINT samples,TimingStats stats[3],std::vector<TimingRow>& rows)
    {
        PTARD3D11GpuTimerRing timer;
        HRESULT hr=timer.Initialize(m_device.Get());
        if(FAILED(hr)) return hr;
        static const int perms[6][3]={{0,1,2},{1,2,0},{2,0,1},{0,2,1},{2,1,0},{1,0,2}};

        for(UINT i=0;i<warmup;++i)
        {
            const int* order=perms[i%6u];
            for(int j=0;j<3;++j)
            {
                double ms=0.0;
                hr=TimedDraw(timer,m_ps[order[j]].Get(),ms);
                if(FAILED(hr)) return hr;
            }
        }

        std::vector<double> samplesByShader[3];
        for(int s=0;s<3;++s) samplesByShader[s].reserve(samples);
        rows.clear(); rows.reserve(samples);
        for(UINT i=0;i<samples;++i)
        {
            TimingRow r={}; r.sampleIndex=i;
            const int* order=perms[i%6u];
            for(int j=0;j<3;++j) r.order[j]=order[j];
            for(int j=0;j<3;++j)
            {
                const int s=order[j];
                hr=TimedDraw(timer,m_ps[s].Get(),r.ms[s]);
                if(FAILED(hr)) return hr;
                samplesByShader[s].push_back(r.ms[s]);
            }
            rows.push_back(r);
        }
        for(int s=0;s<3;++s) FillStats(samplesByShader[s],stats[s]);
        ID3D11ShaderResourceView* nullSrv=0; m_context->PSSetShaderResources(0,1,&nullSrv);
        return S_OK;
    }

    HRESULT DeviceRemovedReason() const { return m_device?m_device->GetDeviceRemovedReason():E_POINTER; }

private:
    struct Constants { float inputSize[2]; float outputSize[2]; };

    void SetPipeline(ID3D11PixelShader* ps)
    {
        Constants c={{(float)m_inW,(float)m_inH},{(float)m_outW,(float)m_outH}};
        m_context->UpdateSubresource(m_constants.Get(),0,0,&c,0,0);
        D3D11_VIEWPORT vp={0,0,(FLOAT)m_outW,(FLOAT)m_outH,0,1};
        m_context->RSSetViewports(1,&vp); m_context->RSSetState(m_raster.Get());
        m_context->OMSetDepthStencilState(m_depthOff.Get(),0);
        ID3D11RenderTargetView* rt=m_rtv.Get(); m_context->OMSetRenderTargets(1,&rt,0);
        m_context->IASetInputLayout(0); m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_context->VSSetShader(m_vs.Get(),0,0); m_context->PSSetShader(ps,0,0);
        ID3D11ShaderResourceView* srv=m_srv.Get(); m_context->PSSetShaderResources(0,1,&srv);
        ID3D11SamplerState* sam=m_sampler.Get(); m_context->PSSetSamplers(0,1,&sam);
        ID3D11Buffer* cb=m_constants.Get(); m_context->PSSetConstantBuffers(0,1,&cb);
    }

    HRESULT WaitSample(PTARD3D11GpuTimerRing& timer,double& ms)
    {
        const ULONGLONG start=GetTickCount64();
        for(;;)
        {
            bool ready=false,valid=false;
            HRESULT hr=timer.TryResolveEx(m_context.Get(),&ms,0u,&ready,&valid);
            if(FAILED(hr)) return hr;
            if(ready && valid) return S_OK;
            const HRESULT dr=m_device->GetDeviceRemovedReason(); if(FAILED(dr)) return dr;
            if(GetTickCount64()-start>=PTAR_QUERY_WAIT_TIMEOUT_MS) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            Sleep(1);
        }
    }

    HRESULT TimedDraw(PTARD3D11GpuTimerRing& timer,ID3D11PixelShader* ps,double& ms)
    {
        SetPipeline(ps);
        if(!timer.Begin(m_context.Get())) return E_UNEXPECTED;
        m_context->Draw(3,0);
        if(!timer.End(m_context.Get())) return E_UNEXPECTED;
        return WaitSample(timer,ms);
    }

    UINT m_inW=0,m_inH=0,m_outW=0,m_outH=0;
    D3D_FEATURE_LEVEL m_featureLevel;
    ComPtr<ID3D11Device> m_device; ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11VertexShader> m_vs; ComPtr<ID3D11PixelShader> m_ps[3];
    ComPtr<ID3D11SamplerState> m_sampler; ComPtr<ID3D11RasterizerState> m_raster; ComPtr<ID3D11DepthStencilState> m_depthOff;
    ComPtr<ID3D11Buffer> m_constants;
    ComPtr<ID3D11Texture2D> m_input,m_output,m_staging;
    ComPtr<ID3D11ShaderResourceView> m_srv; ComPtr<ID3D11RenderTargetView> m_rtv;
};

static const char* ShaderName(int s)
{
    return s==0?"PTAR_NG_MOE_V02_LAB18":(s==1?"PTAR_NG_MOE_V01_SF5":"EDGE_NG_V03_K185_CONTROL");
}

static void WriteTimingCsv(const std::wstring& path,const TimingStats s[3])
{
    FILE* f=0; if(_wfopen_s(&f,path.c_str(),L"wb")!=0||!f) return;
    fprintf(f,"variant,samples,median_ms,p90_ms,p95_ms,p99_ms,max_ms\r\n");
    for(int i=0;i<3;++i) fprintf(f,"%s,%u,%.9f,%.9f,%.9f,%.9f,%.9f\r\n",ShaderName(i),s[i].validSamples,s[i].median,s[i].p90,s[i].p95,s[i].p99,s[i].maximum);
    fclose(f);
}

static void WritePairsCsv(const std::wstring& path,const std::vector<TimingRow>& rows)
{
    FILE* f=0; if(_wfopen_s(&f,path.c_str(),L"wb")!=0||!f) return;
    fprintf(f,"sample_index,order,v02_ms,v01_ms,k185_ms,v02_minus_v01_ms,v02_minus_k185_ms,v01_minus_k185_ms\r\n");
    for(size_t i=0;i<rows.size();++i)
    {
        const TimingRow& r=rows[i];
        std::string ord=std::string(ShaderName(r.order[0]))+">"+ShaderName(r.order[1])+">"+ShaderName(r.order[2]);
        fprintf(f,"%u,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\r\n",r.sampleIndex,ord.c_str(),r.ms[0],r.ms[1],r.ms[2],r.ms[0]-r.ms[1],r.ms[0]-r.ms[2],r.ms[1]-r.ms[2]);
    }
    fclose(f);
}

static int Usage()
{
    std::wcout<<L"Usage: ptar_v02_runtime_bench.exe --bundle-root <bundle> --out <results> [--warp] [--allow-any-adapter] [--smoke]"<<std::endl;
    return 2;
}

int wmain(int argc,wchar_t** argv)
{
    std::wstring bundleRoot,outDir; bool warp=false,allowAny=false,smoke=false;
    for(int i=1;i<argc;++i)
    {
        const std::wstring a=argv[i];
        if(a==L"--bundle-root"&&i+1<argc) bundleRoot=argv[++i];
        else if(a==L"--out"&&i+1<argc) outDir=argv[++i];
        else if(a==L"--warp") warp=true;
        else if(a==L"--allow-any-adapter") allowAny=true;
        else if(a==L"--smoke") smoke=true;
        else return Usage();
    }
    if(bundleRoot.empty()||outDir.empty()) return Usage();
    if(!EnsureDirectoryRecursive(outDir)) return 3;

    Harness h; std::wstring adapter;
    HRESULT hr=h.Initialize(warp,allowAny,adapter);
    if(FAILED(hr)) { std::wcerr<<L"[FAIL] D3D11 init 0x"<<std::hex<<hr<<std::endl; return 20; }
    std::wcout<<L"[GPU] Selection: "<<adapter<<std::endl;

    hr=h.LoadShaders(JoinPath(bundleRoot,L"shaders"));
    if(FAILED(hr)) { std::wcerr<<L"[FAIL] shader load 0x"<<std::hex<<hr<<std::endl; return 21; }

    const UINT inW=smoke?320u:1280u, inH=smoke?180u:720u;
    const UINT outW=(inW*3u)/2u, outH=(inH*3u)/2u;
    hr=h.Prepare(inW,inH,outW,outH);
    if(FAILED(hr)) { std::wcerr<<L"[FAIL] resource prep 0x"<<std::hex<<hr<<std::endl; return 22; }

    unsigned long long hashes[3]={0,0,0};
    for(int s=0;s<3;++s)
    {
        hr=h.SmokeHash(s,hashes[s]);
        if(FAILED(hr) || hashes[s]==0ull) { std::wcerr<<L"[FAIL] smoke render "<<s<<L" 0x"<<std::hex<<hr<<std::endl; return 23; }
        std::cout<<"[PASS] smoke "<<ShaderName(s)<<" fnv64=0x"<<std::hex<<hashes[s]<<std::dec<<std::endl;
    }

    const UINT warmup=smoke?6u:32u, samples=smoke?12u:512u;
    TimingStats stats[3]; std::vector<TimingRow> rows;
    hr=h.Benchmark(warmup,samples,stats,rows);
    if(FAILED(hr))
    {
        std::wcerr<<L"[FAIL] bounded D3D11 timing 0x"<<std::hex<<hr<<L" deviceReason=0x"<<h.DeviceRemovedReason()<<std::endl;
        return 24;
    }

    WriteTimingCsv(JoinPath(outDir,L"timing.csv"),stats);
    WritePairsCsv(JoinPath(outDir,L"timing_pairs.csv"),rows);
    FILE* f=0;
    if(_wfopen_s(&f,JoinPath(outDir,L"runtime_summary.txt").c_str(),L"wb")!=0||!f) return 25;
    fprintf(f,"PTAR-NG MoE v02 LAB18 AUTONOMOUS RUNTIME BENCH\r\n");
    fprintf(f,"mode=%s\r\n",warp?"WARP_SMOKE":"PHYSICAL_GPU");
    fprintf(f,"input=%ux%u\r\noutput=%ux%u\r\n",inW,inH,outW,outH);
    fprintf(f,"samples=%u\r\n",samples);
    for(int s=0;s<3;++s) fprintf(f,"%s_median_ms=%.9f\r\n%s_fnv64=%016llx\r\n",ShaderName(s),stats[s].median,ShaderName(s),hashes[s]);
    fprintf(f,"runtime_shader_mode=PRECOMPILED_DXBC_ONLY\r\nruntime_d3dcompiler_required=NO\r\n");
    fclose(f);

    std::cout<<"[PASS] autonomous runtime bench completed."<<std::endl;
    return 0;
}
