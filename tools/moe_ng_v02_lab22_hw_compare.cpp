// PTAR-NG MoE v02 LAB22 hardware comparator.
// Windows 8.1 / D3D11 FL11_0 / precompiled DXBC only.
// Measures LAB07, LAB22 and K185 in six rotating orders so every shader occupies
// every timing position equally. This makes LAB22-vs-LAB07 deltas robust against
// the P-state/thermal regime changes observed in the first GTX 960M LAB07 run.

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
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "PTARD3D11GpuTimerRing.h"

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"bcrypt.lib")

using Microsoft::WRL::ComPtr;

static const UINT NVIDIA_VENDOR_ID=0x10DEu;
static const UINT WARMUP_TRIADS=60u;
static const UINT SAMPLE_TRIADS=600u; // divisible by six permutations
static const DWORD QUERY_TIMEOUT_MS=1000u;

static const char* VS_SHA="6328bbd87aac73b07d6112de593f781b2769381aae65fa9c2bcfa06fdc68585c";
static const char* LAB07_SHA="caa7352d84c1a9ea840ff0f783323478a4f1532a949a1d3640ba4c04b441ebd1";
static const char* LAB22_SHA="148a26f453a49fa319ea7aa7a448ef30b01dece508fccc6bd21f4fdae10926b8";
static const char* K185_SHA="6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16";

static std::wstring JoinPath(const std::wstring& a,const std::wstring& b)
{
    if(a.empty()) return b;
    if(a[a.size()-1]==L'\\' || a[a.size()-1]==L'/') return a+b;
    return a+L"\\"+b;
}

static bool EnsureDir(const std::wstring& p)
{
    if(CreateDirectoryW(p.c_str(),0)) return true;
    return GetLastError()==ERROR_ALREADY_EXISTS;
}

static bool WideToUtf8(const std::wstring& in,std::string& out)
{
    out.clear();
    if(in.empty()) return true;
    int n=WideCharToMultiByte(CP_UTF8,0,in.c_str(),(int)in.size(),0,0,0,0);
    if(n<=0) return false;
    out.resize((size_t)n);
    return WideCharToMultiByte(CP_UTF8,0,in.c_str(),(int)in.size(),&out[0],n,0,0)==n;
}

static bool ReadBinary(const std::wstring& path,std::vector<unsigned char>& out)
{
    FILE* f=0;
    if(_wfopen_s(&f,path.c_str(),L"rb")!=0 || !f) return false;
    _fseeki64(f,0,SEEK_END);
    __int64 n=_ftelli64(f);
    _fseeki64(f,0,SEEK_SET);
    if(n<=0){ fclose(f); return false; }
    out.resize((size_t)n);
    size_t got=fread(&out[0],1,(size_t)n,f);
    fclose(f);
    return got==(size_t)n;
}

static std::string HexLower(const unsigned char* b,size_t n)
{
    static const char* h="0123456789abcdef";
    std::string s(n*2u,'0');
    for(size_t i=0;i<n;++i){ s[i*2u]=h[(b[i]>>4)&15u]; s[i*2u+1u]=h[b[i]&15u]; }
    return s;
}

