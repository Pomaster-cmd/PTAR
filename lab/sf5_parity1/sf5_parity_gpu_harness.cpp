#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "../../build_x86/ptar_old_ps_bytecode.h"
#include "../../build_x86/ptar_sf5_parity_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx { float x,y,z,rhw,u,v; };
struct F4 { float r,g,b,a; };

static float Sat(float v){return v<0?0:(v>1?1:v);}
static F4 Add(F4 a,F4 b){return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a};}
static F4 Mul(F4 a,float s){return {a.r*s,a.g*s,a.b*s,a.a*s};}
static F4 Lerp4(F4 a,F4 b,float t){return Add(Mul(a,1.0f-t),Mul(b,t));}
static F4 Min4(F4 a,F4 b){return {std::min(a.r,b.r),std::min(a.g,b.g),std::min(a.b,b.b),std::min(a.a,b.a)};}
static F4 Max4(F4 a,F4 b){return {std::max(a.r,b.r),std::max(a.g,b.g),std::max(a.b,b.b),std::max(a.a,b.a)};}
static F4 Clamp4(F4 v,F4 lo,F4 hi){return {std::max(lo.r,std::min(hi.r,v.r)),std::max(lo.g,std::min(hi.g,v.g)),std::max(lo.b,std::min(hi.b,v.b)),std::max(lo.a,std::min(hi.a,v.a))};}

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=inst; wc.lpszClassName=L"PTAR_SF5_PARITY";
    RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"PTAR SF5 Parity",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,inst,0);
}

static F4 FromARGB(DWORD p)
{
    return {(float)((p>>16)&255)/255.0f,(float)((p>>8)&255)/255.0f,(float)(p&255)/255.0f,(float)((p>>24)&255)/255.0f};
}
static BYTE Q(float v){int x=(int)std::floor(Sat(v)*255.0f+0.5f);return (BYTE)(x<0?0:(x>255?255:x));}
static DWORD ToARGB(F4 p){return ((DWORD)Q(p.a)<<24)|((DWORD)Q(p.r)<<16)|((DWORD)Q(p.g)<<8)|Q(p.b);}
static int ClampI(int v,int n){return v<0?0:(v>=n?n-1:v);}
static F4 Load(const std::vector<DWORD>& src,int w,int h,int x,int y){return FromARGB(src[(size_t)ClampI(y,h)*(size_t)w+(size_t)ClampI(x,w)]);}
static F4 Bilinear(const std::vector<DWORD>& src,int w,int h,float x,float y)
{
    int x0=(int)std::floor(x), y0=(int)std::floor(y); float fx=x-(float)x0, fy=y-(float)y0;
    F4 a=Lerp4(Load(src,w,h,x0,y0),Load(src,w,h,x0+1,y0),fx);
    F4 b=Lerp4(Load(src,w,h,x0,y0+1),Load(src,w,h,x0+1,y0+1),fx);
    return Lerp4(a,b,fy);
}
static F4 K185_13(F4 m,F4 a,F4 b,F4 c){return Add(Add(Mul(m,-0.274074074074074f),Mul(a,0.877777777777778f)),Add(Mul(b,0.533333333333333f),Mul(c,-0.137037037037037f)));}
static F4 K185_23(F4 m,F4 a,F4 b,F4 c){return Add(Add(Mul(m,-0.137037037037037f),Mul(a,0.533333333333333f)),Add(Mul(b,0.877777777777778f),Mul(c,-0.274074074074074f)));}

