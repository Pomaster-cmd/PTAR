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
#include "../../build_x86/ptar_qstage4diag2_d3d9_ps_bytecode.h"
#include "../../build_x86/ptar_spec5_d3d9_ps_bytecode.h"
#include "../../build_x86/ptar_hq900_d3d9_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};
struct F4{float r,g,b,a;};
struct Err{double mae;int maxe;unsigned over2;};

static float Sat(float v){return v<0.0f?0.0f:(v>1.0f?1.0f:v);}
static F4 Add(F4 a,F4 b){return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a};}
static F4 Sub(F4 a,F4 b){return {a.r-b.r,a.g-b.g,a.b-b.b,a.a-b.a};}
static F4 Mul(F4 a,float s){return {a.r*s,a.g*s,a.b*s,a.a*s};}
static F4 Lerp4(F4 a,F4 b,float t){return Add(Mul(a,1.0f-t),Mul(b,t));}
static F4 Min4(F4 a,F4 b){return {std::min(a.r,b.r),std::min(a.g,b.g),std::min(a.b,b.b),std::min(a.a,b.a)};}
static F4 Max4(F4 a,F4 b){return {std::max(a.r,b.r),std::max(a.g,b.g),std::max(a.b,b.b),std::max(a.a,b.a)};}
static F4 Clamp4(F4 v,F4 lo,F4 hi){return {std::max(lo.r,std::min(hi.r,v.r)),std::max(lo.g,std::min(hi.g,v.g)),std::max(lo.b,std::min(hi.b,v.b)),std::max(lo.a,std::min(hi.a,v.a))};}
static float Luma(F4 p){return p.r*0.299f+p.g*0.587f+p.b*0.114f;}
static int ClampI(int v,int n){return v<0?0:(v>=n?n-1:v);}
static F4 FromARGB(DWORD p){return {(float)((p>>16)&255)/255.0f,(float)((p>>8)&255)/255.0f,(float)(p&255)/255.0f,(float)((p>>24)&255)/255.0f};}
static BYTE Quant(float v){int x=(int)std::floor(Sat(v)*255.0f+0.5f);return (BYTE)ClampI(x,256);}
static DWORD ToARGB(F4 p){return 0xff000000u|((DWORD)Quant(p.r)<<16)|((DWORD)Quant(p.g)<<8)|(DWORD)Quant(p.b);}
static F4 Load(const std::vector<DWORD>&s,int w,int h,int x,int y){return FromARGB(s[(size_t)ClampI(y,h)*(size_t)w+(size_t)ClampI(x,w)]);}
static F4 Bilinear(const std::vector<DWORD>&s,int w,int h,float x,float y){int x0=(int)std::floor(x),y0=(int)std::floor(y);float fx=x-x0,fy=y-y0;return Lerp4(Lerp4(Load(s,w,h,x0,y0),Load(s,w,h,x0+1,y0),fx),Lerp4(Load(s,w,h,x0,y0+1),Load(s,w,h,x0+1,y0+1),fx),fy);}

static F4 K130_13(F4 m,F4 a,F4 b,F4 c){return Add(Add(Mul(m,-0.356296296296296f),Mul(a,0.941111111111111f)),Add(Mul(b,0.593333333333333f),Mul(c,-0.178148148148148f)));}
static F4 K130_23(F4 m,F4 a,F4 b,F4 c){return Add(Add(Mul(m,-0.178148148148148f),Mul(a,0.593333333333333f)),Add(Mul(b,0.941111111111111f),Mul(c,-0.356296296296296f)));}

