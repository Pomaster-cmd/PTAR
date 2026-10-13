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
#include "../../build_x86/ptar_bicubic16_r7_oracle_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};
struct F4{float r,g,b,a;};
struct Err{double mae;int maxe;unsigned over2;};

static float Sat(float v){return v<0.0f?0.0f:(v>1.0f?1.0f:v);}
static int ClampI(int v,int n){return v<0?0:(v>=n?n-1:v);}
static float Luma(F4 c){return c.r*0.2126f+c.g*0.7152f+c.b*0.0722f;}
static F4 Add(F4 a,F4 b){return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a};}
static F4 Sub(F4 a,F4 b){return {a.r-b.r,a.g-b.g,a.b-b.b,a.a-b.a};}
static F4 Mul(F4 a,float s){return {a.r*s,a.g*s,a.b*s,a.a*s};}
static F4 Min4(F4 a,F4 b){return {std::min(a.r,b.r),std::min(a.g,b.g),std::min(a.b,b.b),std::min(a.a,b.a)};}
static F4 Max4(F4 a,F4 b){return {std::max(a.r,b.r),std::max(a.g,b.g),std::max(a.b,b.b),std::max(a.a,b.a)};}
static F4 Clamp4(F4 v,F4 lo,F4 hi){return {std::max(lo.r,std::min(v.r,hi.r)),std::max(lo.g,std::min(v.g,hi.g)),std::max(lo.b,std::min(v.b,hi.b)),std::max(lo.a,std::min(v.a,hi.a))};}
static F4 FromARGB(DWORD p){return {(float)((p>>16)&255)/255.0f,(float)((p>>8)&255)/255.0f,(float)(p&255)/255.0f,(float)((p>>24)&255)/255.0f};}
static BYTE Quant(float v){int x=(int)std::floor(Sat(v)*255.0f+0.5f);return (BYTE)ClampI(x,256);}
static DWORD ToARGB(F4 p){return ((DWORD)Quant(p.a)<<24)|((DWORD)Quant(p.r)<<16)|((DWORD)Quant(p.g)<<8)|(DWORD)Quant(p.b);}
static F4 Load(const std::vector<DWORD>&s,int w,int h,int x,int y){return FromARGB(s[(size_t)ClampI(y,h)*(size_t)w+(size_t)ClampI(x,w)]);}

static void Keys(float f,float a,float w[4])
{
    const float f2=f*f,f3=f2*f;
    w[0]=a*(f-2.0f*f2+f3);
    w[1]=1.0f-(a+3.0f)*f2+(a+2.0f)*f3;
    w[2]=-a*f+(2.0f*a+3.0f)*f2-(a+2.0f)*f3;
    w[3]=a*(f2-f3);
}

static float PhaseClass(float f)
{
    const float phase=4.0f*f*(1.0f-f);
    return Sat((phase-0.5555556f)*2.25f);
}

