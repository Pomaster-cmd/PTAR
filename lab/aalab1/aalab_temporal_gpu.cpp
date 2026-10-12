#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "../../build_x86/ptar_spec5_aalab_v0.h"
#include "../../build_x86/ptar_spec5_aalab_v1.h"
#include "../../build_x86/ptar_spec5_aalab_v2.h"
#include "../../build_x86/ptar_spec5_aalab_v3.h"
#include "../../build_x86/ptar_spec5_aalab_v4.h"
#include "../../build_x86/ptar_spec5_aalab_v5.h"
#include "../../build_x86/ptar_spec5_aalab_v6.h"
#include "../../build_x86/ptar_spec5_aalab_v7.h"
#include "../../build_x86/ptar_spec5_aalab_v8.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};
struct C3{float r,g,b;};
struct TMetric{double deltaErr,excess,deficit,energyRatio;};
static float Sat(float v){return v<0?0:(v>1?1:v);}
static C3 Mix(C3 a,C3 b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
static DWORD Pack(C3 c){BYTE r=(BYTE)(Sat(c.r)*255+.5f),g=(BYTE)(Sat(c.g)*255+.5f),b=(BYTE)(Sat(c.b)*255+.5f);return 0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|b;}
static float Y(DWORD p){float r=((p>>16)&255)/255.0f,g=((p>>8)&255)/255.0f,b=(p&255)/255.0f;return .299f*r+.587f*g+.114f*b;}
static float Seg(float px,float py,float ax,float ay,float bx,float by){float vx=bx-ax,vy=by-ay,wx=px-ax,wy=py-ay,d=vx*vx+vy*vy,t=d>1e-8f?(wx*vx+wy*vy)/d:0;t=std::max(0.f,std::min(1.f,t));float dx=px-(ax+t*vx),dy=py-(ay+t*vy);return std::sqrt(dx*dx+dy*dy);}

static C3 Eval(int kind,int x,int y,int w,int h,float shift)
{
    float px=(x+.5f-shift)/(float)w,py=(y+.5f-.35f*shift)/(float)h;
    if(kind==0){C3 c={.14f,.18f,.22f};const float s[9]={.12f,.21f,.33f,.47f,.62f,-.17f,-.29f,-.44f,-.71f};for(int i=0;i<9;i++){float yy=.06f+.105f*i+s[i]*(px-.5f);float d=std::fabs(py-yy)*h/std::sqrt(1+s[i]*s[i]);float a=Sat(1.65f-d);c=Mix(c,(i&1)?C3{.92f,.87f,.73f}:C3{.035f,.045f,.055f},a);}return c;}
    if(kind==1){C3 c={.62f,.48f,.38f};for(int i=0;i<30;i++){float yy=.045f+.030f*i+.052f*std::sin(px*(8.0f+.11f*i)+i*.61f);float d=std::fabs(py-yy)*h;float a=Sat(((i%6)==0?1.7f:1.05f)-d);c=Mix(c,{.045f,.03f,.02f},a);}return c;}
    if(kind==2){C3 c={.13f,.28f,.12f};for(int i=0;i<16;i++){float ax=.02f+.06f*i,ay=.95f,bx=.08f+.058f*i,by=.10f+.045f*std::sin(i*.7f);float d=Seg(px,py,ax,ay,bx,by)*h;c=Mix(c,{.10f,.055f,.025f},Sat(1.35f-d));}float f=.5f+.5f*std::sin((px*71.0f+py*53.0f)*3.14159f);if(f>.77f)c=Mix(c,{.40f,.66f,.23f},.62f);return c;}
    float gx=std::fmod((px*26.0f)+100.0f,1.0f),gy=std::fmod((py*15.0f)+100.0f,1.0f);float line=(gx<.10f||gy<.10f)?1.0f:0.0f;float diag=std::fabs(std::fmod(px*9.0f-py*7.0f+100.0f,1.0f)-.5f)<.055f?1.0f:0.0f;return Mix({.08f,.10f,.13f},{.91f,.88f,.76f},std::max(line,diag));
}

static std::vector<DWORD> Super(int kind,int w,int h,float shift){std::vector<DWORD>a((size_t)w*h);for(int y=0;y<h;y++)for(int x=0;x<w;x++)a[(size_t)y*w+x]=Pack(Eval(kind,x,y,w,h,shift));return a;}
static std::vector<DWORD> Down(const std::vector<DWORD>&s,int sw,int sh,int f){int dw=sw/f,dh=sh/f;std::vector<DWORD>o((size_t)dw*dh);for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){double R=0,G=0,B=0;for(int j=0;j<f;j++)for(int i=0;i<f;i++){DWORD p=s[(size_t)(y*f+j)*sw+x*f+i];R+=(p>>16)&255;G+=(p>>8)&255;B+=p&255;}double n=f*f;o[(size_t)y*dw+x]=0xff000000u|((DWORD)(R/n+.5)<<16)|((DWORD)(G/n+.5)<<8)|(DWORD)(B/n+.5);}return o;}

