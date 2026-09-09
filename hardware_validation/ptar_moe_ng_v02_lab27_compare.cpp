// PTAR-NG MoE v02 LAB27 isolated hardware compare.
// Target: Windows 8.1 x64 / D3D11 FL11_0 / precompiled DXBC only.
// No D3DCompiler API. Timing is GPU timestamp/disjoint query based.
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

static const UINT kInW=1280u, kInH=720u, kOutW=1920u, kOutH=1080u;
static const UINT kWarmupTriads=60u;
static const UINT kSamples=600u;
static const DWORD kQueryTimeoutMs=1500u;
static const UINT kNvidiaVendor=0x10DEu;

static const char* kLab25Sha="ee882f82dd017c470a5003cd522f2cb5ec31ce4b31ae341ec75f3bb737642e72";
static const char* kLab27Sha="48e3964198c6d1e4371201655519d8ccfc37fe22838b26f3ce4e93cd20d3db44";
static const char* kK185Sha ="6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16";
static const char* kK185VsSha="6328bbd87aac73b07d6112de593f781b2769381aae65fa9c2bcfa06fdc68585c";
static const char* kLabVsSha ="f2512c09f70c30aa6f4b06e6885f34acac23ee5e5e3b94486b0846159578fcf8";

static std::wstring Join(const std::wstring& a,const std::wstring& b)
{
    if(a.empty()) return b;
    if(a[a.size()-1]==L'\\' || a[a.size()-1]==L'/') return a+b;
    return a+L"\\"+b;
}

static bool EnsureDir(const std::wstring& p)
{
    DWORD a=GetFileAttributesW(p.c_str());
    if(a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY)) return true;
    if(CreateDirectoryW(p.c_str(),0)) return true;
    return GetLastError()==ERROR_ALREADY_EXISTS;
}

static bool ReadFile(const std::wstring& p,std::vector<unsigned char>& out)
{
    FILE* f=0;
    if(_wfopen_s(&f,p.c_str(),L"rb")!=0 || !f) return false;
    _fseeki64(f,0,SEEK_END); __int64 n=_ftelli64(f); _fseeki64(f,0,SEEK_SET);
    if(n<=0){ fclose(f); return false; }
    out.resize((size_t)n);
    const size_t got=fread(&out[0],1,(size_t)n,f); fclose(f);
    return got==(size_t)n;
}

static std::string Hex(const unsigned char* p,size_t n)
{
    static const char* h="0123456789abcdef";
    std::string s(n*2u,'0');
    for(size_t i=0;i<n;++i){ s[i*2]=h[(p[i]>>4)&15]; s[i*2+1]=h[p[i]&15]; }
    return s;
}

static HRESULT Sha256(const std::wstring& p,std::string& out)
{
    std::vector<unsigned char> data;
    if(!ReadFile(p,data)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    BCRYPT_ALG_HANDLE alg=0; BCRYPT_HASH_HANDLE hash=0;
    DWORD objN=0,hashN=0,cb=0;
    NTSTATUS st=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,0,0);
    if(st<0) return E_FAIL;
    st=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objN,sizeof(objN),&cb,0);
    if(st<0){ BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
    st=BCryptGetProperty(alg,BCRYPT_HASH_LENGTH,(PUCHAR)&hashN,sizeof(hashN),&cb,0);
    if(st<0 || hashN!=32u){ BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
    std::vector<unsigned char> obj(objN),digest(hashN);
    st=BCryptCreateHash(alg,&hash,&obj[0],objN,0,0,0);
    if(st<0){ BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
    size_t off=0;
    while(off<data.size()){
        const size_t rem=data.size()-off;
        const ULONG n=(ULONG)((std::min)(rem,(size_t)0x40000000u));
        st=BCryptHashData(hash,&data[off],n,0);
        if(st<0){ BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0); return E_FAIL; }
        off+=n;
    }
    st=BCryptFinishHash(hash,&digest[0],hashN,0);
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg,0);
    if(st<0) return E_FAIL;
    out=Hex(&digest[0],digest.size());
    return S_OK;
}

