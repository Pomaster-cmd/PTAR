#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

#include "ptar_vs_jitter_patch.h"
#include "r15_scene_vs_bytecode.h"
#include "r15_scene_ps_bytecode.h"
#include "r15_exact_reservoir_ps_bytecode.h"

static const UINT SRC_W=128;
static const UINT SRC_H=72;
static const UINT OUT_W=192;
static const UINT OUT_H=108;

struct SceneVertex
{
    float x,y,z,w;
    float u,v;
};

struct BlitVertex
{
    float x,y,z,rhw;
    float u,v;
};

struct AxisPhase
{
    float sampleJitter;
    int residue;
};

static const int kSchedule[9][2]={
    {1,1},{2,1},{2,2},{1,2},{0,2},{0,1},{0,0},{1,0},{2,0}
};

static DWORD Pattern(UINT x,UINT y)
{
    // Deliberately hostile one-pixel detail: a wrong lattice phase creates a
    // large byte error rather than hiding behind a smooth gradient.
    const UINT r=(x*73u+y*29u+((x^y)*17u))&255u;
    const UINT g=(x*11u+y*97u+((x*3u+y*5u)^0x5Au))&255u;
    const UINT b=(x*131u+y*7u+((x+y)*43u))&255u;
    return 0xFF000000u|(r<<16)|(g<<8)|b;
}

static HRESULT CreateRenderTexture(
    IDirect3DDevice9* dev,UINT w,UINT h,
    IDirect3DTexture9** tex,IDirect3DSurface9** surf)
{
    *tex=0;*surf=0;
    HRESULT hr=dev->CreateTexture(
        w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
        D3DPOOL_DEFAULT,tex,0);
    if(FAILED(hr) || !*tex) return FAILED(hr)?hr:E_FAIL;
    hr=(*tex)->GetSurfaceLevel(0,surf);
    if(FAILED(hr) || !*surf)
    {
        (*tex)->Release();*tex=0;
        return FAILED(hr)?hr:E_FAIL;
    }
    return S_OK;
}

static bool FillSceneTexture(IDirect3DTexture9* tex)
{
    D3DLOCKED_RECT lr={};
    HRESULT hr=tex->LockRect(0,&lr,0,0);
    if(FAILED(hr)) return false;
    for(UINT y=0;y<OUT_H;++y)
    {
        DWORD* row=(DWORD*)((BYTE*)lr.pBits+(SIZE_T)y*lr.Pitch);
        for(UINT x=0;x<OUT_W;++x) row[x]=Pattern(x,y);
    }
    tex->UnlockRect(0);
    return true;
}

static bool DrawScene(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT w,UINT h,
    IDirect3DVertexShader9* vs,
    IDirect3DPixelShader9* ps,
    IDirect3DVertexDeclaration9* decl,
    IDirect3DTexture9* scene,
    const float* jitterConstant)
{
    dev->SetTexture(0,0);
    HRESULT hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return false;
    dev->SetDepthStencilSurface(0);
    D3DVIEWPORT9 vp={0,0,w,h,0.0f,1.0f};
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
    dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);

    hr=dev->Clear(0,0,D3DCLEAR_TARGET,0xFF010203u,1.0f,0);
    if(FAILED(hr)) return false;

    dev->SetVertexDeclaration(decl);
    dev->SetVertexShader(vs);
    dev->SetPixelShader(ps);
    dev->SetTexture(0,scene);
    if(jitterConstant)
        dev->SetVertexShaderConstantF(255,jitterConstant,1);

    const SceneVertex q[4]={
        {-1.0f, 1.0f,0.5f,1.0f,0.0f,0.0f},
        { 1.0f, 1.0f,0.5f,1.0f,1.0f,0.0f},
        {-1.0f,-1.0f,0.5f,1.0f,0.0f,1.0f},
        { 1.0f,-1.0f,0.5f,1.0f,1.0f,1.0f}
    };

    hr=dev->BeginScene();
    if(SUCCEEDED(hr))
    {
        hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(SceneVertex));
        HRESULT endHr=dev->EndScene();
        if(SUCCEEDED(hr)) hr=endHr;
    }
    dev->SetTexture(0,0);
    return SUCCEEDED(hr);
}

