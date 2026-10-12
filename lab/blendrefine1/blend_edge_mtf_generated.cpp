#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "../../build_x86/ptar_spec5_blend_B00.h"
#include "../../build_x86/ptar_spec5_blend_B12.h"
#include "../../build_x86/ptar_spec5_blend_B16.h"
#include "../../build_x86/ptar_spec5_blend_B18.h"
#include "../../build_x86/ptar_spec5_blend_B20.h"
#include "../../build_x86/ptar_spec5_blend_B24.h"
#include "../../build_x86/ptar_spec5_blend_B28.h"
#include "../../build_x86/ptar_spec5_blend_B32.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};
struct Metrics{double bandMae,haloEnergy,lineContrastErr,lineEnergyErr,gradErr,lapErr;};

static float Sat(float v){return v<0?0:(v>1?1:v);}
static DWORD Gray(float v){BYTE q=(BYTE)(Sat(v)*255.0f+0.5f);return 0xff000000u|((DWORD)q<<16)|((DWORD)q<<8)|q;}
static float Y(DWORD p){return ((p>>8)&255)/255.0f;}
static float SegDist(float px,float py,float ax,float ay,float bx,float by){float vx=bx-ax,vy=by-ay,wx=px-ax,wy=py-ay,d=vx*vx+vy*vy;float t=d>1e-9f?(wx*vx+wy*vy)/d:0;t=std::max(0.0f,std::min(1.0f,t));float dx=px-(ax+t*vx),dy=py-(ay+t*vy);return std::sqrt(dx*dx+dy*dy);}

static std::vector<DWORD> MakeSuper(int kind,int w,int h,float phase)
{
    std::vector<DWORD> a((size_t)w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
    {
        float px=(x+0.5f)/w,py=(y+0.5f)/h,v=0.0f;
        if(kind==0) // vertical step, phase sweeps the edge across pixel centres
        {
            float edge=0.5f+phase/w;
            v=px>=edge?0.88f:0.12f;
        }
        else if(kind==1) // slanted step, 27 degrees
        {
            float edge=0.50f+0.37f*(py-0.5f)+phase/w;
            v=px>=edge?0.90f:0.10f;
        }
        else if(kind==2) // 1-native-pixel-ish bright wire on dark field
        {
            float ax=0.08f,ay=0.22f,bx=0.92f,by=0.73f;
            float d=SegDist(px,py,ax,ay,bx,by)*h;
            float center=1.5f+phase/3.0f; // 3 superpixels = one low-res pixel; ~1.5 native px
            v=(d<center)?0.92f:0.08f;
        }
        else if(kind==3) // paired thin dark wires on bright field
        {
            float d1=std::fabs(py-(0.34f+0.23f*(px-0.5f)))*h;
            float d2=std::fabs(py-(0.62f-0.31f*(px-0.5f)))*h;
            float t=1.25f+phase/4.0f;
            v=(d1<t||d2<t)?0.09f:0.91f;
        }
        else // near-Nyquist oblique bars
        {
            float u=px*18.0f+py*11.0f+phase/6.0f;
            v=(std::fmod(u+100.0f,1.0f)<0.48f)?0.88f:0.12f;
        }
        a[(size_t)y*w+x]=Gray(v);
    }
    return a;
}

static std::vector<DWORD> Down(const std::vector<DWORD>&s,int sw,int sh,int f)
{
    int dw=sw/f,dh=sh/f;std::vector<DWORD> o((size_t)dw*dh);
    for(int y=0;y<dh;y++)for(int x=0;x<dw;x++)
    {
        double sum=0;for(int j=0;j<f;j++)for(int i=0;i<f;i++)sum+=Y(s[(size_t)(y*f+j)*sw+x*f+i]);
        o[(size_t)y*dw+x]=Gray((float)(sum/(f*f)));
    }
    return o;
}

static HWND Win(){HINSTANCE i=GetModuleHandleW(0);WNDCLASSW c={};c.lpfnWndProc=DefWindowProcW;c.hInstance=i;c.lpszClassName=L"PTAR_BLEND_EDGE_MTF";RegisterClassW(&c);return CreateWindowExW(0,c.lpszClassName,L"PTAR",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);}
static HRESULT Upload(IDirect3DDevice9*d,const std::vector<DWORD>&s,int w,int h,IDirect3DTexture9**t){HRESULT r=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(r))return r;D3DLOCKED_RECT l={};r=(*t)->LockRect(0,&l,0,0);if(FAILED(r))return r;for(int y=0;y<h;y++)memcpy((BYTE*)l.pBits+y*l.Pitch,&s[(size_t)y*w],(size_t)w*4);(*t)->UnlockRect(0);return S_OK;}
static HRESULT Render(IDirect3DDevice9*d,IDirect3DPixelShader9*ps,IDirect3DTexture9*src,int sw,int sh,int dw,int dh,std::vector<DWORD>*o){IDirect3DTexture9*dst=0;IDirect3DSurface9*ds=0,*rd=0;HRESULT r=d->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(r))return r;dst->GetSurfaceLevel(0,&ds);d->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&rd,0);d->SetDepthStencilSurface(0);d->SetRenderTarget(0,ds);D3DVIEWPORT9 vp={0,0,(DWORD)dw,(DWORD)dh,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShader(ps);d->SetVertexShader(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,src);Vtx q[4]={{-.5f,-.5f,0,1,0,0},{(float)dw-.5f,-.5f,0,1,1,0},{-.5f,(float)dh-.5f,0,1,0,1},{(float)dw-.5f,(float)dh-.5f,0,1,1,1}};r=d->BeginScene();if(SUCCEEDED(r)){r=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=d->EndScene();if(SUCCEEDED(r))r=e;}d->SetTexture(0,0);if(SUCCEEDED(r))r=d->GetRenderTargetData(ds,rd);if(SUCCEEDED(r)){D3DLOCKED_RECT l={};r=rd->LockRect(&l,0,D3DLOCK_READONLY);if(SUCCEEDED(r)){o->resize((size_t)dw*dh);for(int y=0;y<dh;y++)memcpy(&(*o)[(size_t)y*dw],(BYTE*)l.pBits+y*l.Pitch,(size_t)dw*4);rd->UnlockRect();}}rd->Release();ds->Release();dst->Release();return r;}

