#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>

#include "ptar_vs_jitter_patch.h"
#include "r15_geometry_vs_bytecode.h"
#include "r15_geometry_ps_bytecode.h"
#include "r15_exact_reservoir_ps_bytecode.h"

static const UINT SRC_W=128, SRC_H=72, OUT_W=192, OUT_H=108;

struct GVertex { float x,y,z,w; DWORD color; };
struct BVertex { float x,y,z,rhw,u,v; };
struct Phase { float j; int residue; };
static const Phase kAxis[3]={{-1.0f/3.0f,1},{0.0f,0},{1.0f/3.0f,2}};
static const int kSchedule[9][2]={{1,1},{2,1},{2,2},{1,2},{0,2},{0,1},{0,0},{1,0},{2,0}};

static float ClipX(float pixelX){return 2.0f*pixelX/(float)OUT_W-1.0f;}
static float ClipY(float pixelY){return 1.0f-2.0f*pixelY/(float)OUT_H;}

static HRESULT CreateRT(IDirect3DDevice9* d,UINT w,UINT h,IDirect3DTexture9** t,IDirect3DSurface9** s)
{
    *t=0;*s=0;
    HRESULT hr=d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t,0);
    if(FAILED(hr)||!*t) return FAILED(hr)?hr:E_FAIL;
    hr=(*t)->GetSurfaceLevel(0,s);
    if(FAILED(hr)||!*s){(*t)->Release();*t=0;return FAILED(hr)?hr:E_FAIL;}
    return S_OK;
}

static bool DrawGeometry(IDirect3DDevice9* d,IDirect3DSurface9* rt,UINT w,UINT h,
    IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* decl,
    const float* c255)
{
    d->SetTexture(0,0); d->SetTexture(1,0);
    if(FAILED(d->SetRenderTarget(0,rt))) return false;
    d->SetDepthStencilSurface(0);
    D3DVIEWPORT9 vp={0,0,w,h,0,1}; d->SetViewport(&vp);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    if(FAILED(d->Clear(0,0,D3DCLEAR_TARGET,0xFF18202Au,1,0))) return false;
    d->SetVertexDeclaration(decl); d->SetVertexShader(vs); d->SetPixelShader(ps);
    if(c255) d->SetVertexShaderConstantF(255,c255,1);

    // A perspective cereal-box silhouette: near-vertical sides plus slanted top/bottom.
    // Two triangles, flat color. Any difference is therefore raster coverage/AA geometry,
    // not texture filtering or mip selection.
    const DWORD fg=0xFFE0B85Au;
    const GVertex v[6]={
        {ClipX(67.35f),ClipY(25.20f),0.5f,1,fg},
        {ClipX(118.10f),ClipY(28.65f),0.5f,1,fg},
        {ClipX(116.45f),ClipY(84.30f),0.5f,1,fg},
        {ClipX(67.35f),ClipY(25.20f),0.5f,1,fg},
        {ClipX(116.45f),ClipY(84.30f),0.5f,1,fg},
        {ClipX(64.80f),ClipY(80.55f),0.5f,1,fg}
    };
    HRESULT hr=d->BeginScene();
    if(SUCCEEDED(hr)){
        hr=d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(GVertex));
        HRESULT e=d->EndScene(); if(SUCCEEDED(hr)) hr=e;
    }
    return SUCCEEDED(hr);
}