static bool DrawReservoir(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    IDirect3DPixelShader9* ps,
    IDirect3DTexture9* current,
    IDirect3DTexture9* previous,
    float jitterX,float jitterY,
    int residueX,int residueY)
{
    dev->SetTexture(0,0);dev->SetTexture(1,0);
    HRESULT hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return false;
    dev->SetDepthStencilSurface(0);
    D3DVIEWPORT9 vp={0,0,OUT_W,OUT_H,0.0f,1.0f};
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
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
    const float c0[4]={(float)SRC_W,(float)SRC_H,(float)OUT_W,(float)OUT_H};
    const float c1[4]={jitterX,jitterY,(float)residueX,(float)residueY};
    dev->SetPixelShaderConstantF(0,c0,1);
    dev->SetPixelShaderConstantF(1,c1,1);
    dev->SetVertexShader(0);
    dev->SetVertexDeclaration(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    dev->SetPixelShader(ps);
    dev->SetTexture(0,current);
    dev->SetTexture(1,previous);

    const BlitVertex q[4]={
        {-0.5f,-0.5f,0.0f,1.0f,0.0f,0.0f},
        {(float)OUT_W-0.5f,-0.5f,0.0f,1.0f,1.0f,0.0f},
        {-0.5f,(float)OUT_H-0.5f,0.0f,1.0f,0.0f,1.0f},
        {(float)OUT_W-0.5f,(float)OUT_H-0.5f,0.0f,1.0f,1.0f,1.0f}
    };
    hr=dev->BeginScene();
    if(SUCCEEDED(hr))
    {
        hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(BlitVertex));
        HRESULT endHr=dev->EndScene();
        if(SUCCEEDED(hr)) hr=endHr;
    }
    dev->SetTexture(0,0);dev->SetTexture(1,0);
    return SUCCEEDED(hr);
}

static bool ReadSurface(
    IDirect3DDevice9* dev,IDirect3DSurface9* src,
    UINT w,UINT h,std::vector<DWORD>* out)
{
    IDirect3DSurface9* sys=0;
    HRESULT hr=dev->CreateOffscreenPlainSurface(
        w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,0);
    if(FAILED(hr) || !sys) return false;
    hr=dev->GetRenderTargetData(src,sys);
    if(FAILED(hr)){sys->Release();return false;}
    D3DLOCKED_RECT lr={};
    hr=sys->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)){sys->Release();return false;}
    out->resize((SIZE_T)w*h);
    for(UINT y=0;y<h;++y)
        std::memcpy(&(*out)[(SIZE_T)y*w],(BYTE*)lr.pBits+(SIZE_T)y*lr.Pitch,w*sizeof(DWORD));
    sys->UnlockRect();
    sys->Release();
    return true;
}

struct ErrorStats
{
    double mae;
    unsigned long long changed;
    int maxAbs;
};

static ErrorStats CompareInterior(
    const std::vector<DWORD>& a,
    const std::vector<DWORD>& b)
{
    const UINT margin=3;
    unsigned long long sum=0,changed=0,count=0;
    int maxAbs=0;
    for(UINT y=margin;y<OUT_H-margin;++y)
    for(UINT x=margin;x<OUT_W-margin;++x)
    {
        const DWORD aa=a[(SIZE_T)y*OUT_W+x];
        const DWORD bb=b[(SIZE_T)y*OUT_W+x];
        const int ar=(aa>>16)&255,ag=(aa>>8)&255,ab=aa&255;
        const int br=(bb>>16)&255,bg=(bb>>8)&255,bbv=bb&255;
        const int dr=std::abs(ar-br),dg=std::abs(ag-bg),db=std::abs(ab-bbv);
        const int m=std::max(dr,std::max(dg,db));
        sum+=(unsigned long long)dr+dg+db;
        count+=3;
        if(m) ++changed;
        maxAbs=std::max(maxAbs,m);
    }
    ErrorStats s={count?(double)sum/(double)count:0.0,changed,maxAbs};
    return s;
}