static F4 RefR7(const std::vector<DWORD>&s,int sw,int sh,int dw,int dh,int ox,int oy)
{
    const float px=((ox+0.5f)*sw/(float)dw)-0.5f;
    const float py=((oy+0.5f)*sh/(float)dh)-0.5f;
    const int qx=(int)std::floor(px),qy=(int)std::floor(py);
    const float fx=px-qx,fy=py-qy;

    const F4 s11=Load(s,sw,sh,qx,qy);
    const F4 s21=Load(s,sw,sh,qx+1,qy);
    const F4 s12=Load(s,sw,sh,qx,qy+1);
    const F4 s22=Load(s,sw,sh,qx+1,qy+1);
    const float l11=Luma(s11),l21=Luma(s21),l12=Luma(s12),l22=Luma(s22);
    const float gx=0.5f*((l21-l11)+(l22-l12));
    const float gy=0.5f*((l12-l11)+(l22-l21));
    const float edgeX=Sat((std::fabs(gx)-0.02f)*4.0f);
    const float edgeY=Sat((std::fabs(gy)-0.02f)*4.0f);
    const float baseAx=-1.35f+0.15f*PhaseClass(fx);
    const float baseAy=-1.35f+0.15f*PhaseClass(fy);
    const float ax=baseAx+(-0.50f-baseAx)*edgeX;
    const float ay=baseAy+(-0.50f-baseAy)*edgeY;

    float wx[4],wy[4];Keys(fx,ax,wx);Keys(fy,ay,wy);
    F4 rows[4]={{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    for(int j=0;j<4;++j)
        for(int i=0;i<4;++i)
            rows[j]=Add(rows[j],Mul(Load(s,sw,sh,qx+i-1,qy+j-1),wx[i]));
    F4 cubic={0,0,0,0};
    for(int j=0;j<4;++j)cubic=Add(cubic,Mul(rows[j],wy[j]));

    const float lmin=std::min(std::min(l11,l21),std::min(l12,l22));
    const float lmax=std::max(std::max(l11,l21),std::max(l12,l22));
    const float edge=Sat(((lmax-lmin)-0.03f)*6.6666667f);
    const float margin=0.12f+(0.04f-0.12f)*edge;
    const F4 lo=Min4(Min4(s11,s21),Min4(s12,s22));
    const F4 hi=Max4(Max4(s11,s21),Max4(s12,s22));
    const F4 range=Sub(hi,lo);
    F4 out=Clamp4(cubic,Sub(lo,Mul(range,margin)),Add(hi,Mul(range,margin)));
    out.r=Sat(out.r);out.g=Sat(out.g);out.b=Sat(out.b);out.a=Sat(out.a);
    return out;
}

static std::vector<DWORD> Pattern(int kind,int w,int h)
{
    std::vector<DWORD> p((size_t)w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
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
        case 6:r=(x*37+y*17)&255;g=(x*13+y*53)&255;b=(x*71+y*7)&255;break;
        default:r=g=b=((x+y)%5==0)?250:10;break;
        }
        p[(size_t)y*w+x]=0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;
    }
    return p;
}

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);WNDCLASSW wc={};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=inst;wc.lpszClassName=L"PTAR_BIC16_R7_ORACLE";RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"PTAR",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,inst,0);
}

static HRESULT Upload(IDirect3DDevice9*dev,const std::vector<DWORD>&src,int w,int h,IDirect3DTexture9**tex)
{
    HRESULT hr=dev->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,tex,0);if(FAILED(hr))return hr;
    D3DLOCKED_RECT lr={};hr=(*tex)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    for(int y=0;y<h;++y)memcpy((BYTE*)lr.pBits+y*lr.Pitch,&src[(size_t)y*w],(size_t)w*4);
    (*tex)->UnlockRect(0);return S_OK;
}

static HRESULT Render(IDirect3DDevice9*dev,IDirect3DPixelShader9*ps,IDirect3DTexture9*src,int sw,int sh,int dw,int dh,std::vector<DWORD>*out)
{
    IDirect3DTexture9*dst=0;IDirect3DSurface9*ds=0;IDirect3DSurface9*read=0;
    HRESULT hr=dev->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(hr))return hr;
    hr=dst->GetSurfaceLevel(0,&ds);if(FAILED(hr)){dst->Release();return hr;}
    hr=dev->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);if(FAILED(hr)){ds->Release();dst->Release();return hr;}
    dev->SetDepthStencilSurface(0);dev->SetRenderTarget(0,ds);D3DVIEWPORT9 vp={0,0,(DWORD)dw,(DWORD)dh,0,1};dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);dev->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);dev->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);dev->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh};dev->SetPixelShaderConstantF(0,c0,1);dev->SetPixelShader(ps);dev->SetVertexShader(0);dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);dev->SetTexture(0,src);
    Vtx q[4]={{-0.5f,-0.5f,0,1,0,0},{(float)dw-0.5f,-0.5f,0,1,1,0},{-0.5f,(float)dh-0.5f,0,1,0,1},{(float)dw-0.5f,(float)dh-0.5f,0,1,1,1}};
    hr=dev->BeginScene();if(SUCCEEDED(hr)){hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=dev->EndScene();if(SUCCEEDED(hr))hr=e;}dev->SetTexture(0,0);
    if(SUCCEEDED(hr))hr=dev->GetRenderTargetData(ds,read);
    if(SUCCEEDED(hr)){D3DLOCKED_RECT lr={};hr=read->LockRect(&lr,0,D3DLOCK_READONLY);if(SUCCEEDED(hr)){out->resize((size_t)dw*dh);for(int y=0;y<dh;++y)memcpy(&(*out)[(size_t)y*dw],(BYTE*)lr.pBits+y*lr.Pitch,(size_t)dw*4);read->UnlockRect();}}
    read->Release();ds->Release();dst->Release();return hr;
}