static bool DrawReservoir(IDirect3DDevice9* d,IDirect3DSurface9* rt,IDirect3DPixelShader9* ps,
    IDirect3DTexture9* cur,IDirect3DTexture9* prev,float jx,float jy,int rx,int ry)
{
    d->SetTexture(0,0);d->SetTexture(1,0);
    if(FAILED(d->SetRenderTarget(0,rt))) return false;
    d->SetDepthStencilSurface(0);
    D3DVIEWPORT9 vp={0,0,OUT_W,OUT_H,0,1};d->SetViewport(&vp);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    for(DWORD s=0;s<2;++s){
        d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    const float c0[4]={(float)SRC_W,(float)SRC_H,(float)OUT_W,(float)OUT_H};
    const float c1[4]={jx,jy,(float)rx,(float)ry};
    d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShaderConstantF(1,c1,1);
    d->SetVertexShader(0);d->SetVertexDeclaration(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    d->SetPixelShader(ps);d->SetTexture(0,cur);d->SetTexture(1,prev);
    const BVertex q[4]={{-.5f,-.5f,0,1,0,0},{OUT_W-.5f,-.5f,0,1,1,0},{-.5f,OUT_H-.5f,0,1,0,1},{OUT_W-.5f,OUT_H-.5f,0,1,1,1}};
    HRESULT hr=d->BeginScene();if(SUCCEEDED(hr)){hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(BVertex));HRESULT e=d->EndScene();if(SUCCEEDED(hr))hr=e;}
    d->SetTexture(0,0);d->SetTexture(1,0);return SUCCEEDED(hr);
}

static bool ReadRT(IDirect3DDevice9* d,IDirect3DSurface9* rt,std::vector<DWORD>& out)
{
    IDirect3DSurface9* sys=0;HRESULT hr=d->CreateOffscreenPlainSurface(OUT_W,OUT_H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,0);
    if(FAILED(hr)||!sys)return false;hr=d->GetRenderTargetData(rt,sys);if(FAILED(hr)){sys->Release();return false;}
    D3DLOCKED_RECT lr={};hr=sys->LockRect(&lr,0,D3DLOCK_READONLY);if(FAILED(hr)){sys->Release();return false;}
    out.resize((SIZE_T)OUT_W*OUT_H);for(UINT y=0;y<OUT_H;++y)std::memcpy(&out[(SIZE_T)y*OUT_W],(BYTE*)lr.pBits+(SIZE_T)y*lr.Pitch,OUT_W*4);
    sys->UnlockRect();sys->Release();return true;
}

int main()
{
    HWND hwnd=CreateWindowExW(0,L"STATIC",L"R15_GEOM",WS_OVERLAPPEDWINDOW,0,0,64,64,0,0,GetModuleHandleW(0),0);
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);if(!hwnd||!api)return 2;
    D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.hDeviceWindow=hwnd;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=0;HRESULT hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);
    if(FAILED(hr)||!d)hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);
    if(FAILED(hr)||!d)return 3;

    IDirect3DVertexShader9* nativeVS=0;d->CreateVertexShader((const DWORD*)g_r15GeometryVs,&nativeVS);
    std::vector<DWORD> code;PtVsJitterPatchInfo pi={};if(!PtPatchVertexShaderForJitter((const DWORD*)g_r15GeometryVs,&code,&pi))return 4;
    IDirect3DVertexShader9* jitterVS=0;if(FAILED(d->CreateVertexShader(code.data(),&jitterVS)))return 5;
    IDirect3DPixelShader9* gps=0;IDirect3DPixelShader9* rps=0;
    if(FAILED(d->CreatePixelShader((const DWORD*)g_r15GeometryPs,&gps)))return 6;
    if(FAILED(d->CreatePixelShader((const DWORD*)g_r15ExactReservoirPs,&rps)))return 7;
    const D3DVERTEXELEMENT9 el[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* decl=0;if(FAILED(d->CreateVertexDeclaration(el,&decl)))return 8;

    IDirect3DTexture9 *nt=0,*st=0,*ht[2]={0,0};IDirect3DSurface9 *ns=0,*ss=0,*hs[2]={0,0};
    if(FAILED(CreateRT(d,OUT_W,OUT_H,&nt,&ns))||FAILED(CreateRT(d,SRC_W,SRC_H,&st,&ss)))return 9;
    for(int i=0;i<2;++i)if(FAILED(CreateRT(d,OUT_W,OUT_H,&ht[i],&hs[i])))return 10;
    const float zero[4]={0,0,0,0};if(!DrawGeometry(d,ns,OUT_W,OUT_H,nativeVS,gps,decl,zero))return 11;
    d->ColorFill(hs[0],0,0);d->ColorFill(hs[1],0,0);int read=0,write=1;
    for(int p=0;p<9;++p){const Phase ax=kAxis[kSchedule[p][0]],ay=kAxis[kSchedule[p][1]];const float c255[4]={-2*ax.j/SRC_W,2*ay.j/SRC_H,0,0};
        if(!DrawGeometry(d,ss,SRC_W,SRC_H,jitterVS,gps,decl,c255))return 12;
        if(!DrawReservoir(d,hs[write],rps,st,ht[read],ax.j,ay.j,ax.residue,ay.residue))return 13;std::swap(read,write);}
    std::vector<DWORD>a,b;if(!ReadRT(d,ns,a)||!ReadRT(d,hs[read],b))return 14;
    unsigned long long changed=0,sum=0,byPhase[3][3]={{0}};int maxd=0;UINT firstX=0,firstY=0;bool haveFirst=false;
    for(UINT y=2;y<OUT_H-2;++y)for(UINT x=2;x<OUT_W-2;++x){DWORD A=a[(SIZE_T)y*OUT_W+x],B=b[(SIZE_T)y*OUT_W+x];int dr=std::abs((int)((A>>16)&255)-(int)((B>>16)&255)),dg=std::abs((int)((A>>8)&255)-(int)((B>>8)&255)),db=std::abs((int)(A&255)-(int)(B&255));int m=std::max(dr,std::max(dg,db));sum+=(unsigned long long)dr+dg+db;if(m){++changed;++byPhase[y%3][x%3];if(!haveFirst){firstX=x;firstY=y;haveFirst=true;}}maxd=std::max(maxd,m);}
    const double mae=(double)sum/((OUT_W-4)*(OUT_H-4)*3.0);
    std::printf("R15_GEOM_MAE_8BIT=%.9f\nR15_GEOM_CHANGED_PIXELS=%llu\nR15_GEOM_MAX_ABS=%d\n",mae,changed,maxd);
    if(haveFirst)std::printf("R15_GEOM_FIRST_MISMATCH=%u,%u\n",firstX,firstY);
    for(int py=0;py<3;++py)for(int px=0;px<3;++px)std::printf("R15_GEOM_PHASE_%d_%d_CHANGED=%llu\n",px,py,byPhase[py][px]);
    const bool pass=(changed==0);std::printf("R15_GEOMETRY_EXACT=%s\n",pass?"PASS":"FAIL");
    for(int i=0;i<2;++i){hs[i]->Release();ht[i]->Release();}ss->Release();st->Release();ns->Release();nt->Release();decl->Release();rps->Release();gps->Release();jitterVS->Release();nativeVS->Release();d->Release();api->Release();DestroyWindow(hwnd);return pass?0:15;
}