static F4 RefQstage4(const std::vector<DWORD>&s,int sw,int sh,int ox,int oy)
{
    const float sx=(float)ox*(2.0f/3.0f),sy=(float)oy*(2.0f/3.0f);
    const int fx=(int)std::floor(sx),fy=(int)std::floor(sy);
    const float tl=Load(s,sw,sh,fx,fy).g,tr=Load(s,sw,sh,fx+1,fy).g,bl=Load(s,sw,sh,fx,fy+1).g,br=Load(s,sw,sh,fx+1,fy+1).g;
    const float gx=(tr+br)-(tl+bl),gy=(bl+br)-(tl+tr),agx=std::fabs(gx),agy=std::fabs(gy);
    const bool useX=agx>=agy;
    const float bx=useX?(float)fx:sx,by=useX?sy:(float)fy;
    const F4 f0=Bilinear(s,sw,sh,bx,by);
    const int phase=(useX?ox:oy)%3;
    if(phase==0) return f0;

    const float diagA=tl-br,diagB=tr-bl;
    const bool selectB=std::fabs(diagB)>=std::fabs(diagA);
    const float dx=-1.0f,dy=selectB?-1.0f:1.0f;
    const F4 dm=Bilinear(s,sw,sh,bx+dx,by+dy);
    const F4 dp=Bilinear(s,sw,sh,bx-dx,by-dy);
    const F4 residual=Sub(f0,Mul(Add(dm,dp),0.5f));

    const float ax=useX?1.0f:0.0f,ay=useX?0.0f:1.0f;
    const F4 fm1=Bilinear(s,sw,sh,bx-ax,by-ay),f1=Bilinear(s,sw,sh,bx+ax,by+ay),f2=Bilinear(s,sw,sh,bx+2.0f*ax,by+2.0f*ay);
    const F4 edge=phase==2?K130_13(fm1,f0,f1,f2):K130_23(fm1,f0,f1,f2);
    const F4 raster=Clamp4(edge,Min4(f0,f1),Max4(f0,f1));
    const float phaseFrac=phase==1?2.0f/3.0f:1.0f/3.0f;
    F4 natural=Lerp4(Lerp4(f0,f1,phaseFrac),raster,0.90f);

    const float localRange=std::max(std::max(tl,tr),std::max(bl,br))-std::min(std::min(tl,tr),std::min(bl,br));
    const float cardinal=std::max(agx,agy),gradient=0.5f*cardinal,diagonal=0.5f*std::fabs((tr+bl)-(tl+br));
    const float coherence=std::fabs(agx-agy)/(agx+agy+1.0e-6f);
    const float rangeConf=Sat((localRange-0.01f)*10.0f);
    const float coherenceConf=Sat((coherence-0.25f)*2.2222222222f);
    const float axisConf=1.0f-Sat((diagonal/(gradient+1.0e-6f)-0.08f)*2.2222222222f);
    const float rasterWeight=rangeConf*coherenceConf*axisConf;
    const float edgeBase=Sat((gradient-0.02f)*10.0f);
    const float edgeWeight=(1.0f-rasterWeight)*edgeBase;
    const float naturalWeight=(1.0f-rasterWeight)*(1.0f-edgeBase);
    const float diagGate=Sat((std::fabs(std::fabs(diagB)-std::fabs(diagA))/(cardinal+1.0e-6f)-0.10f)*2.5f);
    natural=Add(natural,Mul(residual,(1.0f-edgeBase)*diagGate*naturalWeight*0.125f));
    return Add(Add(Mul(natural,naturalWeight),Mul(edge,edgeWeight)),Mul(raster,rasterWeight));
}

static F4 RefU(const std::vector<DWORD>&s,int sw,int sh,float x,float y,float sharp)
{
    const int qx=(int)std::floor(x),qy=(int)std::floor(y);
    const float fx=x-qx,fy=y-qy;
    const F4 c=Load(s,sw,sh,qx,qy),d=Load(s,sw,sh,qx+1,qy),e=Load(s,sw,sh,qx,qy+1),h=Load(s,sw,sh,qx+1,qy+1);
    const F4 b=Lerp4(Lerp4(c,d,fx),Lerp4(e,h,fx),fy);
    const float E=Sat((std::fabs(Luma(d)-Luma(c))+std::fabs(Luma(h)-Luma(e))+std::fabs(Luma(e)-Luma(c))+std::fabs(Luma(h)-Luma(d)))*1.6f);
    const F4 lo=Min4(Min4(c,d),Min4(e,h)),hi=Max4(Max4(c,d),Max4(e,h));
    const float sx=fx*fx*(3.0f-2.0f*fx),sy=fy*fy*(3.0f-2.0f*fy);
    const F4 smooth=Lerp4(Lerp4(c,d,sx),Lerp4(e,h,sx),sy);
    const float g=Sat((sharp-1.0f)*(0.35f+0.75f*E))*1.15f;
    return Clamp4(Add(b,Mul(Sub(smooth,b),g)),lo,hi);
}

