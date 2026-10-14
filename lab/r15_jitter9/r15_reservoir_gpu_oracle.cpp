#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

#include "r15_exact_reservoir_ps_bytecode.h"

static const UINT SRC_W=1280;
static const UINT SRC_H=720;
static const UINT OUT_W=1920;
static const UINT OUT_H=1080;

struct Phase1D
{
    float jitter;
    int parity;
    int residue;
};

static const Phase1D kAxis[3]={
    {-0.5f,       1,1},
    {-1.0f/6.0f, 0,0},
    { 1.0f/6.0f, 1,2}
};

static const int kSchedule[9][2]={
    {1,1},{2,1},{2,2},{1,2},{0,2},{0,1},{0,0},{1,0},{2,0}
};

struct Vertex
{
    float x,y,z,rhw;
    float u,v;
};

static unsigned char PtByte(float x)
{
    x=std::max(0.0f,std::min(1.0f,x));
    return (unsigned char)std::floor(x*255.0f+0.5f);
}

static DWORD Pattern(float x,float y)
{
    const double pi=3.14159265358979323846;
    const double r=0.50 + 0.22*std::sin(2.0*pi*(0.413*x+0.071*y)+0.31)
                        + 0.09*std::sin(2.0*pi*(0.357*y)+0.77);
    const double g=0.48 + 0.20*std::sin(2.0*pi*(0.389*x+0.333*y)-0.27)
                        + 0.11*std::cos(2.0*pi*(0.421*y)+0.19);
    const double b=0.46 + 0.18*std::cos(2.0*pi*(0.371*x-0.347*y)+0.73)
                        + 0.12*std::sin(2.0*pi*(0.443*x)+0.41);
    const DWORD R=PtByte((float)r);
    const DWORD G=PtByte((float)g);
    const DWORD B=PtByte((float)b);
    return 0xFF000000u|(R<<16)|(G<<8)|B;
}

static bool FillCurrent(
    IDirect3DTexture9* tex,
    float jitterX,
    float jitterY)
{
    D3DLOCKED_RECT lr={};
    HRESULT hr=tex->LockRect(0,&lr,0,0);
    if(FAILED(hr)) return false;
    for(UINT sy=0;sy<SRC_H;++sy)
    {
        DWORD* row=(DWORD*)((BYTE*)lr.pBits+(SIZE_T)sy*lr.Pitch);
        const float y=1.5f*(float)sy+0.25f+1.5f*jitterY;
        for(UINT sx=0;sx<SRC_W;++sx)
        {
            const float x=1.5f*(float)sx+0.25f+1.5f*jitterX;
            row[sx]=Pattern(x,y);
        }
    }
    tex->UnlockRect(0);
    return true;
}

static bool DrawPass(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    IDirect3DPixelShader9* shader,
    IDirect3DTexture9* current,
    IDirect3DTexture9* previous,
    float jitterX,
    float jitterY,
    float residueX,
    float residueY)
{
    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    HRESULT hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return false;
    dev->SetDepthStencilSurface(0);

    D3DVIEWPORT9 vp={0,0,OUT_W,OUT_H,0.0f,1.0f};
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);

    for(DWORD s=0;s<2;++s)
    {
        dev->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        dev->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);

    const float c0[4]={(float)SRC_W,(float)SRC_H,(float)OUT_W,(float)OUT_H};
    const float c1[4]={jitterX,jitterY,residueX,residueY};
    dev->SetPixelShaderConstantF(0,c0,1);
    dev->SetPixelShaderConstantF(1,c1,1);
    dev->SetPixelShader(shader);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetTexture(0,current);
    dev->SetTexture(1,previous);

    Vertex q[4]={
        {-0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f},
        {(float)OUT_W-0.5f,-0.5f,0.0f,1.0f,1.0f,0.0f},
        {-0.5f,(float)OUT_H-0.5f,0.0f,1.0f,0.0f,1.0f},
        {(float)OUT_W-0.5f,(float)OUT_H-0.5f,0.0f,1.0f,1.0f,1.0f}
    };

    hr=dev->BeginScene();
    if(SUCCEEDED(hr))
    {
        hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex));
        HRESULT endHr=dev->EndScene();
        if(SUCCEEDED(hr)) hr=endHr;
    }
    dev->SetTexture(0,0);
    dev->SetTexture(1,0);
    return SUCCEEDED(hr);
}