static Err Compare(const std::vector<DWORD>&got,const std::vector<DWORD>&ref)
{
    uint64_t sum=0,n=0;int maxe=0;unsigned over2=0;
    for(size_t i=0;i<got.size();++i)for(int sh=0;sh<=16;sh+=8){int a=(got[i]>>sh)&255,b=(ref[i]>>sh)&255,e=std::abs(a-b);sum+=(uint64_t)e;++n;maxe=std::max(maxe,e);if(e>2)++over2;}
    return {(double)sum/(double)n,maxe,over2};
}

int main()
{
    wchar_t path[MAX_PATH]={0};if(!GetSystemDirectoryW(path,MAX_PATH))return 2;wcscat_s(path,L"\\d3d9.dll");HMODULE module=LoadLibraryW(path);if(!module)return 3;
    PFN_Direct3DCreate9 create9=(PFN_Direct3DCreate9)GetProcAddress(module,"Direct3DCreate9");if(!create9)return 4;IDirect3D9*d3d=create9(D3D_SDK_VERSION);if(!d3d)return 5;HWND hwnd=MakeWindow();if(!hwnd)return 6;
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=hwnd;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9*dev=0;HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr))hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr)||!dev)return 7;
    IDirect3DPixelShader9*ps=0;hr=dev->CreatePixelShader((const DWORD*)g_ptarBicubic16R7Ps,&ps);if(FAILED(hr)||!ps)return 8;
    const int sw=48,sh=32,dw=72,dh=48;double sumMae=0.0;int maxErr=0;unsigned over2=0;
    for(int kind=0;kind<8;++kind)
    {
        std::vector<DWORD>src=Pattern(kind,sw,sh),got,ref((size_t)dw*dh);IDirect3DTexture9*tex=0;hr=Upload(dev,src,sw,sh,&tex);if(FAILED(hr))return 9;
        for(int y=0;y<dh;++y)for(int x=0;x<dw;++x)ref[(size_t)y*dw+x]=ToARGB(RefR7(src,sw,sh,dw,dh,x,y));
        hr=Render(dev,ps,tex,sw,sh,dw,dh,&got);tex->Release();if(FAILED(hr))return 10;
        Err e=Compare(got,ref);sumMae+=e.mae;maxErr=std::max(maxErr,e.maxe);over2+=e.over2;std::printf("R7_PATTERN_%d_MAE=%.6f MAX=%d OVER2=%u\n",kind,e.mae,e.maxe,e.over2);
    }
    const double mean=sumMae/8.0;std::printf("R7_MEAN_MAE=%.6f MAX=%d OVER2=%u\n",mean,maxErr,over2);const bool ok=mean<=0.12&&maxErr<=2&&over2==0;std::printf("BIC16_R7_PIXEL_PARITY=%s\n",ok?"PASS":"FAIL");
    ps->Release();dev->Release();DestroyWindow(hwnd);d3d->Release();FreeLibrary(module);return ok?0:1;
}