static HRESULT Sha256File(const std::wstring& path,std::string& out)
{
    std::vector<unsigned char> data;
    if(!ReadBinary(path,data)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    BCRYPT_ALG_HANDLE alg=0; BCRYPT_HASH_HANDLE hash=0;
    DWORD objBytes=0,cb=0,hashBytes=0;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,0,0)<0) return E_FAIL;
    if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objBytes,sizeof(objBytes),&cb,0)<0 ||
       BCryptGetProperty(alg,BCRYPT_HASH_LENGTH,(PUCHAR)&hashBytes,sizeof(hashBytes),&cb,0)<0 || hashBytes!=32u)
    { BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
    std::vector<unsigned char> obj(objBytes),dig(hashBytes);
    if(BCryptCreateHash(alg,&hash,&obj[0],objBytes,0,0,0)<0)
    { BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
    size_t off=0;
    while(off<data.size())
    {
        ULONG n=(ULONG)((std::min)(data.size()-off,(size_t)0x40000000u));
        if(BCryptHashData(hash,&data[off],n,0)<0)
        { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
        off+=n;
    }
    NTSTATUS st=BCryptFinishHash(hash,&dig[0],hashBytes,0);
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
    if(st<0) return E_FAIL;
    out=HexLower(&dig[0],dig.size());
    return S_OK;
}

static HRESULT VerifySha(const std::wstring& path,const char* expected,const char* label)
{
    std::string got; HRESULT hr=Sha256File(path,got);
    if(FAILED(hr)) return hr;
    if(got!=expected)
    {
        std::cerr<<"[FAIL] SHA "<<label<<" expected="<<expected<<" got="<<got<<std::endl;
        return HRESULT_FROM_WIN32(ERROR_CRC);
    }
    std::cout<<"[PASS] SHA "<<label<<"="<<got<<std::endl;
    return S_OK;
}

struct Stats
{
    double median,p90,p95,p99,maximum,mean;
    Stats():median(0),p90(0),p95(0),p99(0),maximum(0),mean(0){}
};

struct Triad
{
    UINT index,permutation;
    double lab07,lab22,k185;
    Triad():index(0),permutation(0),lab07(0),lab22(0),k185(0){}
};

static double Percentile(std::vector<double> v,double p)
{
    if(v.empty()) return 0.0;
    std::sort(v.begin(),v.end());
    double pos=(v.size()-1)*p; size_t lo=(size_t)pos; size_t hi=(lo+1<v.size())?lo+1:lo;
    return v[lo]+(v[hi]-v[lo])*(pos-(double)lo);
}

static Stats MakeStats(const std::vector<double>& v)
{
    Stats s; if(v.empty()) return s;
    s.median=Percentile(v,0.50); s.p90=Percentile(v,0.90); s.p95=Percentile(v,0.95); s.p99=Percentile(v,0.99);
    s.maximum=*std::max_element(v.begin(),v.end());
    s.mean=std::accumulate(v.begin(),v.end(),0.0)/(double)v.size();
    return s;
}

class Harness
{
public:
    Harness():m_fl(D3D_FEATURE_LEVEL_9_1){}
    ~Harness(){ if(m_ctx) m_ctx->ClearState(); }

    HRESULT Initialize(std::wstring& adapterName)
    {
        ComPtr<IDXGIFactory1> fac; HRESULT hr=CreateDXGIFactory1(__uuidof(IDXGIFactory1),&fac); if(FAILED(hr)) return hr;
        ComPtr<IDXGIAdapter1> chosen; DXGI_ADAPTER_DESC1 desc={};
        for(UINT i=0;;++i)
        {
            ComPtr<IDXGIAdapter1> a; if(fac->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 d={}; a->GetDesc1(&d);
            std::wcout<<L"[GPU] ["<<i<<L"] "<<d.Description<<L" vendor=0x"<<std::hex<<d.VendorId<<std::dec<<std::endl;
            if(!chosen && d.VendorId==NVIDIA_VENDOR_ID){ chosen=a; desc=d; }
        }
        if(!chosen) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        D3D_FEATURE_LEVEL req[]={D3D_FEATURE_LEVEL_11_0};
        hr=D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,0,0,req,1,D3D11_SDK_VERSION,&m_dev,&m_fl,&m_ctx);
        if(FAILED(hr) || m_fl<D3D_FEATURE_LEVEL_11_0) return FAILED(hr)?hr:E_FAIL;
        adapterName=desc.Description;

        D3D11_SAMPLER_DESC sd={}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        hr=m_dev->CreateSamplerState(&sd,&m_sampler); if(FAILED(hr)) return hr;
        D3D11_RASTERIZER_DESC rd={}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        hr=m_dev->CreateRasterizerState(&rd,&m_raster); if(FAILED(hr)) return hr;
        D3D11_DEPTH_STENCIL_DESC dd={}; dd.DepthEnable=FALSE; dd.StencilEnable=FALSE;
        hr=m_dev->CreateDepthStencilState(&dd,&m_depth); if(FAILED(hr)) return hr;
        D3D11_BUFFER_DESC bd={}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        return m_dev->CreateBuffer(&bd,0,&m_cb);
    }

    HRESULT Load(const std::wstring& bundle)
    {
        std::wstring d=JoinPath(bundle,L"shaders");
        std::wstring vs=JoinPath(d,L"ptar_vs.cso");
        std::wstring p07=JoinPath(d,L"ptar_moe_ng_v02_mc_native_detail_ps.cso");
        std::wstring p22=JoinPath(d,L"ptar_moe_ng_v02_lab22_phase_fused_mc_ps.cso");
        std::wstring pk=JoinPath(d,L"ptar_k185_control_ps.cso");
        HRESULT hr=VerifySha(vs,VS_SHA,"VS"); if(FAILED(hr)) return hr;
        hr=VerifySha(p07,LAB07_SHA,"LAB07"); if(FAILED(hr)) return hr;
        hr=VerifySha(p22,LAB22_SHA,"LAB22"); if(FAILED(hr)) return hr;
        hr=VerifySha(pk,K185_SHA,"K185"); if(FAILED(hr)) return hr;
        std::vector<unsigned char> bvs,b07,b22,bk;
        if(!ReadBinary(vs,bvs)||!ReadBinary(p07,b07)||!ReadBinary(p22,b22)||!ReadBinary(pk,bk)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if(bvs.size()<4||b07.size()<4||b22.size()<4||bk.size()<4 || memcmp(&bvs[0],"DXBC",4)||memcmp(&b07[0],"DXBC",4)||memcmp(&b22[0],"DXBC",4)||memcmp(&bk[0],"DXBC",4)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        hr=m_dev->CreateVertexShader(&bvs[0],bvs.size(),0,&m_vs); if(FAILED(hr)) return hr;
        hr=m_dev->CreatePixelShader(&b07[0],b07.size(),0,&m_lab07); if(FAILED(hr)) return hr;
        hr=m_dev->CreatePixelShader(&b22[0],b22.size(),0,&m_lab22); if(FAILED(hr)) return hr;
        return m_dev->CreatePixelShader(&bk[0],bk.size(),0,&m_k185);
    }

    HRESULT Run(std::vector<Triad>& out)
    {
        const UINT inW=1280,inH=720,outW=1920,outH=1080;
        std::vector<unsigned char> pixels((size_t)inW*inH*4u);
        for(UINT y=0;y<inH;++y) for(UINT x=0;x<inW;++x)
        {
            size_t i=((size_t)y*inW+x)*4u;
            pixels[i]=(BYTE)((x*17u+y*13u+(x^y))&255u);
            pixels[i+1]=(BYTE)((x*5u+y*29u+((x*3u)^(y*7u)))&255u);
            pixels[i+2]=(BYTE)((x*31u+y*3u+(x+y)*11u)&255u); pixels[i+3]=255u;
        }
        D3D11_TEXTURE2D_DESC td={}; td.Width=inW; td.Height=inH; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init={}; init.pSysMem=&pixels[0]; init.SysMemPitch=inW*4u;
        ComPtr<ID3D11Texture2D> input; ComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr=m_dev->CreateTexture2D(&td,&init,&input); if(FAILED(hr)) return hr;
        hr=m_dev->CreateShaderResourceView(input.Get(),0,&srv); if(FAILED(hr)) return hr;
        td.Width=outW; td.Height=outH; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> output; ComPtr<ID3D11RenderTargetView> rtv;
        hr=m_dev->CreateTexture2D(&td,0,&output); if(FAILED(hr)) return hr;
        hr=m_dev->CreateRenderTargetView(output.Get(),0,&rtv); if(FAILED(hr)) return hr;
        PTARD3D11GpuTimerRing timer; hr=timer.Initialize(m_dev.Get()); if(FAILED(hr)) return hr;

        static const int perm[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for(UINT i=0;i<WARMUP_TRIADS;++i)
        {
            double v[3]={0,0,0};
            for(int pos=0;pos<3;++pos){ int id=perm[i%6u][pos]; hr=Timed(timer,srv.Get(),rtv.Get(),inW,inH,outW,outH,Shader(id),v[id]); if(FAILED(hr)) return hr; }
        }
        out.clear(); out.reserve(SAMPLE_TRIADS);
        for(UINT i=0;i<SAMPLE_TRIADS;++i)
        {
            Triad t; t.index=i; t.permutation=i%6u; double v[3]={0,0,0};
            for(int pos=0;pos<3;++pos){ int id=perm[t.permutation][pos]; hr=Timed(timer,srv.Get(),rtv.Get(),inW,inH,outW,outH,Shader(id),v[id]); if(FAILED(hr)) return hr; }
            t.lab07=v[0]; t.lab22=v[1]; t.k185=v[2]; out.push_back(t);
        }
        ID3D11ShaderResourceView* nullSrv=0; m_ctx->PSSetShaderResources(0,1,&nullSrv);
        return S_OK;
    }

    HRESULT DeviceReason() const { return m_dev?m_dev->GetDeviceRemovedReason():E_POINTER; }

private:
    struct Constants{ float inSize[2],outSize[2]; };
    ID3D11PixelShader* Shader(int id){ return id==0?m_lab07.Get():(id==1?m_lab22.Get():m_k185.Get()); }

    void Pipeline(ID3D11ShaderResourceView* srv,ID3D11RenderTargetView* rtv,UINT iw,UINT ih,UINT ow,UINT oh,ID3D11PixelShader* ps)
    {
        Constants c={{(float)iw,(float)ih},{(float)ow,(float)oh}}; m_ctx->UpdateSubresource(m_cb.Get(),0,0,&c,0,0);
        D3D11_VIEWPORT vp={0,0,(FLOAT)ow,(FLOAT)oh,0,1}; m_ctx->RSSetViewports(1,&vp); m_ctx->RSSetState(m_raster.Get()); m_ctx->OMSetDepthStencilState(m_depth.Get(),0); m_ctx->OMSetRenderTargets(1,&rtv,0);
        m_ctx->IASetInputLayout(0); m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); m_ctx->VSSetShader(m_vs.Get(),0,0); m_ctx->PSSetShader(ps,0,0); m_ctx->PSSetShaderResources(0,1,&srv);
        ID3D11SamplerState* s=m_sampler.Get(); m_ctx->PSSetSamplers(0,1,&s); ID3D11Buffer* cb=m_cb.Get(); m_ctx->PSSetConstantBuffers(0,1,&cb);
    }

    HRESULT Wait(PTARD3D11GpuTimerRing& timer,double& ms,bool& valid)
    {
        ULONGLONG start=GetTickCount64();
        for(;;)
        {
            bool ready=false; valid=false; HRESULT hr=timer.TryResolveEx(m_ctx.Get(),&ms,0u,&ready,&valid); if(FAILED(hr)) return hr; if(ready) return S_OK;
            HRESULT dr=m_dev->GetDeviceRemovedReason(); if(FAILED(dr)) return dr; if(GetTickCount64()-start>=QUERY_TIMEOUT_MS) return HRESULT_FROM_WIN32(ERROR_TIMEOUT); Sleep(1);
        }
    }

    HRESULT Timed(PTARD3D11GpuTimerRing& timer,ID3D11ShaderResourceView* srv,ID3D11RenderTargetView* rtv,UINT iw,UINT ih,UINT ow,UINT oh,ID3D11PixelShader* ps,double& ms)
    {
        Pipeline(srv,rtv,iw,ih,ow,oh,ps);
        for(;;)
        {
            if(!timer.Begin(m_ctx.Get())) return E_UNEXPECTED; m_ctx->Draw(3,0); if(!timer.End(m_ctx.Get())) return E_UNEXPECTED;
            bool valid=false; HRESULT hr=Wait(timer,ms,valid); if(FAILED(hr)) return hr; if(valid) return S_OK;
        }
    }

    ComPtr<ID3D11Device> m_dev; ComPtr<ID3D11DeviceContext> m_ctx; D3D_FEATURE_LEVEL m_fl;
    ComPtr<ID3D11VertexShader> m_vs; ComPtr<ID3D11PixelShader> m_lab07,m_lab22,m_k185;
    ComPtr<ID3D11SamplerState> m_sampler; ComPtr<ID3D11RasterizerState> m_raster; ComPtr<ID3D11DepthStencilState> m_depth; ComPtr<ID3D11Buffer> m_cb;
};

static int Usage()
{
    std::wcerr<<L"Usage: moe_ng_v02_lab22_hw_compare.exe --bundle-root <bundle> --out <results_dir>"<<std::endl; return 2;
}

int wmain(int argc,wchar_t** argv)
{
    std::wstring bundle,outDir;
    for(int i=1;i<argc;++i)
    {
        std::wstring a=argv[i];
        if(a==L"--bundle-root" && i+1<argc) bundle=argv[++i];
        else if(a==L"--out" && i+1<argc) outDir=argv[++i];
        else return Usage();
    }
    if(bundle.empty()||outDir.empty()) return Usage();
    if(!EnsureDir(outDir)){ std::wcerr<<L"[FAIL] output directory"<<std::endl; return 3; }

    Harness h; std::wstring adapter; HRESULT hr=h.Initialize(adapter);
    if(FAILED(hr)){ std::wcerr<<L"[FAIL] NVIDIA D3D11 init HRESULT=0x"<<std::hex<<hr<<std::endl; return 20; }
    hr=h.Load(bundle); if(FAILED(hr)){ std::wcerr<<L"[FAIL] shader load HRESULT=0x"<<std::hex<<hr<<std::endl; return 21; }
    std::vector<Triad> triads; hr=h.Run(triads);
    if(FAILED(hr)){ std::wcerr<<L"[FAIL] timing HRESULT=0x"<<std::hex<<hr<<L" deviceReason=0x"<<h.DeviceReason()<<std::endl; return 22; }
    if(triads.size()!=SAMPLE_TRIADS) return 23;

    std::vector<double> a,b,k,d2207,d07k,d22k; a.reserve(triads.size()); b.reserve(triads.size()); k.reserve(triads.size()); d2207.reserve(triads.size()); d07k.reserve(triads.size()); d22k.reserve(triads.size());
    UINT lab22Faster=0,lab07Faster=0,ties=0;
    for(size_t i=0;i<triads.size();++i)
    {
        const Triad& t=triads[i]; a.push_back(t.lab07); b.push_back(t.lab22); k.push_back(t.k185); d2207.push_back(t.lab22-t.lab07); d07k.push_back(t.lab07-t.k185); d22k.push_back(t.lab22-t.k185);
        if(t.lab22<t.lab07) ++lab22Faster; else if(t.lab07<t.lab22) ++lab07Faster; else ++ties;
    }
    Stats s07=MakeStats(a),s22=MakeStats(b),sk=MakeStats(k),sd=MakeStats(d2207),sd07k=MakeStats(d07k),sd22k=MakeStats(d22k);

    FILE* f=0; std::wstring csv=JoinPath(outDir,L"compare_triads.csv"); _wfopen_s(&f,csv.c_str(),L"wb"); if(!f) return 24;
    fprintf(f,"sample,permutation,lab07_ms,lab22_ms,k185_ms,delta_lab22_minus_lab07_ms,delta_lab07_minus_k185_ms,delta_lab22_minus_k185_ms\r\n");
    for(size_t i=0;i<triads.size();++i)
    {
        const Triad& t=triads[i]; fprintf(f,"%u,%u,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\r\n",t.index,t.permutation,t.lab07,t.lab22,t.k185,t.lab22-t.lab07,t.lab07-t.k185,t.lab22-t.k185);
    }
    fclose(f);

    std::string adapter8; WideToUtf8(adapter,adapter8);
    std::wstring sp=JoinPath(outDir,L"compare_summary.txt"); _wfopen_s(&f,sp.c_str(),L"wb"); if(!f) return 25;
    fprintf(f,"PTAR-NG MoE v02 LAB22 SAME-RUN HARDWARE COMPARISON\r\n");
    fprintf(f,"adapter=%s\r\n",adapter8.c_str());
    fprintf(f,"samples=%u\r\n",SAMPLE_TRIADS);
    fprintf(f,"order_protocol=SIX_ROTATING_PERMUTATIONS_BALANCED\r\n");
    fprintf(f,"lab07_median_ms=%.9f\r\nlab07_p95_ms=%.9f\r\n",s07.median,s07.p95);
    fprintf(f,"lab22_median_ms=%.9f\r\nlab22_p95_ms=%.9f\r\n",s22.median,s22.p95);
    fprintf(f,"k185_median_ms=%.9f\r\nk185_p95_ms=%.9f\r\n",sk.median,sk.p95);
    fprintf(f,"paired_lab22_minus_lab07_mean_ms=%.9f\r\n",sd.mean);
    fprintf(f,"paired_lab22_minus_lab07_median_ms=%.9f\r\n",sd.median);
    fprintf(f,"paired_lab22_minus_lab07_p95_ms=%.9f\r\n",sd.p95);
    fprintf(f,"lab22_faster_count=%u\r\nlab07_faster_count=%u\r\ntie_count=%u\r\n",lab22Faster,lab07Faster,ties);
    fprintf(f,"paired_lab07_minus_k185_median_ms=%.9f\r\n",sd07k.median);
    fprintf(f,"paired_lab22_minus_k185_median_ms=%.9f\r\n",sd22k.median);
    fprintf(f,"shader_contract=LAB07_AND_LAB22_1_GATHER4_4_SAMPLE_L_0_UAV\r\n");
    fprintf(f,"runtime_shader_mode=PRECOMPILED_DXBC_ONLY\r\n");
    fclose(f);

    std::cout<<std::fixed<<std::setprecision(6)
             <<"[COMPARE] LAB07 median="<<s07.median<<" ms LAB22 median="<<s22.median<<" ms K185 median="<<sk.median<<" ms\n"
             <<"[PAIRED] LAB22-LAB07 median="<<sd.median<<" ms mean="<<sd.mean<<" ms; LAB22 faster "<<lab22Faster<<"/"<<SAMPLE_TRIADS<<"\n"
             <<"[PASS] same-run tri-shader comparison complete."<<std::endl;
    return 0;
}