static TMetric Temporal(const std::vector<DWORD>&g0,const std::vector<DWORD>&g1,const std::vector<DWORD>&r0,const std::vector<DWORD>&r1){double err=0,ex=0,de=0,ge=0,re=0;size_t n=g0.size();for(size_t i=0;i<n;i++){double gd=Y(g1[i])-Y(g0[i]),rd=Y(r1[i])-Y(r0[i]);double ag=std::fabs(gd),ar=std::fabs(rd);err+=std::fabs(gd-rd);ex+=std::max(0.0,ag-ar-.003);de+=std::max(0.0,ar-ag-.003);ge+=ag;re+=ar;}return {err/n,ex/n,de/n,re>0?ge/re:1.0};}

static HWND Win(){HINSTANCE i=GetModuleHandleW(0);WNDCLASSW c={};c.lpfnWndProc=DefWindowProcW;c.hInstance=i;c.lpszClassName=L"PTAR_AALAB_TEMP";RegisterClassW(&c);return CreateWindowExW(0,c.lpszClassName,L"PTAR",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);}
static HRESULT Upload(IDirect3DDevice9*d,const std::vector<DWORD>&s,int w,int h,IDirect3DTexture9**t){HRESULT r=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(r))return r;D3DLOCKED_RECT l={};r=(*t)->LockRect(0,&l,0,0);if(FAILED(r))return r;for(int y=0;y<h;y++)memcpy((BYTE*)l.pBits+y*l.Pitch,&s[(size_t)y*w],(size_t)w*4);(*t)->UnlockRect(0);return S_OK;}
static HRESULT Render(IDirect3DDevice9*d,IDirect3DPixelShader9*ps,IDirect3DTexture9*src,int sw,int sh,int dw,int dh,std::vector<DWORD>*o){IDirect3DTexture9*dst=0;IDirect3DSurface9*ds=0,*rd=0;HRESULT r=d->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(r))return r;dst->GetSurfaceLevel(0,&ds);d->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&rd,0);d->SetDepthStencilSurface(0);d->SetRenderTarget(0,ds);D3DVIEWPORT9 v={0,0,(DWORD)dw,(DWORD)dh,0,1};d->SetViewport(&v);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShader(ps);d->SetVertexShader(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,src);Vtx q[4]={{-.5f,-.5f,0,1,0,0},{(float)dw-.5f,-.5f,0,1,1,0},{-.5f,(float)dh-.5f,0,1,0,1},{(float)dw-.5f,(float)dh-.5f,0,1,1,1}};r=d->BeginScene();if(SUCCEEDED(r)){r=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=d->EndScene();if(SUCCEEDED(r))r=e;}d->SetTexture(0,0);if(SUCCEEDED(r))r=d->GetRenderTargetData(ds,rd);if(SUCCEEDED(r)){D3DLOCKED_RECT l={};r=rd->LockRect(&l,0,D3DLOCK_READONLY);if(SUCCEEDED(r)){o->resize((size_t)dw*dh);for(int y=0;y<dh;y++)memcpy(&(*o)[(size_t)y*dw],(BYTE*)l.pBits+y*l.Pitch,(size_t)dw*4);rd->UnlockRect();}}rd->Release();ds->Release();dst->Release();return r;}

