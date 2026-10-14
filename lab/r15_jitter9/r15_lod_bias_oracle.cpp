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
#include "r15_scene_vs_bytecode.h"
#include "r15_scene_ps_bytecode.h"
#include "r15_exact_reservoir_ps_bytecode.h"

static const UINT SRC_W=128,SRC_H=72,OUT_W=192,OUT_H=108;
static const UINT TEX_W=256,TEX_H=144;
static const float NATIVE_LOD_BIAS=-0.5849625007211562f; // -log2(1.5)

struct V { float x,y,z,w,u,v; };
struct BV { float x,y,z,rhw,u,v; };
struct Phase { float j; int residue; };
static const Phase kAxis[3]={{-1.0f/3.0f,1},{0.0f,0},{1.0f/3.0f,2}};
static const int kSchedule[9][2]={{1,1},{2,1},{2,2},{1,2},{0,2},{0,1},{0,0},{1,0},{2,0}};

static DWORD FloatBits(float x){DWORD d=0;std::memcpy(&d,&x,sizeof(d));return d;}

static HRESULT CreateRT(IDirect3DDevice9* d,UINT w,UINT h,IDirect3DTexture9** t,IDirect3DSurface9** s)
{
    *t=0;*s=0;HRESULT hr=d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t,0);
    if(FAILED(hr)||!*t)return FAILED(hr)?hr:E_FAIL;hr=(*t)->GetSurfaceLevel(0,s);if(FAILED(hr)||!*s){(*t)->Release();*t=0;return FAILED(hr)?hr:E_FAIL;}return S_OK;
}

static DWORD BasePattern(UINT x,UINT y)
{
    // Woven-chair-like high-frequency material: crossing threads plus a slower
    // modulation. The mip chain matters strongly; this is deliberate.
    const int warp=((x/2u)&1u)?220:42;
    const int weft=((y/2u)&1u)?196:55;
    const int diag=(((x+y)/3u)&1u)?38:-22;
    int r=(warp*3+weft)/4+diag;
    int g=(warp+weft*3)/4+diag/2;
    int b=(warp+weft)/2+diag/3;
    r=std::max(0,std::min(255,r));g=std::max(0,std::min(255,g));b=std::max(0,std::min(255,b));
    return 0xFF000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;
}

static DWORD Avg4(DWORD a,DWORD b,DWORD c,DWORD d)
{
    const UINT r=(((a>>16)&255)+((b>>16)&255)+((c>>16)&255)+((d>>16)&255)+2)/4;
    const UINT g=(((a>>8)&255)+((b>>8)&255)+((c>>8)&255)+((d>>8)&255)+2)/4;
    const UINT bl=((a&255)+(b&255)+(c&255)+(d&255)+2)/4;
    return 0xFF000000u|(r<<16)|(g<<8)|bl;
}

static bool BuildMipTexture(IDirect3DDevice9* d,IDirect3DTexture9** out)
{
    *out=0;HRESULT hr=d->CreateTexture(TEX_W,TEX_H,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,out,0);if(FAILED(hr)||!*out)return false;
    UINT w=TEX_W,h=TEX_H;std::vector<DWORD> prev((SIZE_T)w*h);for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)prev[(SIZE_T)y*w+x]=BasePattern(x,y);
    const UINT levels=(*out)->GetLevelCount();
    for(UINT level=0;level<levels;++level){
        D3DSURFACE_DESC desc={};(*out)->GetLevelDesc(level,&desc);D3DLOCKED_RECT lr={};hr=(*out)->LockRect(level,&lr,0,0);if(FAILED(hr))return false;
        for(UINT y=0;y<desc.Height;++y)std::memcpy((BYTE*)lr.pBits+(SIZE_T)y*lr.Pitch,&prev[(SIZE_T)y*desc.Width],desc.Width*4);(*out)->UnlockRect(level);
        if(level+1<levels){UINT nw=std::max(1u,desc.Width/2),nh=std::max(1u,desc.Height/2);std::vector<DWORD> next((SIZE_T)nw*nh);
            for(UINT y=0;y<nh;++y)for(UINT x=0;x<nw;++x){UINT x0=std::min(desc.Width-1,2*x),x1=std::min(desc.Width-1,2*x+1),y0=std::min(desc.Height-1,2*y),y1=std::min(desc.Height-1,2*y+1);next[(SIZE_T)y*nw+x]=Avg4(prev[(SIZE_T)y0*desc.Width+x0],prev[(SIZE_T)y0*desc.Width+x1],prev[(SIZE_T)y1*desc.Width+x0],prev[(SIZE_T)y1*desc.Width+x1]);}prev.swap(next);}
    }return true;
}