static HRESULT Verify(const std::wstring& p,const char* expected,const char* label)
{
    std::string got; HRESULT hr=Sha256(p,got); if(FAILED(hr)) return hr;
    if(got!=expected){
        std::cerr<<"[FAIL] SHA256 "<<label<<" expected="<<expected<<" got="<<got<<std::endl;
        return HRESULT_FROM_WIN32(ERROR_CRC);
    }
    std::cout<<"[PASS] SHA256 "<<label<<"="<<got<<std::endl;
    return S_OK;
}

static double Percentile(std::vector<double> v,double p)
{
    if(v.empty()) return 0.0;
    std::sort(v.begin(),v.end());
    const double pos=(v.size()-1)*p;
    const size_t lo=(size_t)pos, hi=(lo+1<v.size()?lo+1:lo);
    const double t=pos-(double)lo;
    return v[lo]+(v[hi]-v[lo])*t;
}

static double Median(const std::vector<double>& v){ return Percentile(v,0.5); }

struct Triad
{
    UINT index;
    int order0,order1,order2;
    double lab25,lab27,k185;
    Triad():index(0),order0(0),order1(1),order2(2),lab25(0),lab27(0),k185(0){}
};

class Bench
{
public:
    Bench():m_fl(D3D_FEATURE_LEVEL_9_1){}
    ~Bench(){ if(m_ctx) m_ctx->ClearState(); }

    HRESULT Init(bool warp,std::wstring& adapterName)
    {
        HRESULT hr=S_OK;
        if(warp){
            D3D_FEATURE_LEVEL req[]={D3D_FEATURE_LEVEL_11_0};
            hr=D3D11CreateDevice(0,D3D_DRIVER_TYPE_WARP,0,0,req,1,D3D11_SDK_VERSION,&m_dev,&m_fl,&m_ctx);
            adapterName=L"WARP";
        }else{
            ComPtr<IDXGIFactory1> fac; hr=CreateDXGIFactory1(__uuidof(IDXGIFactory1),&fac); if(FAILED(hr)) return hr;
            ComPtr<IDXGIAdapter1> chosen; DXGI_ADAPTER_DESC1 chosenDesc={};
            std::wcout<<L"[GPU] adapters:"<<std::endl;
            for(UINT i=0;;++i){
                ComPtr<IDXGIAdapter1> a; if(fac->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 d={}; a->GetDesc1(&d);
                std::wcout<<L"  ["<<i<<L"] "<<d.Description<<L" vendor=0x"<<std::hex<<d.VendorId<<std::dec<<std::endl;
                if(!chosen && d.VendorId==kNvidiaVendor && wcsstr(d.Description,L"GTX 960M")){ chosen=a; chosenDesc=d; }
            }
            if(!chosen) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            D3D_FEATURE_LEVEL req[]={D3D_FEATURE_LEVEL_11_0};
            hr=D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,0,0,req,1,D3D11_SDK_VERSION,&m_dev,&m_fl,&m_ctx);
            adapterName=chosenDesc.Description;
        }
        if(FAILED(hr)) return hr;
        if(m_fl<D3D_FEATURE_LEVEL_11_0) return E_FAIL;

        D3D11_SAMPLER_DESC sd={}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        hr=m_dev->CreateSamplerState(&sd,&m_sampler); if(FAILED(hr)) return hr;
        D3D11_RASTERIZER_DESC rd={}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        hr=m_dev->CreateRasterizerState(&rd,&m_raster); if(FAILED(hr)) return hr;
        D3D11_DEPTH_STENCIL_DESC dd={}; dd.DepthEnable=FALSE; dd.StencilEnable=FALSE;
        hr=m_dev->CreateDepthStencilState(&dd,&m_depth); if(FAILED(hr)) return hr;
        D3D11_BUFFER_DESC bd={}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr=m_dev->CreateBuffer(&bd,0,&m_cb); if(FAILED(hr)) return hr;
        return CreateTargets();
    }