int main()
{
    HWND hwnd=CreateWindowExW(
        0,L"STATIC",L"PTAR_R15_RESERVOIR_ORACLE",
        WS_OVERLAPPEDWINDOW,0,0,64,64,0,0,GetModuleHandleW(0),0);
    if(!hwnd) return 2;

    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d){DestroyWindow(hwnd);return 3;}

    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed=TRUE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth=64;
    pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.hDeviceWindow=hwnd;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr) || !dev)
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    }
    if(FAILED(hr) || !dev)
    {
        std::printf("R15_RESERVOIR_DEVICE=FAIL hr=0x%08lX\n",(unsigned long)hr);
        d3d->Release();
        DestroyWindow(hwnd);
        return 4;
    }

    IDirect3DPixelShader9* shader=0;
    hr=dev->CreatePixelShader((const DWORD*)g_r15ExactReservoirPs,&shader);
    if(FAILED(hr) || !shader)
    {
        std::printf("R15_RESERVOIR_SHADER_CREATE=FAIL hr=0x%08lX\n",(unsigned long)hr);
        dev->Release();d3d->Release();DestroyWindow(hwnd);return 5;
    }

    IDirect3DTexture9* current=0;
    hr=dev->CreateTexture(
        SRC_W,SRC_H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&current,0);
    if(FAILED(hr) || !current) return 6;

    IDirect3DTexture9* histTex[2]={0,0};
    IDirect3DSurface9* histSurf[2]={0,0};
    for(int i=0;i<2;++i)
    {
        hr=dev->CreateTexture(
            OUT_W,OUT_H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,&histTex[i],0);
        if(FAILED(hr) || !histTex[i]) return 7;
        hr=histTex[i]->GetSurfaceLevel(0,&histSurf[i]);
        if(FAILED(hr) || !histSurf[i]) return 8;
        hr=dev->ColorFill(histSurf[i],0,0x00000000u);
        if(FAILED(hr)) return 9;
    }

    int read=0;
    int write=1;
    for(int phase=0;phase<9;++phase)
    {
        const int px=kSchedule[phase][0];
        const int py=kSchedule[phase][1];
        const Phase1D& ax=kAxis[px];
        const Phase1D& ay=kAxis[py];
        if(!FillCurrent(current,ax.jitter,ay.jitter))
        {
            std::printf("R15_FILL_PHASE_%d=FAIL\n",phase);
            return 10;
        }
        if(!DrawPass(
            dev,histSurf[write],shader,current,histTex[read],
            ax.jitter,ay.jitter,(float)ax.residue,(float)ay.residue))
        {
            std::printf("R15_DRAW_PHASE_%d=FAIL\n",phase);
            return 11;
        }
        std::swap(read,write);
    }

    IDirect3DSurface9* readback=0;
    hr=dev->CreateOffscreenPlainSurface(
        OUT_W,OUT_H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,0);
    if(FAILED(hr) || !readback) return 12;
    hr=dev->GetRenderTargetData(histSurf[read],readback);
    if(FAILED(hr))
    {
        std::printf("R15_READBACK=FAIL hr=0x%08lX\n",(unsigned long)hr);
        return 13;
    }

    D3DLOCKED_RECT lr={};
    hr=readback->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)) return 14;

    unsigned long long sumAbs=0;
    unsigned long long changed=0;
    unsigned long long invalid=0;
    int maxAbs=0;
    for(UINT y=0;y<OUT_H;++y)
    {
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+(SIZE_T)y*lr.Pitch);
        for(UINT x=0;x<OUT_W;++x)
        {
            const DWORD got=row[x];
            const DWORD ref=Pattern((float)x,(float)y);
            const int ga=(got>>24)&255;
            if(ga<254) ++invalid;
            const int gr=(got>>16)&255, gg=(got>>8)&255, gb=got&255;
            const int rr=(ref>>16)&255, rg=(ref>>8)&255, rb=ref&255;
            const int dr=std::abs(gr-rr), dg=std::abs(gg-rg), db=std::abs(gb-rb);
            const int m=std::max(dr,std::max(dg,db));
            maxAbs=std::max(maxAbs,m);
            sumAbs+=(unsigned long long)dr+dg+db;
            if(m) ++changed;
        }
    }
    readback->UnlockRect();

    const double mae=(double)sumAbs/(double)(OUT_W*OUT_H*3);
    std::printf("R15_RESERVOIR_INVALID_PIXELS=%llu\n",invalid);
    std::printf("R15_RESERVOIR_CHANGED_PIXELS=%llu\n",changed);
    std::printf("R15_RESERVOIR_MAE_8BIT=%.9f\n",mae);
    std::printf("R15_RESERVOIR_MAX_ABS=%d\n",maxAbs);

    const bool pass=(invalid==0 && changed==0 && maxAbs==0);
    std::printf("R15_EXACT_RESERVOIR_GPU=%s\n",pass?"PASS":"FAIL");

    readback->Release();
    for(int i=0;i<2;++i){histSurf[i]->Release();histTex[i]->Release();}
    current->Release();
    shader->Release();
    dev->Release();
    d3d->Release();
    DestroyWindow(hwnd);
    return pass?0:15;
}