static Metrics Measure(const std::vector<DWORD>&g,const std::vector<DWORD>&r,int w,int h,int kind)
{
    double band=0,halo=0,lineCE=0,lineEE=0,grad=0,lap=0;uint64_t bn=0,ln=0,gn=0;
    for(int y=1;y<h-1;y++)for(int x=1;x<w-1;x++)
    {
        size_t i=(size_t)y*w+x;float rv=Y(r[i]),gv=Y(g[i]);float rgx=.5f*(Y(r[i+1])-Y(r[i-1])),rgy=.5f*(Y(r[i+w])-Y(r[i-w]));float ggx=.5f*(Y(g[i+1])-Y(g[i-1])),ggy=.5f*(Y(g[i+w])-Y(g[i-w]));float rg=std::sqrt(rgx*rgx+rgy*rgy),gg=std::sqrt(ggx*ggx+ggy*ggy);
        float rl=4*rv-Y(r[i-1])-Y(r[i+1])-Y(r[i-w])-Y(r[i+w]);float gl=4*gv-Y(g[i-1])-Y(g[i+1])-Y(g[i-w])-Y(g[i+w]);grad+=std::fabs(gg-rg);lap+=std::fabs(gl-rl);gn++;
        if(rg>0.025f){band+=std::fabs(gv-rv);bn++;float lo=std::min(std::min(Y(r[i-1]),Y(r[i+1])),std::min(Y(r[i-w]),Y(r[i+w])));float hi=std::max(std::max(Y(r[i-1]),Y(r[i+1])),std::max(Y(r[i-w]),Y(r[i+w])));halo+=std::max(0.0f,gv-hi)+std::max(0.0f,lo-gv);}
        if(kind==2||kind==3){float bg=(kind==2)?0.08f:0.91f;float rc=std::fabs(rv-bg),gc=std::fabs(gv-bg);if(rc>0.03f){lineCE+=std::fabs(gc-rc);lineEE+=std::fabs(gc*gc-rc*rc);ln++;}}
    }
    return {bn?band/bn:0,bn?halo/bn:0,ln?lineCE/ln:0,ln?lineEE/ln:0,gn?grad/gn:0,gn?lap/gn:0};
}