int main(){const char*names[9]={"BASE","B08","B12","BADAPT20","AA0_15","AA2_15","AA2B","AA4B","AA2B_STRONG"};const DWORD*code[9]={(DWORD*)g_ptarSpec5AaLabV0,(DWORD*)g_ptarSpec5AaLabV1,(DWORD*)g_ptarSpec5AaLabV2,(DWORD*)g_ptarSpec5AaLabV3,(DWORD*)g_ptarSpec5AaLabV4,(DWORD*)g_ptarSpec5AaLabV5,(DWORD*)g_ptarSpec5AaLabV6,(DWORD*)g_ptarSpec5AaLabV7,(DWORD*)g_ptarSpec5AaLabV8};wchar_t path[MAX_PATH]={};GetSystemDirectoryW(path,MAX_PATH);wcscat_s(path,L"\\d3d9.dll");HMODULE m=LoadLibraryW(path);if(!m)return 2;PFN_Direct3DCreate9 fn=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");IDirect3D9*d=fn?fn(D3D_SDK_VERSION):0;if(!d)return 3;HWND w=Win();D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*dev=0;HRESULT hr=d->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr))hr=d->CreateDevice(0,D3DDEVTYPE_REF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr)||!dev)return 4;IDirect3DPixelShader9*ps[9]={};for(int i=0;i<9;i++){if(FAILED(dev->CreatePixelShader(code[i],&ps[i])))return 5;}const int SW=384,SH=216,HW=192,HH=108,LW=128,LH=72,FRAMES=13;double acc[9][4]={};int pairs=0;FILE*csv=0;fopen_s(&csv,"AALAB_TEMPORAL.csv","wb");fprintf(csv,"variant,pattern,pair,delta_err,flicker_excess,motion_deficit,energy_ratio\n");for(int kind=0;kind<4;kind++){std::vector<DWORD>prevRef;std::vector<DWORD>prevGot[9];for(int f=0;f<FRAMES;f++){float shift=f*.50f;std::vector<DWORD>sup=Super(kind,SW,SH,shift),ref=Down(sup,SW,SH,2),low=Down(sup,SW,SH,3);IDirect3DTexture9*t=0;if(FAILED(Upload(dev,low,LW,LH,&t)))return 6;std::vector<DWORD>got[9];for(int v=0;v<9;v++){if(FAILED(Render(dev,ps[v],t,LW,LH,HW,HH,&got[v])))return 7;if(f>0){TMetric q=Temporal(prevGot[v],got[v],prevRef,ref);fprintf(csv,"%s,%d,%d,%.9f,%.9f,%.9f,%.9f\n",names[v],kind,f-1,q.deltaErr,q.excess,q.deficit,q.energyRatio);acc[v][0]+=q.deltaErr;acc[v][1]+=q.excess;acc[v][2]+=q.deficit;acc[v][3]+=q.energyRatio;}}t->Release();prevRef.swap(ref);for(int v=0;v<9;v++)prevGot[v].swap(got[v]);}pairs+=FRAMES-1;}fclose(csv);for(int v=0;v<9;v++){for(int j=0;j<4;j++)acc[v][j]/=pairs;printf("AALAB_TEMP_%s DELTAERR=%.9f EXCESS=%.9f DEFICIT=%.9f ENERGY=%.9f\n",names[v],acc[v][0],acc[v][1],acc[v][2],acc[v][3]);}printf("AALAB_TEMPORAL_GATE=PASS\n");for(int i=0;i<9;i++)ps[i]->Release();dev->Release();DestroyWindow(w);d->Release();FreeLibrary(m);return 0;}