static F4 RefSpec5(const std::vector<DWORD>&s,int sw,int sh,int dw,int dh,int ox,int oy)
{
    const float px=((ox+0.5f)*sw/(float)dw)-0.5f,py=((oy+0.5f)*sh/(float)dh)-0.5f;
    const float dx=2.0f*sw/(float)dw,dy=2.0f*sh/(float)dh;
    const F4 c=RefU(s,sw,sh,px,py,2.88f),R=RefU(s,sw,sh,px+dx,py,2.88f),L=RefU(s,sw,sh,px-dx,py,2.88f),B=RefU(s,sw,sh,px,py+dy,2.88f),T=RefU(s,sw,sh,px,py-dy,2.88f);
    const F4 n=Mul(Add(Add(R,L),Add(B,T)),0.25f);
    const float x=std::fabs(Luma(R)-Luma(L)),y=std::fabs(Luma(B)-Luma(T)),hp=std::fabs(Luma(c)-Luma(n));
    const float detail=Sat((hp-0.0008f)*100.0f)*Sat(1.0f-std::max(x,y)/0.15f)*(1.0f-std::fabs(x-y)/(x+y+0.01f));
    F4 out=Add(c,Mul(Sub(c,n),0.40f+0.25f*detail));
    out.r=Sat(out.r);out.g=Sat(out.g);out.b=Sat(out.b);out.a=c.a;
    return out;
}

static F4 RefHq900(const std::vector<DWORD>&s,int sw,int sh,int dw,int dh,int ox,int oy)
{
    const float sharp=0.96f;
    const float px=((ox+0.5f)*sw/(float)dw)-0.5f,py=((oy+0.5f)*sh/(float)dh)-0.5f;
    const int qx=(int)std::floor(px),qy=(int)std::floor(py);
    const float fx=px-qx,fy=py-qy;
    const F4 c=Load(s,sw,sh,qx,qy),d=Load(s,sw,sh,qx+1,qy),e=Load(s,sw,sh,qx,qy+1),h=Load(s,sw,sh,qx+1,qy+1);
    const F4 b=Lerp4(Lerp4(c,d,fx),Lerp4(e,h,fx),fy);
    const float E=Sat((std::fabs(Luma(d)-Luma(c))+std::fabs(Luma(h)-Luma(e))+std::fabs(Luma(e)-Luma(c))+std::fabs(Luma(h)-Luma(d)))*1.6f);
    const F4 lo=Min4(Min4(c,d),Min4(e,h)),hi=Max4(Max4(c,d),Max4(e,h));
    const F4 N=Load(s,sw,sh,(int)std::floor(px+0.5f),(int)std::floor(py+0.5f));
    return Clamp4(Add(b,Mul(Sub(N,b),sharp*(0.12f+0.38f*E))),lo,hi);
}

static std::vector<DWORD> Pattern(int kind,int w,int h)
{
    std::vector<DWORD> p((size_t)w*h);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)
    {
        int r=0,g=0,b=0;
        switch(kind)
        {
        case 0:r=(x*255)/(w-1);g=(y*255)/(h-1);b=(((x/4)^(y/4))&1)?230:24;break;
        case 1:r=g=b=x<w/2?20:235;break;
        case 2:r=g=b=y<h/2?20:235;break;
        case 3:r=g=b=x>y+(w-h)/2?238:18;break;
        case 4:r=g=b=((x%7)==0||(y%9)==0)?245:16;break;
        case 5:r=(x<w/2)?245:15;g=((x+y)&7)<4?225:30;b=(x<w/2)?25:240;break;
        default:r=(x*37+y*17)&255;g=(x*13+y*53)&255;b=(x*71+y*7)&255;break;
        }
        p[(size_t)y*w+x]=0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;
    }
    return p;
}

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=inst;wc.lpszClassName=L"PTAR_FULLQUALITY_ORACLE_V2";
    RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"PTAR",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,inst,0);
}