    HRESULT Load(const std::wstring& dir)
    {
        const std::wstring labVs=Join(dir,L"lab_fullscreen_vs.cso");
        const std::wstring kVs=Join(dir,L"ptar_vs.cso");
        const std::wstring p25=Join(dir,L"lab25.cso");
        const std::wstring p27=Join(dir,L"lab27.cso");
        const std::wstring pk=Join(dir,L"ptar_k185_control_ps.cso");
        HRESULT hr=Verify(labVs,kLabVsSha,"lab_fullscreen_vs.cso"); if(FAILED(hr)) return hr;
        hr=Verify(kVs,kK185VsSha,"ptar_vs.cso"); if(FAILED(hr)) return hr;
        hr=Verify(p25,kLab25Sha,"lab25.cso"); if(FAILED(hr)) return hr;
        hr=Verify(p27,kLab27Sha,"lab27.cso"); if(FAILED(hr)) return hr;
        hr=Verify(pk,kK185Sha,"ptar_k185_control_ps.cso"); if(FAILED(hr)) return hr;
        std::vector<unsigned char> a,b,c,d,e;
        if(!ReadFile(labVs,a)||!ReadFile(kVs,b)||!ReadFile(p25,c)||!ReadFile(p27,d)||!ReadFile(pk,e)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if(a.size()<4||b.size()<4||c.size()<4||d.size()<4||e.size()<4 || memcmp(&a[0],"DXBC",4)||memcmp(&b[0],"DXBC",4)||memcmp(&c[0],"DXBC",4)||memcmp(&d[0],"DXBC",4)||memcmp(&e[0],"DXBC",4)) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        hr=m_dev->CreateVertexShader(&a[0],a.size(),0,&m_labVs); if(FAILED(hr)) return hr;
        hr=m_dev->CreateVertexShader(&b[0],b.size(),0,&m_kVs); if(FAILED(hr)) return hr;
        hr=m_dev->CreatePixelShader(&c[0],c.size(),0,&m_p25); if(FAILED(hr)) return hr;
        hr=m_dev->CreatePixelShader(&d[0],d.size(),0,&m_p27); if(FAILED(hr)) return hr;
        hr=m_dev->CreatePixelShader(&e[0],e.size(),0,&m_pk); if(FAILED(hr)) return hr;
        return S_OK;
    }

    HRESULT Smoke()
    {
        for(int v=0;v<3;++v){ SetPipe(v); m_ctx->Draw(3,0); }
        ID3D11ShaderResourceView* n=0; m_ctx->PSSetShaderResources(0,1,&n);
        return m_dev->GetDeviceRemovedReason();
    }

    HRESULT Run(std::vector<Triad>& rows)
    {
        PTARD3D11GpuTimerRing timer; HRESULT hr=timer.Initialize(m_dev.Get()); if(FAILED(hr)) return hr;
        static const int perms[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for(UINT i=0;i<kWarmupTriads;++i){
            const int* p=perms[i%6];
            for(int j=0;j<3;++j){ double ms=0; hr=Timed(timer,p[j],ms); if(FAILED(hr)) return hr; }
        }
        rows.clear(); rows.reserve(kSamples);
        for(UINT i=0;i<kSamples;++i){
            Triad r; r.index=i; const int* p=perms[i%6]; r.order0=p[0];r.order1=p[1];r.order2=p[2];
            for(int j=0;j<3;++j){
                double ms=0; hr=Timed(timer,p[j],ms); if(FAILED(hr)) return hr;
                if(p[j]==0) r.lab25=ms; else if(p[j]==1) r.lab27=ms; else r.k185=ms;
            }
            rows.push_back(r);
        }
        ID3D11ShaderResourceView* n=0; m_ctx->PSSetShaderResources(0,1,&n);
        return S_OK;
    }

private:
    struct Constants{ float inputSize[2]; float outputSize[2]; };

    HRESULT CreateTargets()
    {
        std::vector<unsigned char> px((size_t)kInW*kInH*4u);
        for(UINT y=0;y<kInH;++y) for(UINT x=0;x<kInW;++x){
            const size_t i=((size_t)y*kInW+x)*4u;
            px[i]=(unsigned char)((x*17u+y*13u+(x^y))&255u);
            px[i+1]=(unsigned char)((x*5u+y*29u+((x*3u)^(y*7u)))&255u);
            px[i+2]=(unsigned char)((x*31u+y*3u+(x+y)*11u)&255u); px[i+3]=255u;
        }
        D3D11_TEXTURE2D_DESC d={}; d.Width=kInW;d.Height=kInH;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init={}; init.pSysMem=&px[0]; init.SysMemPitch=kInW*4u;
        HRESULT hr=m_dev->CreateTexture2D(&d,&init,&m_in); if(FAILED(hr)) return hr;
        hr=m_dev->CreateShaderResourceView(m_in.Get(),0,&m_srv); if(FAILED(hr)) return hr;
        d.Width=kOutW;d.Height=kOutH;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        hr=m_dev->CreateTexture2D(&d,0,&m_out); if(FAILED(hr)) return hr;
        return m_dev->CreateRenderTargetView(m_out.Get(),0,&m_rtv);
    }

    void SetPipe(int variant)
    {
        Constants c={{(float)kInW,(float)kInH},{(float)kOutW,(float)kOutH}};
        m_ctx->UpdateSubresource(m_cb.Get(),0,0,&c,0,0);
        D3D11_VIEWPORT vp={0,0,(FLOAT)kOutW,(FLOAT)kOutH,0,1}; m_ctx->RSSetViewports(1,&vp);
        m_ctx->RSSetState(m_raster.Get()); m_ctx->OMSetDepthStencilState(m_depth.Get(),0);
        ID3D11RenderTargetView* r=m_rtv.Get(); m_ctx->OMSetRenderTargets(1,&r,0);
        m_ctx->IASetInputLayout(0); m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_ctx->VSSetShader(variant==2?m_kVs.Get():m_labVs.Get(),0,0);
        m_ctx->PSSetShader(variant==0?m_p25.Get():(variant==1?m_p27.Get():m_pk.Get()),0,0);
        ID3D11ShaderResourceView* s=m_srv.Get(); m_ctx->PSSetShaderResources(0,1,&s);
        ID3D11SamplerState* sam=m_sampler.Get(); m_ctx->PSSetSamplers(0,1,&sam);
        ID3D11Buffer* cb=m_cb.Get(); m_ctx->PSSetConstantBuffers(0,1,&cb);
    }

    HRESULT Timed(PTARD3D11GpuTimerRing& timer,int variant,double& ms)
    {
        SetPipe(variant);
        for(;;){
            if(!timer.Begin(m_ctx.Get())) return E_UNEXPECTED;
            m_ctx->Draw(3,0);
            if(!timer.End(m_ctx.Get())) return E_UNEXPECTED;
            const ULONGLONG start=GetTickCount64();
            for(;;){
                bool ready=false,valid=false; HRESULT hr=timer.TryResolveEx(m_ctx.Get(),&ms,0u,&ready,&valid);
                if(FAILED(hr)) return hr;
                if(ready){ if(valid) return S_OK; break; }
                if(FAILED(m_dev->GetDeviceRemovedReason())) return m_dev->GetDeviceRemovedReason();
                if(GetTickCount64()-start>=kQueryTimeoutMs) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                Sleep(1);
            }
        }
    }

    ComPtr<ID3D11Device> m_dev; ComPtr<ID3D11DeviceContext> m_ctx; D3D_FEATURE_LEVEL m_fl;
    ComPtr<ID3D11SamplerState> m_sampler; ComPtr<ID3D11RasterizerState> m_raster; ComPtr<ID3D11DepthStencilState> m_depth; ComPtr<ID3D11Buffer> m_cb;
    ComPtr<ID3D11Texture2D> m_in,m_out; ComPtr<ID3D11ShaderResourceView> m_srv; ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11VertexShader> m_labVs,m_kVs; ComPtr<ID3D11PixelShader> m_p25,m_p27,m_pk;
};

static const char* Name(int v){ return v==0?"LAB25":(v==1?"LAB27":"K185"); }

static void WriteCsv(const std::wstring& path,const std::vector<Triad>& rows)
{
    FILE* f=0; if(_wfopen_s(&f,path.c_str(),L"wb")!=0||!f) return;
    fprintf(f,"triad,order0,order1,order2,lab25_ms,lab27_ms,k185_ms,lab25_minus_lab27_ms,lab27_faster\r\n");
    for(size_t i=0;i<rows.size();++i){ const Triad&r=rows[i];
        fprintf(f,"%u,%s,%s,%s,%.9f,%.9f,%.9f,%.9f,%u\r\n",r.index,Name(r.order0),Name(r.order1),Name(r.order2),r.lab25,r.lab27,r.k185,r.lab25-r.lab27,r.lab27<r.lab25?1u:0u);
    }
    fclose(f);
}

static void WriteSummary(const std::wstring& path,const std::wstring& adapter,const std::vector<Triad>& rows)
{
    std::vector<double> a,b,k,d,sp; a.reserve(rows.size());b.reserve(rows.size());k.reserve(rows.size());d.reserve(rows.size());sp.reserve(rows.size());
    UINT w27=0,w25=0,tie=0;
    for(size_t i=0;i<rows.size();++i){
        a.push_back(rows[i].lab25);b.push_back(rows[i].lab27);k.push_back(rows[i].k185);d.push_back(rows[i].lab25-rows[i].lab27);
        sp.push_back(rows[i].lab25>0?100.0*(rows[i].lab25-rows[i].lab27)/rows[i].lab25:0.0);
        if(rows[i].lab27<rows[i].lab25) ++w27; else if(rows[i].lab25<rows[i].lab27) ++w25; else ++tie;
    }
    std::vector<double> k0(k.begin(),k.begin()+k.size()/2), k1(k.begin()+k.size()/2,k.end());
    std::vector<double> a0(a.begin(),a.begin()+a.size()/2), a1(a.begin()+a.size()/2,a.end());
    std::vector<double> b0(b.begin(),b.begin()+b.size()/2), b1(b.begin()+b.size()/2,b.end());
    FILE* f=0; if(_wfopen_s(&f,path.c_str(),L"wb")!=0||!f) return;
    std::string ad(adapter.begin(),adapter.end());
    fprintf(f,"{\r\n");
    fprintf(f,"  \"protocol\": \"LAB27_GTX960M_SAME_RUN_BALANCED_COMPARE\",\r\n");
    fprintf(f,"  \"adapter\": \"%s\",\r\n",ad.c_str());
    fprintf(f,"  \"input\": \"1280x720\", \"output\": \"1920x1080\",\r\n");
    fprintf(f,"  \"samples\": %u, \"warmup_triads\": %u,\r\n",(UINT)rows.size(),kWarmupTriads);
    fprintf(f,"  \"order_protocol\": \"SIX_ROTATING_PERMUTATIONS_BALANCED\",\r\n");
    fprintf(f,"  \"lab25_median_ms\": %.9f, \"lab27_median_ms\": %.9f, \"k185_median_ms\": %.9f,\r\n",Median(a),Median(b),Median(k));
    fprintf(f,"  \"paired_lab25_minus_lab27_median_ms\": %.9f,\r\n",Median(d));
    fprintf(f,"  \"paired_speedup_median_percent\": %.9f,\r\n",Median(sp));
    fprintf(f,"  \"lab27_faster_count\": %u, \"lab25_faster_count\": %u, \"ties\": %u,\r\n",w27,w25,tie);
    fprintf(f,"  \"first_half\": {\"lab25_median_ms\": %.9f, \"lab27_median_ms\": %.9f, \"k185_median_ms\": %.9f},\r\n",Median(a0),Median(b0),Median(k0));
    fprintf(f,"  \"second_half\": {\"lab25_median_ms\": %.9f, \"lab27_median_ms\": %.9f, \"k185_median_ms\": %.9f},\r\n",Median(a1),Median(b1),Median(k1));
    fprintf(f,"  \"hashes\": {\"lab25\": \"%s\", \"lab27\": \"%s\", \"k185\": \"%s\"}\r\n",kLab25Sha,kLab27Sha,kK185Sha);
    fprintf(f,"}\r\n"); fclose(f);

    FILE* bf=0; if(_wfopen_s(&bf,Join(path.substr(0,path.find_last_of(L"\\/")),L"BLOCKS_60.csv").c_str(),L"wb")==0 && bf){
        fprintf(bf,"block,first_triad,last_triad,lab25_median_ms,lab27_median_ms,k185_median_ms,paired_lab25_minus_lab27_median_ms\r\n");
        for(size_t s=0;s<rows.size();s+=60){
            const size_t e=(std::min)(s+60,rows.size()); std::vector<double> aa,bb,kk,dd;
            for(size_t i=s;i<e;++i){aa.push_back(rows[i].lab25);bb.push_back(rows[i].lab27);kk.push_back(rows[i].k185);dd.push_back(rows[i].lab25-rows[i].lab27);}
            fprintf(bf,"%u,%u,%u,%.9f,%.9f,%.9f,%.9f\r\n",(UINT)(s/60),(UINT)s,(UINT)(e-1),Median(aa),Median(bb),Median(kk),Median(dd));
        }
        fclose(bf);
    }
}

int wmain(int argc,wchar_t** argv)
{
    bool warp=false;
    for(int i=1;i<argc;++i) if(wcscmp(argv[i],L"--warp-smoke")==0) warp=true;
    wchar_t exe[MAX_PATH]={}; if(!GetModuleFileNameW(0,exe,MAX_PATH)) return 2;
    std::wstring base=exe; size_t slash=base.find_last_of(L"\\/"); if(slash!=std::wstring::npos) base.resize(slash); else base=L".";
    const std::wstring shaders=Join(base,L"shaders");
    Bench b; std::wstring adapter; HRESULT hr=b.Init(warp,adapter);
    if(FAILED(hr)){ std::cerr<<"[FAIL] D3D init hr=0x"<<std::hex<<(unsigned)hr<<std::dec<<std::endl; return 10; }
    hr=b.Load(shaders); if(FAILED(hr)){ std::cerr<<"[FAIL] shader load hr=0x"<<std::hex<<(unsigned)hr<<std::dec<<std::endl; return 11; }
    if(warp){ hr=b.Smoke(); if(FAILED(hr)) return 12; std::cout<<"[PASS] WARP smoke: LAB25/LAB27/K185 precompiled DXBC loaded and drawn."<<std::endl; return 0; }
    std::wcout<<L"[GPU] selected="<<adapter<<std::endl;
    std::vector<Triad> rows; hr=b.Run(rows); if(FAILED(hr)){ std::cerr<<"[FAIL] timing hr=0x"<<std::hex<<(unsigned)hr<<std::dec<<std::endl; return 20; }
    const std::wstring out=Join(base,L"results"); if(!EnsureDir(out)) return 21;
    WriteCsv(Join(out,L"LAB27_COMPARE_RAW.csv"),rows);
    WriteSummary(Join(out,L"LAB27_COMPARE_SUMMARY.json"),adapter,rows);
    std::vector<double> delta; UINT wins=0; for(size_t i=0;i<rows.size();++i){delta.push_back(rows[i].lab25-rows[i].lab27); if(rows[i].lab27<rows[i].lab25) ++wins;}
    std::cout<<std::fixed<<std::setprecision(6)<<"[RESULT] paired LAB25-LAB27 median ms="<<Median(delta)<<" LAB27 wins="<<wins<<"/"<<rows.size()<<std::endl;
    std::cout<<"[PASS] results written under .\\results"<<std::endl;
    return 0;
}