static F4 Sf5Reference(const std::vector<DWORD>& src,int sw,int sh,int ox,int oy)
{
    float sx=(float)ox*(2.0f/3.0f), sy=(float)oy*(2.0f/3.0f); int fx=(int)std::floor(sx), fy=(int)std::floor(sy);
    float tl=Load(src,sw,sh,fx,fy).g, tr=Load(src,sw,sh,fx+1,fy).g, bl=Load(src,sw,sh,fx,fy+1).g, br=Load(src,sw,sh,fx+1,fy+1).g;
    float gx=(tr+br)-(tl+bl), gy=(bl+br)-(tl+tr); float agx=std::fabs(gx), agy=std::fabs(gy); bool useX=agx>=agy;
    // SF5 reconstructs along the selected axis, while retaining the fractional
    // coordinate on the orthogonal axis for the SampleLevel-linear colour reads.
    float bx=useX?(float)fx:sx, by=useX?sy:(float)fy; float ax=useX?1.0f:0.0f, ay=useX?0.0f:1.0f;
    F4 fm1=Bilinear(src,sw,sh,bx-ax,by-ay), f0=Bilinear(src,sw,sh,bx,by), f1=Bilinear(src,sw,sh,bx+ax,by+ay), f2=Bilinear(src,sw,sh,bx+2*ax,by+2*ay);
    int pc=useX?ox:oy, phase=((pc%3)+3)%3; if(phase==0)return f0; bool phase2=phase>=2;
    F4 edge=phase2?K185_13(fm1,f0,f1,f2):K185_23(fm1,f0,f1,f2); F4 raster=Clamp4(edge,Min4(f0,f1),Max4(f0,f1));
    float phaseFrac=phase2?1.0f/3.0f:2.0f/3.0f; F4 bil=Lerp4(f0,f1,phaseFrac), natural=Lerp4(bil,raster,0.70f);
    float localRange=std::max(std::max(tl,tr),std::max(bl,br))-std::min(std::min(tl,tr),std::min(bl,br)); float gradient=0.5f*std::max(agx,agy); float diagonal=0.5f*std::fabs((tl+br)-(tr+bl)); float coherence=std::fabs(agx-agy)/(agx+agy+1.0e-6f);
    float rangeConf=Sat((localRange-0.01f)/0.10f), coherenceConf=Sat((coherence-0.25f)/0.45f), diagRatio=diagonal/(gradient+1.0e-6f), axisConf=1.0f-Sat((diagRatio-0.08f)/0.45f); float rw=rangeConf*coherenceConf*axisConf;
    float edgeBase=Sat((gradient-0.02f)/0.10f), ew=(1.0f-rw)*edgeBase, nw=1.0f-rw-ew; return Add(Add(Mul(natural,nw),Mul(edge,ew)),Mul(raster,rw));
}

static std::vector<DWORD> Pattern(int kind,int w,int h)
{
    std::vector<DWORD> p((size_t)w*h); for(int y=0;y<h;++y)for(int x=0;x<w;++x){int r=0,g=0,b=0;
        switch(kind){case 0:r=(x*255)/(w-1);g=(y*255)/(h-1);b=(((x/4)^(y/4))&1)?224:31;break;case 1:r=g=b=(x<w/2)?24:232;break;case 2:r=g=b=(y<h/2)?24:232;break;case 3:r=g=b=(x>y+(w-h)/2)?235:20;break;case 4:r=g=b=((x%7)==0||(y%9)==0)?245:18;break;case 5:r=(x<w/2)?245:20;g=((x+y)&7)<4?220:35;b=(x<w/2)?25:240;break;default:r=(x*37+y*17)&255;g=(x*13+y*53)&255;b=(x*71+y*7)&255;break;}
        p[(size_t)y*w+x]=0xFF000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;} return p;
}

static HRESULT Upload(IDirect3DDevice9* dev,const std::vector<DWORD>& src,int w,int h,IDirect3DTexture9** tex)
{
    HRESULT hr=dev->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,tex,0);if(FAILED(hr))return hr;D3DLOCKED_RECT lr={};hr=(*tex)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    for(int y=0;y<h;++y)memcpy((BYTE*)lr.pBits+y*lr.Pitch,&src[(size_t)y*w],(size_t)w*4);(*tex)->UnlockRect(0);return S_OK;
}

static HRESULT Render(IDirect3DDevice9* dev,IDirect3DPixelShader9* ps,IDirect3DTexture9* src,int sw,int sh,int dw,int dh,bool parity,std::vector<DWORD>* out)
{
    IDirect3DTexture9* dst=0;IDirect3DSurface9* ds=0;IDirect3DSurface9* read=0;HRESULT hr=dev->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(hr))return hr;
    hr=dst->GetSurfaceLevel(0,&ds);if(FAILED(hr))return hr;hr=dev->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);if(FAILED(hr))return hr;
    dev->SetDepthStencilSurface(0);dev->SetRenderTarget(0,ds);D3DVIEWPORT9 vp={0,0,(DWORD)dw,(DWORD)dh,0,1};dev->SetViewport(&vp);dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    for(DWORD s=0;s<2;++s){D3DTEXTUREFILTERTYPE f=(parity&&s==0)?D3DTEXF_POINT:D3DTEXF_LINEAR;dev->SetSamplerState(s,D3DSAMP_MINFILTER,f);dev->SetSamplerState(s,D3DSAMP_MAGFILTER,f);dev->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);dev->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
    float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh};dev->SetPixelShaderConstantF(0,c0,1);dev->SetPixelShader(ps);dev->SetVertexShader(0);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);dev->SetTexture(0,src);dev->SetTexture(1,parity?src:0);
    Vtx q[4]={{-0.5f,-0.5f,0,1,0,0},{(float)dw-0.5f,-0.5f,0,1,1,0},{-0.5f,(float)dh-0.5f,0,1,0,1},{(float)dw-0.5f,(float)dh-0.5f,0,1,1,1}};
    hr=dev->BeginScene();if(SUCCEEDED(hr)){hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT eh=dev->EndScene();if(SUCCEEDED(hr))hr=eh;}dev->SetTexture(0,0);dev->SetTexture(1,0);if(FAILED(hr))return hr;
    hr=dev->GetRenderTargetData(ds,read);if(FAILED(hr))return hr;D3DLOCKED_RECT lr={};hr=read->LockRect(&lr,0,D3DLOCK_READONLY);if(FAILED(hr))return hr;out->resize((size_t)dw*dh);for(int y=0;y<dh;++y)memcpy(&(*out)[(size_t)y*dw],(BYTE*)lr.pBits+y*lr.Pitch,(size_t)dw*4);read->UnlockRect();read->Release();ds->Release();dst->Release();return S_OK;
}

