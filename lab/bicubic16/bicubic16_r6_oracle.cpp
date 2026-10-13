#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../../build_x86/ptar_bicubic16_r6_oracle_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct V{float x,y,z,rhw,u,v;};
struct F{float r,g,b,a;};

static float sat(float x){return x<0?0:(x>1?1:x);}
static int ci(int x,int n){return x<0?0:(x>=n?n-1:x);}
static F add(F a,F b){return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a};}
static F sub(F a,F b){return {a.r-b.r,a.g-b.g,a.b-b.b,a.a-b.a};}
static F mul(F a,float s){return {a.r*s,a.g*s,a.b*s,a.a*s};}
static F lerp4(F a,F b,float t){return add(mul(a,1-t),mul(b,t));}
static F min4(F a,F b){return {std::min(a.r,b.r),std::min(a.g,b.g),std::min(a.b,b.b),std::min(a.a,b.a)};}
static F max4(F a,F b){return {std::max(a.r,b.r),std::max(a.g,b.g),std::max(a.b,b.b),std::max(a.a,b.a)};}
static F clamp4(F v,F lo,F hi){return {std::max(lo.r,std::min(v.r,hi.r)),std::max(lo.g,std::min(v.g,hi.g)),std::max(lo.b,std::min(v.b,hi.b)),std::max(lo.a,std::min(v.a,hi.a))};}
static float luma(F c){return c.r*0.2126f+c.g*0.7152f+c.b*0.0722f;}
static F fromargb(DWORD p){return {(float)((p>>16)&255)/255.0f,(float)((p>>8)&255)/255.0f,(float)(p&255)/255.0f,(float)((p>>24)&255)/255.0f};}
static BYTE q8(float v){int x=(int)std::floor(sat(v)*255.0f+0.5f);return (BYTE)ci(x,256);}
static DWORD toargb(F p){return ((DWORD)q8(p.a)<<24)|((DWORD)q8(p.r)<<16)|((DWORD)q8(p.g)<<8)|(DWORD)q8(p.b);}
static F load(const std::vector<DWORD>&s,int w,int h,int x,int y){return fromargb(s[(size_t)ci(y,h)*(size_t)w+(size_t)ci(x,w)]);}

static void cw(float f,float w[4])
{
    float f2=f*f,f3=f2*f;
    w[0]=-0.5f*f+f2-0.5f*f3;
    w[1]=1-2.5f*f2+1.5f*f3;
    w[2]=0.5f*f+2*f2-1.5f*f3;
    w[3]=-0.5f*f2+0.5f*f3;
}