static bool DrawTextured(IDirect3DDevice9* d,IDirect3DSurface9* rt,UINT w,UINT h,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* decl,IDirect3DTexture9* tex,float lodBias,const float* c255)
{
    d->SetTexture(0,0);if(FAILED(d->SetRenderTarget(0,rt)))return false;d->SetDepthStencilSurface(0);D3DVIEWPORT9 vp={0,0,w,h,0,1};d->SetViewport(&vp);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,FloatBits(lodBias));d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(ps);d->SetTexture(0,tex);if(c255)d->SetVertexShaderConstantF(255,c255,1);
    const V q[4]={{-1,1,.5f,1,0,0},{1,1,.5f,1,1,0},{-1,-1,.5f,1,0,1},{1,-1,.5f,1,1,1}};
    HRESULT hr=d->BeginScene();if(SUCCEEDED(hr)){hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));HRESULT e=d->EndScene();if(SUCCEEDED(hr))hr=e;}d->SetTexture(0,0);return SUCCEEDED(hr);
}

static bool DrawReservoir(IDirect3DDevice9* d,IDirect3DSurface9* rt,IDirect3DPixelShader9* ps,IDirect3DTexture9* cur,IDirect3DTexture9* prev,float jx,float jy,int rx,int ry)
{
    d->SetTexture(0,0);d->SetTexture(1,0);if(FAILED(d->SetRenderTarget(0,rt)))return false;d->SetDepthStencilSurface(0);D3DVIEWPORT9 vp={0,0,OUT_W,OUT_H,0,1};d->SetViewport(&vp);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);
    for(DWORD s=0;s<2;++s){d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);}
    const float c0[4]={(float)SRC_W,(float)SRC_H,(float)OUT_W,(float)OUT_H},c1[4]={jx,jy,(float)rx,(float)ry};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShaderConstantF(1,c1,1);d->SetVertexShader(0);d->SetVertexDeclaration(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(ps);d->SetTexture(0,cur);d->SetTexture(1,prev);
    const BV q[4]={{-.5f,-.5f,0,1,0,0},{OUT_W-.5f,-.5f,0,1,1,0},{-.5f,OUT_H-.5f,0,1,0,1},{OUT_W-.5f,OUT_H-.5f,0,1,1,1}};HRESULT hr=d->BeginScene();if(SUCCEEDED(hr)){hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(BV));HRESULT e=d->EndScene();if(SUCCEEDED(hr))hr=e;}d->SetTexture(0,0);d->SetTexture(1,0);return SUCCEEDED(hr);
}

static bool Read(IDirect3DDevice9* d,IDirect3DSurface9* s,std::vector<DWORD>& out){IDirect3DSurface9* sys=0;HRESULT hr=d->CreateOffscreenPlainSurface(OUT_W,OUT_H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,0);if(FAILED(hr)||!sys)return false;hr=d->GetRenderTargetData(s,sys);if(FAILED(hr)){sys->Release();return false;}D3DLOCKED_RECT lr={};if(FAILED(sys->LockRect(&lr,0,D3DLOCK_READONLY))){sys->Release();return false;}out.resize((SIZE_T)OUT_W*OUT_H);for(UINT y=0;y<OUT_H;++y)std::memcpy(&out[(SIZE_T)y*OUT_W],(BYTE*)lr.pBits+(SIZE_T)y*lr.Pitch,OUT_W*4);sys->UnlockRect();sys->Release();return true;}

struct Stats{double mae;double lumaMae;unsigned long long changed;int maxd;};
static Stats Compare(const std::vector<DWORD>& a,const std::vector<DWORD>& b){unsigned long long sum=0,changed=0,lumaSum=0,count=0;int mx=0;for(UINT y=3;y<OUT_H-3;++y)for(UINT x=3;x<OUT_W-3;++x){DWORD A=a[(SIZE_T)y*OUT_W+x],B=b[(SIZE_T)y*OUT_W+x];int ar=(A>>16)&255,ag=(A>>8)&255,ab=A&255,br=(B>>16)&255,bg=(B>>8)&255,bb=B&255;int dr=std::abs(ar-br),dg=std::abs(ag-bg),db=std::abs(ab-bb),m=std::max(dr,std::max(dg,db));sum+=(unsigned long long)dr+dg+db;int la=(54*ar+183*ag+19*ab+128)>>8,lb=(54*br+183*bg+19*bb+128)>>8;lumaSum+=std::abs(la-lb);if(m)++changed;mx=std::max(mx,m);++count;}Stats s={(double)sum/(3.0*count),(double)lumaSum/count,changed,mx};return s;}