struct Err{double mae;int maxe;unsigned over2;};
static Err Compare(const std::vector<DWORD>& got,const std::vector<DWORD>& ref)
{
    uint64_t sum=0,n=0;int mx=0;unsigned over=0;for(size_t i=0;i<got.size();++i)for(int sh=0;sh<=16;sh+=8){int a=(got[i]>>sh)&255,b=(ref[i]>>sh)&255,e=std::abs(a-b);sum+=(uint64_t)e;++n;mx=std::max(mx,e);if(e>2)++over;}return{(double)sum/(double)n,mx,over};
}

int main()
{
    wchar_t path[MAX_PATH]={0};if(!GetSystemDirectoryW(path,MAX_PATH))return 2;wcscat_s(path,L"\\d3d9.dll");HMODULE m=LoadLibraryW(path);if(!m)return 3;PFN_Direct3DCreate9 c=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");if(!c)return 4;IDirect3D9* d=c(D3D_SDK_VERSION);if(!d)return 5;HWND w=MakeWindow();if(!w)return 6;
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* dev=0;HRESULT hr=d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr))hr=d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr)||!dev)return 7;
    IDirect3DPixelShader9 *oldps=0,*newps=0;if(FAILED(dev->CreatePixelShader((DWORD*)g_ptarOldPs,&oldps)))return 8;if(FAILED(dev->CreatePixelShader((DWORD*)g_ptarSf5ParityPs,&newps)))return 9;
    const int sw=48,sh=32,dw=72,dh=48;double oldTotal=0,newTotal=0;int oldMax=0,newMax=0;unsigned improved=0;
    for(int kind=0;kind<7;++kind){std::vector<DWORD> src=Pattern(kind,sw,sh),oldout,newout,ref((size_t)dw*dh);IDirect3DTexture9* tex=0;if(FAILED(Upload(dev,src,sw,sh,&tex)))return 10;for(int y=0;y<dh;++y)for(int x=0;x<dw;++x)ref[(size_t)y*dw+x]=ToARGB(Sf5Reference(src,sw,sh,x,y));if(FAILED(Render(dev,oldps,tex,sw,sh,dw,dh,false,&oldout)))return 11;if(FAILED(Render(dev,newps,tex,sw,sh,dw,dh,true,&newout)))return 12;tex->Release();Err eo=Compare(oldout,ref),en=Compare(newout,ref);oldTotal+=eo.mae;newTotal+=en.mae;oldMax=std::max(oldMax,eo.maxe);newMax=std::max(newMax,en.maxe);if(en.mae+1e-6<eo.mae)++improved;std::printf("SF5_PARITY_PATTERN_%d old_mae=%.6f old_max=%d old_over2=%u new_mae=%.6f new_max=%d new_over2=%u\n",kind,eo.mae,eo.maxe,eo.over2,en.mae,en.maxe,en.over2);}
    std::printf("SF5_PARITY_OLD_MEAN_MAE=%.6f\n",oldTotal/7.0);std::printf("SF5_PARITY_NEW_MEAN_MAE=%.6f\n",newTotal/7.0);std::printf("SF5_PARITY_OLD_MAX=%d\n",oldMax);std::printf("SF5_PARITY_NEW_MAX=%d\n",newMax);std::printf("SF5_PARITY_PATTERNS_IMPROVED=%u\n",improved);
    bool absolute=(newTotal/7.0)<=0.80&&newMax<=5;bool nondestructive=newTotal<=oldTotal+1e-6;std::printf("SF5_PARITY_ABSOLUTE_GATE=%s\n",absolute?"PASS":"FAIL");std::printf("SF5_PARITY_NOT_WORSE_THAN_OLD=%s\n",nondestructive?"PASS":"FAIL");newps->Release();oldps->Release();dev->Release();DestroyWindow(w);d->Release();FreeLibrary(m);return(absolute&&nondestructive)?0:1;
}