static bool RunCandidate(
    IDirect3DDevice9* dev,
    IDirect3DVertexShader9* patchedVs,
    IDirect3DPixelShader9* scenePs,
    IDirect3DPixelShader9* reservoirPs,
    IDirect3DVertexDeclaration9* decl,
    IDirect3DTexture9* scene,
    IDirect3DTexture9* sourceTex,
    IDirect3DSurface9* sourceSurf,
    IDirect3DTexture9* histTex[2],
    IDirect3DSurface9* histSurf[2],
    const AxisPhase axis[3],
    bool oppositeClipSign,
    const std::vector<DWORD>& nativePixels,
    const char* label,
    ErrorStats* statsOut)
{
    dev->ColorFill(histSurf[0],0,0x00000000u);
    dev->ColorFill(histSurf[1],0,0x00000000u);
    int read=0,write=1;

    for(int phase=0;phase<9;++phase)
    {
        const int ix=kSchedule[phase][0];
        const int iy=kSchedule[phase][1];
        const AxisPhase& ax=axis[ix];
        const AxisPhase& ay=axis[iy];

        // If j is the desired shift of the scene sampling position in source
        // pixels, moving geometry by -j gives that sampling shift. D3D9 Y has
        // the opposite viewport sign. The opposite-sign candidate is kept as
        // an explicit calibration control rather than assumed away.
        float cx=-2.0f*ax.sampleJitter/(float)SRC_W;
        float cy= 2.0f*ay.sampleJitter/(float)SRC_H;
        if(oppositeClipSign){cx=-cx;cy=-cy;}
        const float c255[4]={cx,cy,0.0f,0.0f};

        if(!DrawScene(
            dev,sourceSurf,SRC_W,SRC_H,
            patchedVs,scenePs,decl,scene,c255))
            return false;

        if(!DrawReservoir(
            dev,histSurf[write],reservoirPs,
            sourceTex,histTex[read],
            ax.sampleJitter,ay.sampleJitter,
            ax.residue,ay.residue))
            return false;
        std::swap(read,write);
    }

    std::vector<DWORD> got;
    if(!ReadSurface(dev,histSurf[read],OUT_W,OUT_H,&got)) return false;
    const ErrorStats s=CompareInterior(got,nativePixels);
    if(statsOut) *statsOut=s;
    std::printf(
        "%s_MAE=%.9f CHANGED=%llu MAX=%d\n",
        label,s.mae,s.changed,s.maxAbs);
    return true;
}