static F refpx(const std::vector<DWORD>&s,int sw,int sh,int dw,int dh,int ox,int oy)
{
    float px=((ox+0.5f)*sw/(float)dw)-0.5f;
    float py=((oy+0.5f)*sh/(float)dh)-0.5f;
    int qx=(int)std::floor(px),qy=(int)std::floor(py);
    float fx=px-qx,fy=py-qy,wx[4],wy[4];
    cw(fx,wx);cw(fy,wy);

    F row[4]={{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    for(int j=0;j<4;++j)
        for(int i=0;i<4;++i)
            row[j]=add(row[j],mul(load(s,sw,sh,qx+i-1,qy+j-1),wx[i]));
    F cubic={0,0,0,0};
    for(int j=0;j<4;++j)cubic=add(cubic,mul(row[j],wy[j]));

    F s01=load(s,sw,sh,qx-1,qy);
    F s11=load(s,sw,sh,qx,qy);
    F s21=load(s,sw,sh,qx+1,qy);
    F s31=load(s,sw,sh,qx+2,qy);
    F s10=load(s,sw,sh,qx,qy-1);
    F s20=load(s,sw,sh,qx+1,qy-1);
    F s02=load(s,sw,sh,qx-1,qy+1);
    F s12=load(s,sw,sh,qx,qy+1);
    F s22=load(s,sw,sh,qx+1,qy+1);
    F s32=load(s,sw,sh,qx+2,qy+1);
    F s13=load(s,sw,sh,qx,qy+2);
    F s23=load(s,sw,sh,qx+1,qy+2);

    F bil=lerp4(lerp4(s11,s21,fx),lerp4(s12,s22,fx),fy);

    float l11=luma(s11),l21=luma(s21),l12=luma(s12),l22=luma(s22);
    float lm=std::min(std::min(l11,l21),std::min(l12,l22));
    float lx=std::max(std::max(l11,l21),std::max(l12,l22));
    float edge=sat((lx-lm-0.03f)*6.6666667f);

    float gx=0.5f*((l21-l11)+(l22-l12));
    float gy=0.5f*((l12-l11)+(l22-l21));
    float ax=std::fabs(gx),ay=std::fabs(gy);
    float gsum=ax+ay+0.00001f;
    float strong=sat((gsum-0.02f)*6.25f);
    float attenX=1.0f-0.45f*strong*(ax/gsum);
    float attenY=1.0f-0.45f*strong*(ay/gsum);

    F dx11=sub(s11,mul(add(s01,s21),0.5f));
    F dx21=sub(s21,mul(add(s11,s31),0.5f));
    F dx12=sub(s12,mul(add(s02,s22),0.5f));
    F dx22=sub(s22,mul(add(s12,s32),0.5f));
    F dy11=sub(s11,mul(add(s10,s12),0.5f));
    F dy21=sub(s21,mul(add(s20,s22),0.5f));
    F dy12=sub(s12,mul(add(s11,s13),0.5f));
    F dy22=sub(s22,mul(add(s21,s23),0.5f));

    F detailX=lerp4(lerp4(dx11,dx21,fx),lerp4(dx12,dx22,fx),fy);
    F detailY=lerp4(lerp4(dy11,dy21,fx),lerp4(dy12,dy22,fx),fy);
    F sourceDetail=mul(add(mul(detailX,attenX),mul(detailY,attenY)),0.5f);

    float phaseX=4.0f*fx*(1.0f-fx);
    float phaseY=4.0f*fy*(1.0f-fy);
    float phaseGain=1.0f+0.20f*0.5f*(phaseX+phaseY);
    float residualGain=0.70f+(0.60f-0.70f)*edge;
    float detailGain=1.00f+(0.60f-1.00f)*edge;
    float margin=0.18f+(0.08f-0.18f)*edge;

    F raw=add(cubic,mul(sub(cubic,bil),residualGain));
    raw=add(raw,mul(sourceDetail,detailGain*phaseGain));

    F lo=min4(min4(s11,s21),min4(s12,s22));
    F hi=max4(max4(s11,s21),max4(s12,s22));
    F range=sub(hi,lo);
    F out=clamp4(raw,sub(lo,mul(range,margin)),add(hi,mul(range,margin)));
    out.r=sat(out.r);out.g=sat(out.g);out.b=sat(out.b);out.a=sat(out.a);
    return out;
}

static HWND wnd()
{
    HINSTANCE i=GetModuleHandleW(0);
    WNDCLASSW w={};w.lpfnWndProc=DefWindowProcW;w.hInstance=i;w.lpszClassName=L"R6O";
    RegisterClassW(&w);
    return CreateWindowExW(0,w.lpszClassName,L"R6",WS_OVERLAPPEDWINDOW,0,0,160,120,0,0,i,0);
}

int main()
{
    const int sw=24,sh=16,dw=36,dh=24;
    std::vector<DWORD> src((size_t)sw*sh);
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x)
    {
        int r=(x*37+y*17)&255;
        int g=(x*13+y*53)&255;
        int b=((x%5)==0||(y%7)==0)?245:((x*71+y*7)&255);
        src[(size_t)y*sw+x]=0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;
    }

    wchar_t p[MAX_PATH]={0};GetSystemDirectoryW(p,MAX_PATH);wcscat_s(p,L"\\d3d9.dll");
    HMODULE m=LoadLibraryW(p);if(!m)return 2;
    PFN_Direct3DCreate9 f=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");if(!f)return 3;
    IDirect3D9*d=f(D3D_SDK_VERSION);if(!d)return 4;
    HWND h=wnd();

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=160;pp.BackBufferHeight=120;pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=h;
    pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9*dev=0;
    HRESULT hr=d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr))hr=d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,h,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)||!dev)return 5;

    IDirect3DPixelShader9*ps=0;
    if(FAILED(dev->CreatePixelShader((const DWORD*)g_ptarBicubic16R6Ps,&ps))||!ps)return 6;

    IDirect3DTexture9*st=0;
    dev->CreateTexture(sw,sh,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&st,0);
    D3DLOCKED_RECT lr={};st->LockRect(0,&lr,0,0);
    for(int y=0;y<sh;++y)memcpy((BYTE*)lr.pBits+y*lr.Pitch,&src[(size_t)y*sw],sw*4);
    st->UnlockRect(0);

    IDirect3DTexture9*dt=0;IDirect3DSurface9*ds=0,*rd=0;
    dev->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dt,0);
    dt->GetSurfaceLevel(0,&ds);
    dev->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&rd,0);
    dev->SetDepthStencilSurface(0);dev->SetRenderTarget(0,ds);
    D3DVIEWPORT9 vp={0,0,dw,dh,0,1};dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE,FALSE);dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);dev->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    dev->SetTexture(0,st);
    float c[4]={(float)sw,(float)sh,(float)dw,(float)dh};
    dev->SetPixelShaderConstantF(0,c,1);dev->SetPixelShader(ps);dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
    V q[4]={{-.5f,-.5f,0,1,0,0},{dw-.5f,-.5f,0,1,1,0},{-.5f,dh-.5f,0,1,0,1},{dw-.5f,dh-.5f,0,1,1,1}};
    dev->BeginScene();dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));dev->EndScene();
    dev->SetTexture(0,0);dev->GetRenderTargetData(ds,rd);

    std::vector<DWORD> got((size_t)dw*dh);
    rd->LockRect(&lr,0,D3DLOCK_READONLY);
    for(int y=0;y<dh;++y)memcpy(&got[(size_t)y*dw],(BYTE*)lr.pBits+y*lr.Pitch,dw*4);
    rd->UnlockRect();

    uint64_t sum=0,n=0;int mx=0,over2=0;
    for(int y=0;y<dh;++y)for(int x=0;x<dw;++x)
    {
        DWORD a=got[(size_t)y*dw+x],b=toargb(refpx(src,sw,sh,dw,dh,x,y));
        for(int sft=0;sft<=16;sft+=8)
        {
            int e=std::abs((int)((a>>sft)&255)-(int)((b>>sft)&255));
            sum+=e;++n;mx=std::max(mx,e);if(e>2)++over2;
        }
    }
    double mae=(double)sum/(double)n;
    bool ok=mae<=0.14&&mx<=2&&over2==0;
    std::printf("R6_MAE=%.6f MAX=%d OVER2=%d\n",mae,mx,over2);
    std::printf("BIC16_R6_PIXEL_PARITY=%s\n",ok?"PASS":"FAIL");

    rd->Release();ds->Release();dt->Release();st->Release();ps->Release();
    dev->Release();DestroyWindow(h);d->Release();FreeLibrary(m);
    return ok?0:1;
}