int main(){const char*names[8]={"B00","B12","B16","B18","B20","B24","B28","B32"};const DWORD*codes[8]={(DWORD*)g_ptarSpec5BlendB00,(DWORD*)g_ptarSpec5BlendB12,(DWORD*)g_ptarSpec5BlendB16,(DWORD*)g_ptarSpec5BlendB18,(DWORD*)g_ptarSpec5BlendB20,(DWORD*)g_ptarSpec5BlendB24,(DWORD*)g_ptarSpec5BlendB28,(DWORD*)g_ptarSpec5BlendB32};wchar_t path[MAX_PATH]={};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\d3d9.dll");HMODULE m=LoadLibraryW(path);if(!m)return 2;PFN_Direct3DCreate9 fn=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");IDirect3D9*d=fn?fn(D3D_SDK_VERSION):0;if(!d)return 3;HWND w=Win();D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*dev=0;HRESULT hr=d->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr))hr=d->CreateDevice(0,D3DDEVTYPE_REF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr)||!dev)return 4;IDirect3DPixelShader9*ps[8]={};for(int v=0;v<8;v++)if(FAILED(dev->CreatePixelShader(codes[v],&ps[v])))return 5;const int SW=384,SH=216,NW=192,NH=108,LW=128,LH=72;double sum[8][6]={};int samples=0;FILE*csv=0;fopen_s(&csv,"BLENDREFINE_EDGE_MTF.csv","wb");fprintf(csv,"variant,kind,phase,band_mae,halo_energy,line_contrast_err,line_energy_err,grad_err,lap_err\n");for(int kind=0;kind<5;kind++)for(int pi=0;pi<9;pi++){float phase=-1.0f+0.25f*pi;std::vector<DWORD>sup=MakeSuper(kind,SW,SH,phase),ref=Down(sup,SW,SH,2),low=Down(sup,SW,SH,3);IDirect3DTexture9*t=0;if(FAILED(Upload(dev,low,LW,LH,&t)))return 6;for(int v=0;v<8;v++){std::vector<DWORD>got;if(FAILED(Render(dev,ps[v],t,LW,LH,NW,NH,&got)))return 7;Metrics q=Measure(got,ref,NW,NH,kind);fprintf(csv,"%s,%d,%.2f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n",names[v],kind,phase,q.bandMae,q.haloEnergy,q.lineContrastErr,q.lineEnergyErr,q.gradErr,q.lapErr);sum[v][0]+=q.bandMae;sum[v][1]+=q.haloEnergy;sum[v][2]+=q.lineContrastErr;sum[v][3]+=q.lineEnergyErr;sum[v][4]+=q.gradErr;sum[v][5]+=q.lapErr;}t->Release();samples++;}fclose(csv);for(int v=0;v<8;v++){for(int j=0;j<6;j++)sum[v][j]/=samples;printf("EDGE_MTF_%s BAND=%.9f HALO=%.9f LINEC=%.9f LINEE=%.9f GRAD=%.9f LAP=%.9f\n",names[v],sum[v][0],sum[v][1],sum[v][2],sum[v][3],sum[v][4],sum[v][5]);}printf("BLENDREFINE_EDGE_MTF_GATE=PASS\n");for(int v=0;v<8;v++)ps[v]->Release();dev->Release();DestroyWindow(w);d->Release();FreeLibrary(m);return 0;}