static bool Run(IDirect3DDevice9* d,IDirect3DVertexShader9* jvs,IDirect3DPixelShader9* sps,IDirect3DPixelShader9* rps,IDirect3DVertexDeclaration9* decl,IDirect3DTexture9* tex,IDirect3DTexture9* src,IDirect3DSurface9* srcS,IDirect3DTexture9* hist[2],IDirect3DSurface9* histS[2],float bias,const std::vector<DWORD>& native,Stats& out)
{
    d->ColorFill(histS[0],0,0);d->ColorFill(histS[1],0,0);int read=0,write=1;for(int p=0;p<9;++p){Phase ax=kAxis[kSchedule[p][0]],ay=kAxis[kSchedule[p][1]];float c255[4]={-2*ax.j/SRC_W,2*ay.j/SRC_H,0,0};if(!DrawTextured(d,srcS,SRC_W,SRC_H,jvs,sps,decl,tex,bias,c255))return false;if(!DrawReservoir(d,histS[write],rps,src,hist[read],ax.j,ay.j,ax.residue,ay.residue))return false;std::swap(read,write);}std::vector<DWORD> got;if(!Read(d,histS[read],got))return false;out=Compare(native,got);return true;
}

int main(){HWND hwnd=CreateWindowExW(0,L"STATIC",L"R15_LOD",WS_OVERLAPPEDWINDOW,0,0,64,64,0,0,GetModuleHandleW(0),0);IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);if(!hwnd||!api)return 2;D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.hDeviceWindow=hwnd;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* d=0;HRESULT hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(hr)||!d)hr=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);if(FAILED(hr)||!d)return 3;
    IDirect3DVertexShader9* nvs=0;d->CreateVertexShader((const DWORD*)g_r15SceneVs,&nvs);std::vector<DWORD> code;PtVsJitterPatchInfo pi={};if(!PtPatchVertexShaderForJitter((const DWORD*)g_r15SceneVs,&code,&pi))return 4;IDirect3DVertexShader9* jvs=0;if(FAILED(d->CreateVertexShader(code.data(),&jvs)))return 5;IDirect3DPixelShader9 *sps=0,*rps=0;if(FAILED(d->CreatePixelShader((const DWORD*)g_r15ScenePs,&sps)))return 6;if(FAILED(d->CreatePixelShader((const DWORD*)g_r15ExactReservoirPs,&rps)))return 7;
    const D3DVERTEXELEMENT9 el[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=0;if(FAILED(d->CreateVertexDeclaration(el,&decl)))return 8;IDirect3DTexture9* tex=0;if(!BuildMipTexture(d,&tex))return 9;
    IDirect3DTexture9 *nt=0,*src=0,*hist[2]={0,0};IDirect3DSurface9 *ns=0,*srcS=0,*hs[2]={0,0};if(FAILED(CreateRT(d,OUT_W,OUT_H,&nt,&ns))||FAILED(CreateRT(d,SRC_W,SRC_H,&src,&srcS)))return 10;for(int i=0;i<2;++i)if(FAILED(CreateRT(d,OUT_W,OUT_H,&hist[i],&hs[i])))return 11;float zero[4]={0,0,0,0};if(!DrawTextured(d,ns,OUT_W,OUT_H,nvs,sps,decl,tex,0.0f,zero))return 12;std::vector<DWORD> native;if(!Read(d,ns,native))return 13;Stats base={},biased={};if(!Run(d,jvs,sps,rps,decl,tex,src,srcS,hist,hs,0.0f,native,base))return 14;if(!Run(d,jvs,sps,rps,decl,tex,src,srcS,hist,hs,NATIVE_LOD_BIAS,native,biased))return 15;
    std::printf("R15_LOD_UNBIASED_MAE=%.9f LUMA=%.9f CHANGED=%llu MAX=%d\n",base.mae,base.lumaMae,base.changed,base.maxd);std::printf("R15_LOD_NATIVEBIAS=%.9f\n",NATIVE_LOD_BIAS);std::printf("R15_LOD_BIASED_MAE=%.9f LUMA=%.9f CHANGED=%llu MAX=%d\n",biased.mae,biased.lumaMae,biased.changed,biased.maxd);double gain=base.mae>0?100.0*(base.mae-biased.mae)/base.mae:0;std::printf("R15_LOD_MAE_GAIN_PCT=%.6f\n",gain);bool pass=biased.mae<base.mae*0.35 && biased.lumaMae<base.lumaMae*0.35;std::printf("R15_NATIVE_LOD_EQUIVALENCE=%s\n",pass?"PASS":"FAIL");
    for(int i=0;i<2;++i){hs[i]->Release();hist[i]->Release();}srcS->Release();src->Release();ns->Release();nt->Release();tex->Release();decl->Release();rps->Release();sps->Release();jvs->Release();nvs->Release();d->Release();api->Release();DestroyWindow(hwnd);return pass?0:16;}