int main()
{
    HWND hwnd=CreateWindowExW(
        0,L"STATIC",L"PTAR_R15_RASTER_JITTER_ORACLE",
        WS_OVERLAPPEDWINDOW,0,0,64,64,0,0,GetModuleHandleW(0),0);
    if(!hwnd) return 2;
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d){DestroyWindow(hwnd);return 3;}

    D3DPRESENT_PARAMETERS pp={};
    pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth=64;pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.hDeviceWindow=hwnd;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr) || !dev)
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr) || !dev)
    {
        std::printf("R15_RASTER_DEVICE=FAIL hr=0x%08lX\n",(unsigned long)hr);
        d3d->Release();DestroyWindow(hwnd);return 4;
    }

    IDirect3DVertexShader9* nativeVs=0;
    hr=dev->CreateVertexShader((const DWORD*)g_r15SceneVs,&nativeVs);
    if(FAILED(hr) || !nativeVs) return 5;

    std::vector<DWORD> patchedCode;
    PtVsJitterPatchInfo patchInfo={};
    if(!PtPatchVertexShaderForJitter(
        (const DWORD*)g_r15SceneVs,&patchedCode,&patchInfo)) return 6;
    IDirect3DVertexShader9* patchedVs=0;
    hr=dev->CreateVertexShader(patchedCode.data(),&patchedVs);
    if(FAILED(hr) || !patchedVs) return 7;

    IDirect3DPixelShader9* scenePs=0;
    IDirect3DPixelShader9* reservoirPs=0;
    hr=dev->CreatePixelShader((const DWORD*)g_r15ScenePs,&scenePs);
    if(FAILED(hr) || !scenePs) return 8;
    hr=dev->CreatePixelShader((const DWORD*)g_r15ExactReservoirPs,&reservoirPs);
    if(FAILED(hr) || !reservoirPs) return 9;

    const D3DVERTEXELEMENT9 elems[]={
        {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        D3DDECL_END()
    };
    IDirect3DVertexDeclaration9* decl=0;
    hr=dev->CreateVertexDeclaration(elems,&decl);
    if(FAILED(hr) || !decl) return 10;

    IDirect3DTexture9* scene=0;
    hr=dev->CreateTexture(
        OUT_W,OUT_H,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&scene,0);
    if(FAILED(hr) || !scene || !FillSceneTexture(scene)) return 11;

    IDirect3DTexture9* nativeTex=0;IDirect3DSurface9* nativeSurf=0;
    IDirect3DTexture9* sourceTex=0;IDirect3DSurface9* sourceSurf=0;
    IDirect3DTexture9* histTex[2]={0,0};IDirect3DSurface9* histSurf[2]={0,0};
    if(FAILED(CreateRenderTexture(dev,OUT_W,OUT_H,&nativeTex,&nativeSurf))) return 12;
    if(FAILED(CreateRenderTexture(dev,SRC_W,SRC_H,&sourceTex,&sourceSurf))) return 13;
    for(int i=0;i<2;++i)
        if(FAILED(CreateRenderTexture(dev,OUT_W,OUT_H,&histTex[i],&histSurf[i]))) return 14;

    const float zero[4]={0,0,0,0};
    if(!DrawScene(dev,nativeSurf,OUT_W,OUT_H,nativeVs,scenePs,decl,scene,zero)) return 15;
    std::vector<DWORD> nativePixels;
    if(!ReadSurface(dev,nativeSurf,OUT_W,OUT_H,&nativePixels)) return 16;

    // Model H is the image/texel-center convention used by the first offline
    // oracle. Model D is the D3D9 integer raster-center hypothesis. The GPU
    // decides which one is actually correct for programmable geometry.
    const AxisPhase modelH[3]={
        {-0.5f,1},{-1.0f/6.0f,0},{1.0f/6.0f,2}
    };
    const AxisPhase modelD[3]={
        {-1.0f/3.0f,1},{0.0f,0},{1.0f/3.0f,2}
    };

    ErrorStats h={};ErrorStats hOpp={};ErrorStats d={};ErrorStats dOpp={};
    if(!RunCandidate(dev,patchedVs,scenePs,reservoirPs,decl,scene,sourceTex,sourceSurf,histTex,histSurf,modelH,false,nativePixels,"R15_RASTER_HALF_CENTER",&h)) return 17;
    if(!RunCandidate(dev,patchedVs,scenePs,reservoirPs,decl,scene,sourceTex,sourceSurf,histTex,histSurf,modelH,true,nativePixels,"R15_RASTER_HALF_CENTER_OPPOSITE",&hOpp)) return 18;
    if(!RunCandidate(dev,patchedVs,scenePs,reservoirPs,decl,scene,sourceTex,sourceSurf,histTex,histSurf,modelD,false,nativePixels,"R15_RASTER_D3D9_CENTER",&d)) return 19;
    if(!RunCandidate(dev,patchedVs,scenePs,reservoirPs,decl,scene,sourceTex,sourceSurf,histTex,histSurf,modelD,true,nativePixels,"R15_RASTER_D3D9_CENTER_OPPOSITE",&dOpp)) return 20;

    const ErrorStats* best=&h;const char* bestName="HALF_CENTER";
    if(hOpp.mae<best->mae){best=&hOpp;bestName="HALF_CENTER_OPPOSITE";}
    if(d.mae<best->mae){best=&d;bestName="D3D9_CENTER";}
    if(dOpp.mae<best->mae){best=&dOpp;bestName="D3D9_CENTER_OPPOSITE";}
    std::printf("R15_RASTER_BEST=%s MAE=%.9f CHANGED=%llu MAX=%d\n",
        bestName,best->mae,best->changed,best->maxAbs);

    // Exact zero is the target. A one-byte tolerance is intentionally NOT
    // accepted here because this stage calibrates the phase convention itself.
    const bool pass=(best->changed==0 && best->maxAbs==0);
    std::printf("R15_RASTER_JITTER_EXACT=%s\n",pass?"PASS":"FAIL");

    for(int i=0;i<2;++i){histSurf[i]->Release();histTex[i]->Release();}
    sourceSurf->Release();sourceTex->Release();
    nativeSurf->Release();nativeTex->Release();
    scene->Release();decl->Release();
    reservoirPs->Release();scenePs->Release();
    patchedVs->Release();nativeVs->Release();
    dev->Release();d3d->Release();DestroyWindow(hwnd);
    return pass?0:21;
}