static HRESULT Upload(IDirect3DDevice9* dev,const std::vector<DWORD>&src,int w,int h,IDirect3DTexture9**tex)
{
    HRESULT hr=dev->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,tex,0);if(FAILED(hr))return hr;
    D3DLOCKED_RECT lr={};hr=(*tex)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    for(int y=0;y<h;++y)memcpy((BYTE*)lr.pBits+y*lr.Pitch,&src[(size_t)y*w],(size_t)w*4);
    (*tex)->UnlockRect(0);return S_OK;
}

static HRESULT Render(IDirect3DDevice9* dev,IDirect3DPixelShader9* ps,IDirect3DTexture9* src,int sw,int sh,int dw,int dh,std::vector<DWORD>* out)
{
    IDirect3DTexture9* dst=0;IDirect3DSurface9* ds=0;IDirect3DSurface9* read=0;
    HRESULT hr=dev->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(hr))return hr;
    hr=dst->GetSurfaceLevel(0,&ds);if(FAILED(hr)){dst->Release();return hr;}
    hr=dev->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);if(FAILED(hr)){ds->Release();dst->Release();return hr;}
    dev->SetDepthStencilSurface(0);dev->SetRenderTarget(0,ds);D3DVIEWPORT9 vp={0,0,(DWORD)dw,(DWORD)dh,0,1};dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh};dev->SetPixelShaderConstantF(0,c0,1);dev->SetPixelShader(ps);dev->SetVertexShader(0);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);dev->SetTexture(0,src);
    Vtx q[4]={{-0.5f,-0.5f,0,1,0,0},{(float)dw-0.5f,-0.5f,0,1,1,0},{-0.5f,(float)dh-0.5f,0,1,0,1},{(float)dw-0.5f,(float)dh-0.5f,0,1,1,1}};
    hr=dev->BeginScene();if(SUCCEEDED(hr)){hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT endHr=dev->EndScene();if(SUCCEEDED(hr))hr=endHr;}
    dev->SetTexture(0,0);
    if(SUCCEEDED(hr))hr=dev->GetRenderTargetData(ds,read);
    if(SUCCEEDED(hr)){D3DLOCKED_RECT lr={};hr=read->LockRect(&lr,0,D3DLOCK_READONLY);if(SUCCEEDED(hr)){out->resize((size_t)dw*dh);for(int y=0;y<dh;++y)memcpy(&(*out)[(size_t)y*dw],(BYTE*)lr.pBits+y*lr.Pitch,(size_t)dw*4);read->UnlockRect();}}
    read->Release();ds->Release();dst->Release();return hr;
}

static Err Compare(const std::vector<DWORD>&got,const std::vector<DWORD>&ref)
{
    uint64_t sum=0,n=0;int maxe=0;unsigned over2=0;
    for(size_t i=0;i<got.size();++i) for(int shift=0;shift<=16;shift+=8)
    {int a=(got[i]>>shift)&255,b=(ref[i]>>shift)&255,e=std::abs(a-b);sum+=(uint64_t)e;++n;maxe=std::max(maxe,e);if(e>2)++over2;}
    return {(double)sum/(double)n,maxe,over2};
}

static bool RunProfile(IDirect3DDevice9*dev,IDirect3DPixelShader9*ps,const char*name,int sw,int sh,int dw,int dh,int profile)
{
    double sumMae=0.0;int maxErr=0;unsigned totalOver2=0;
    for(int kind=0;kind<7;++kind)
    {
        std::vector<DWORD> src=Pattern(kind,sw,sh),got,ref((size_t)dw*dh);IDirect3DTexture9* tex=0;
        HRESULT hr=Upload(dev,src,sw,sh,&tex);if(FAILED(hr)){std::printf("%s_UPLOAD_HR=0x%08lX\n",name,(unsigned long)hr);return false;}
        for(int y=0;y<dh;++y)for(int x=0;x<dw;++x)
        {
            F4 p=profile==0?RefQstage4(src,sw,sh,x,y):(profile==1?RefSpec5(src,sw,sh,dw,dh,x,y):RefHq900(src,sw,sh,dw,dh,x,y));
            ref[(size_t)y*dw+x]=ToARGB(p);
        }
        hr=Render(dev,ps,tex,sw,sh,dw,dh,&got);tex->Release();
        if(FAILED(hr)){std::printf("%s_RENDER_HR=0x%08lX\n",name,(unsigned long)hr);return false;}
        Err e=Compare(got,ref);sumMae+=e.mae;maxErr=std::max(maxErr,e.maxe);totalOver2+=e.over2;
        std::printf("%s_PATTERN_%d_MAE=%.6f MAX=%d OVER2=%u\n",name,kind,e.mae,e.maxe,e.over2);
    }
    const double mean=sumMae/7.0;
    std::printf("%s_MEAN_MAE=%.6f MAX=%d OVER2=%u\n",name,mean,maxErr,totalOver2);
    const bool ok=mean<=0.10 && maxErr<=2 && totalOver2==0;
    std::printf("%s_PARITY=%s\n",name,ok?"PASS":"FAIL");
    return ok;
}

int main()
{
    wchar_t path[MAX_PATH]={0};if(!GetSystemDirectoryW(path,MAX_PATH))return 2;wcscat_s(path,L"\\d3d9.dll");HMODULE module=LoadLibraryW(path);if(!module)return 3;
    PFN_Direct3DCreate9 create9=(PFN_Direct3DCreate9)GetProcAddress(module,"Direct3DCreate9");if(!create9)return 4;IDirect3D9* d3d=create9(D3D_SDK_VERSION);if(!d3d)return 5;
    HWND hwnd=MakeWindow();if(!hwnd)return 6;
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=hwnd;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev=0;D3DDEVTYPE usedType=D3DDEVTYPE_HAL;HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    std::printf("DEVICE_HAL_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr)){usedType=D3DDEVTYPE_REF;hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);std::printf("DEVICE_REF_HR=0x%08lX\n",(unsigned long)hr);}
    if(FAILED(hr)||!dev){DestroyWindow(hwnd);d3d->Release();FreeLibrary(module);return 7;}
    D3DCAPS9 caps={};hr=dev->GetDeviceCaps(&caps);if(FAILED(hr))return 8;
    std::printf("DEVICE_TYPE=%s\n",usedType==D3DDEVTYPE_HAL?"HAL":"REF");
    std::printf("PIXEL_SHADER_VERSION=0x%08lX\n",(unsigned long)caps.PixelShaderVersion);
    std::printf("MAX_PS30_SLOTS=%lu\n",(unsigned long)caps.MaxPixelShader30InstructionSlots);

    IDirect3DPixelShader9 *q4=0,*spec5=0,*hq900=0;
    HRESULT q4hr=dev->CreatePixelShader((const DWORD*)g_ptarQstage4Diag2Ps,&q4);std::printf("CREATE_Q4_HR=0x%08lX\n",(unsigned long)q4hr);
    HRESULT s5hr=dev->CreatePixelShader((const DWORD*)g_ptarSpec5Ps,&spec5);std::printf("CREATE_SPEC5_HR=0x%08lX\n",(unsigned long)s5hr);
    HRESULT h9hr=dev->CreatePixelShader((const DWORD*)g_ptarHq900Ps,&hq900);std::printf("CREATE_HQ900_HR=0x%08lX\n",(unsigned long)h9hr);
    if(FAILED(q4hr)||FAILED(s5hr)||FAILED(h9hr)||!q4||!spec5||!hq900){if(q4)q4->Release();if(spec5)spec5->Release();if(hq900)hq900->Release();dev->Release();DestroyWindow(hwnd);d3d->Release();FreeLibrary(module);return 9;}

    bool q4ok=RunProfile(dev,q4,"QSTAGE4",48,32,72,48,0);
    bool s5ok=RunProfile(dev,spec5,"SPEC5",48,32,72,48,1);
    bool h9ok=RunProfile(dev,hq900,"HQ900",64,36,80,45,2);
    const bool all=q4ok&&s5ok&&h9ok;
    std::printf("FULLQUALITY_PIXEL_PARITY=%s\n",all?"PASS":"FAIL");

    hq900->Release();spec5->Release();q4->Release();dev->Release();DestroyWindow(hwnd);d3d->Release();FreeLibrary(module);
    return all?0:1;
}
