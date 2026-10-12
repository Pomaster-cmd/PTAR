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
#include "../../build_x86/ptar_spec5_hq900_d3d9_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};
struct F4{float r,g,b,a;};

static float Sat(float v){return v<0?0:(v>1?1:v);}
static F4 Add(F4 a,F4 b){return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a};}
static F4 Sub(F4 a,F4 b){return {a.r-b.r,a.g-b.g,a.b-b.b,a.a-b.a};}
static F4 Mul(F4 a,float s){return {a.r*s,a.g*s,a.b*s,a.a*s};}
static F4 L4(F4 a,F4 b,float t){return Add(Mul(a,1-t),Mul(b,t));}
static F4 Min4(F4 a,F4 b){return {std::min(a.r,b.r),std::min(a.g,b.g),std::min(a.b,b.b),std::min(a.a,b.a)};}
static F4 Max4(F4 a,F4 b){return {std::max(a.r,b.r),std::max(a.g,b.g),std::max(a.b,b.b),std::max(a.a,b.a)};}
static F4 Clamp4(F4 v,F4 lo,F4 hi){return {std::max(lo.r,std::min(hi.r,v.r)),std::max(lo.g,std::min(hi.g,v.g)),std::max(lo.b,std::min(hi.b,v.b)),std::max(lo.a,std::min(hi.a,v.a))};}
static float Y(F4 p){return p.r*.299f+p.g*.587f+p.b*.114f;}
static int CI(int v,int n){return v<0?0:(v>=n?n-1:v);}
static F4 From(DWORD p){return {(float)((p>>16)&255)/255.f,(float)((p>>8)&255)/255.f,(float)(p&255)/255.f,(float)((p>>24)&255)/255.f};}
static BYTE Q(float v){int n=(int)std::floor(Sat(v)*255.f+.5f);return (BYTE)CI(n,256);}
static DWORD To(F4 p){return 0xff000000u|((DWORD)Q(p.r)<<16)|((DWORD)Q(p.g)<<8)|Q(p.b);}
static F4 Load(const std::vector<DWORD>&s,int w,int h,int x,int y){return From(s[(size_t)CI(y,h)*w+CI(x,w)]);}
static F4 Bil(const std::vector<DWORD>&s,int w,int h,float x,float y){int ix=(int)std::floor(x),iy=(int)std::floor(y);float fx=x-ix,fy=y-iy;return L4(L4(Load(s,w,h,ix,iy),Load(s,w,h,ix+1,iy),fx),L4(Load(s,w,h,ix,iy+1),Load(s,w,h,ix+1,iy+1),fx),fy);}

static F4 K13(F4 m,F4 a,F4 b,F4 c){return Add(Add(Mul(m,-.356296296296296f),Mul(a,.941111111111111f)),Add(Mul(b,.593333333333333f),Mul(c,-.178148148148148f)));}
static F4 K23(F4 m,F4 a,F4 b,F4 c){return Add(Add(Mul(m,-.178148148148148f),Mul(a,.593333333333333f)),Add(Mul(b,.941111111111111f),Mul(c,-.356296296296296f)));}

static F4 RefQ4(const std::vector<DWORD>&s,int sw,int sh,int ox,int oy)
{
    float sx=ox*(2.f/3.f),sy=oy*(2.f/3.f);int fx=(int)std::floor(sx),fy=(int)std::floor(sy);
    float tl=Load(s,sw,sh,fx,fy).g,tr=Load(s,sw,sh,fx+1,fy).g,bl=Load(s,sw,sh,fx,fy+1).g,br=Load(s,sw,sh,fx+1,fy+1).g;
    float gx=(tr+br)-(tl+bl),gy=(bl+br)-(tl+tr),agx=std::fabs(gx),agy=std::fabs(gy);bool ux=agx>=agy;
    float bx=ux?(float)fx:sx,by=ux?sy:(float)fy;F4 f0=Bil(s,sw,sh,bx,by);int phase=(ux?ox:oy)%3;if(phase==0)return f0;
    float da=tl-br,db=tr-bl;bool chooseB=std::fabs(db)>=std::fabs(da);float dx=chooseB?-1.f:-1.f,dy=chooseB?-1.f:1.f;
    F4 dm=Bil(s,sw,sh,bx+dx,by+dy),dp=Bil(s,sw,sh,bx-dx,by-dy);F4 residual=Sub(f0,Mul(Add(dm,dp),.5f));
    float ax=ux?1.f:0.f,ay=ux?0.f:1.f;F4 fm=Bil(s,sw,sh,bx-ax,by-ay),f1=Bil(s,sw,sh,bx+ax,by+ay),f2=Bil(s,sw,sh,bx+2*ax,by+2*ay);
    F4 edge=phase==2?K13(fm,f0,f1,f2):K23(fm,f0,f1,f2),raster=Clamp4(edge,Min4(f0,f1),Max4(f0,f1));float frac=phase==1?2.f/3.f:1.f/3.f;F4 natural=L4(L4(f0,f1,frac),raster,.9f);
    float range=std::max(std::max(tl,tr),std::max(bl,br))-std::min(std::min(tl,tr),std::min(bl,br));float card=std::max(agx,agy),grad=.5f*card,diag=.5f*std::fabs((tr+bl)-(tl+br));float coh=std::fabs(agx-agy)/(agx+agy+1e-6f);
    float rc=Sat((range-.01f)*10.f),cc=Sat((coh-.25f)*2.2222222222f),ac=1.f-Sat((diag/(grad+1e-6f)-.08f)*2.2222222222f);float rw=rc*cc*ac,eb=Sat((grad-.02f)*10.f),ew=(1-rw)*eb,nw=(1-rw)*(1-eb);
    float dg=Sat((std::fabs(std::fabs(db)-std::fabs(da))/(card+1e-6f)-.10f)*2.5f);natural=Add(natural,Mul(residual,(1-eb)*dg*nw*.125f));return Add(Add(Mul(natural,nw),Mul(edge,ew)),Mul(raster,rw));
}

static F4 RefU(const std::vector<DWORD>&s,int sw,int sh,float x,float y,float sharp)
{
    int qx=(int)std::floor(x),qy=(int)std::floor(y);float fx=x-qx,fy=y-qy;F4 c=Load(s,sw,sh,qx,qy),d=Load(s,sw,sh,qx+1,qy),e=Load(s,sw,sh,qx,qy+1),h=Load(s,sw,sh,qx+1,qy+1);F4 b=L4(L4(c,d,fx),L4(e,h,fx),fy);
    float E=Sat((std::fabs(Y(d)-Y(c))+std::fabs(Y(h)-Y(e))+std::fabs(Y(e)-Y(c))+std::fabs(Y(h)-Y(d)))*1.6f);F4 lo=Min4(Min4(c,d),Min4(e,h)),hi=Max4(Max4(c,d),Max4(e,h));float sx=fx*fx*(3-2*fx),sy=fy*fy*(3-2*fy);F4 r=L4(L4(c,d,sx),L4(e,h,sx),sy);float g=Sat((sharp-1.f)*(.35f+.75f*E))*1.15f;return Clamp4(Add(b,Mul(Sub(r,b),g)),lo,hi);
}

static F4 RefAdaptive(const std::vector<DWORD>&s,int sw,int sh,int dw,int dh,int ox,int oy,float sharp)
{
    float px=((ox+.5f)*sw/(float)dw)-.5f,py=((oy+.5f)*sh/(float)dh)-.5f;
    if(sharp>1){float dx=2.f*sw/dw,dy=2.f*sh/dh;F4 c=RefU(s,sw,sh,px,py,sharp),R=RefU(s,sw,sh,px+dx,py,sharp),L=RefU(s,sw,sh,px-dx,py,sharp),B=RefU(s,sw,sh,px,py+dy,sharp),T=RefU(s,sw,sh,px,py-dy,sharp);F4 n=Mul(Add(Add(R,L),Add(B,T)),.25f);float x=std::fabs(Y(R)-Y(L)),y=std::fabs(Y(B)-Y(T)),hp=std::fabs(Y(c)-Y(n));float t=Sat((hp-.0008f)*100.f)*Sat(1.f-std::max(x,y)/.15f)*(1.f-std::fabs(x-y)/(x+y+.01f));F4 o=Add(c,Mul(Sub(c,n),.4f+.25f*t));o.r=Sat(o.r);o.g=Sat(o.g);o.b=Sat(o.b);return o;}
    int qx=(int)std::floor(px),qy=(int)std::floor(py);float fx=px-qx,fy=py-qy;F4 c=Load(s,sw,sh,qx,qy),d=Load(s,sw,sh,qx+1,qy),e=Load(s,sw,sh,qx,qy+1),h=Load(s,sw,sh,qx+1,qy+1);F4 b=L4(L4(c,d,fx),L4(e,h,fx),fy);float E=Sat((std::fabs(Y(d)-Y(c))+std::fabs(Y(h)-Y(e))+std::fabs(Y(e)-Y(c))+std::fabs(Y(h)-Y(d)))*1.6f);F4 lo=Min4(Min4(c,d),Min4(e,h)),hi=Max4(Max4(c,d),Max4(e,h));F4 N=Load(s,sw,sh,(int)std::floor(px+.5f),(int)std::floor(py+.5f));return Clamp4(Add(b,Mul(Sub(N,b),sharp*(.12f+.38f*E))),lo,hi);
}

static std::vector<DWORD> Pattern(int k,int w,int h){std::vector<DWORD> p((size_t)w*h);for(int y=0;y<h;y++)for(int x=0;x<w;x++){int r=0,g=0,b=0;switch(k){case 0:r=(x*255)/(w-1);g=(y*255)/(h-1);b=(((x/4)^(y/4))&1)?230:24;break;case 1:r=g=b=x<w/2?20:235;break;case 2:r=g=b=y<h/2?20:235;break;case 3:r=g=b=x>y+(w-h)/2?238:18;break;case 4:r=g=b=((x%7)==0||(y%9)==0)?245:16;break;case 5:r=(x<w/2)?245:15;g=((x+y)&7)<4?225:30;b=(x<w/2)?25:240;break;default:r=(x*37+y*17)&255;g=(x*13+y*53)&255;b=(x*71+y*7)&255;break;}p[(size_t)y*w+x]=0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|b;}return p;}

static HWND Win(){HINSTANCE i=GetModuleHandleW(0);WNDCLASSW c={};c.lpfnWndProc=DefWindowProcW;c.hInstance=i;c.lpszClassName=L"PTAR_FQ_ORACLE";RegisterClassW(&c);return CreateWindowExW(0,c.lpszClassName,L"x",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);}
static HRESULT Upload(IDirect3DDevice9*d,const std::vector<DWORD>&s,int w,int h,IDirect3DTexture9**t){HRESULT r=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(r))return r;D3DLOCKED_RECT l={};r=(*t)->LockRect(0,&l,0,0);if(FAILED(r))return r;for(int y=0;y<h;y++)memcpy((BYTE*)l.pBits+y*l.Pitch,&s[(size_t)y*w],(size_t)w*4);(*t)->UnlockRect(0);return S_OK;}
static HRESULT Render(IDirect3DDevice9*d,IDirect3DPixelShader9*ps,IDirect3DTexture9*src,int sw,int sh,int dw,int dh,float sharp,std::vector<DWORD>*o){IDirect3DTexture9*dst=0;IDirect3DSurface9*ds=0,*rd=0;HRESULT r=d->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(r))return r;dst->GetSurfaceLevel(0,&ds);d->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&rd,0);d->SetDepthStencilSurface(0);d->SetRenderTarget(0,ds);D3DVIEWPORT9 vp={0,0,(DWORD)dw,(DWORD)dh,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh},c1[4]={sharp,0,0,0};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShaderConstantF(1,c1,1);d->SetPixelShader(ps);d->SetVertexShader(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,src);Vtx q[4]={{-.5f,-.5f,0,1,0,0},{(float)dw-.5f,-.5f,0,1,1,0},{-.5f,(float)dh-.5f,0,1,0,1},{(float)dw-.5f,(float)dh-.5f,0,1,1,1}};r=d->BeginScene();if(SUCCEEDED(r)){r=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=d->EndScene();if(SUCCEEDED(r))r=e;}d->SetTexture(0,0);if(SUCCEEDED(r))r=d->GetRenderTargetData(ds,rd);if(SUCCEEDED(r)){D3DLOCKED_RECT l={};r=rd->LockRect(&l,0,D3DLOCK_READONLY);if(SUCCEEDED(r)){o->resize((size_t)dw*dh);for(int y=0;y<dh;y++)memcpy(&(*o)[(size_t)y*dw],(BYTE*)l.pBits+y*l.Pitch,(size_t)dw*4);rd->UnlockRect();}}rd->Release();ds->Release();dst->Release();return r;}
struct Err{double mae;int maxe;unsigned over2;};static Err Cmp(const std::vector<DWORD>&a,const std::vector<DWORD>&b){uint64_t sum=0,n=0;int mx=0;unsigned o=0;for(size_t i=0;i<a.size();i++)for(int s=0;s<=16;s+=8){int e=std::abs((int)((a[i]>>s)&255)-(int)((b[i]>>s)&255));sum+=e;n++;mx=std::max(mx,e);if(e>2)o++;}return{(double)sum/n,mx,o};}

int main(){wchar_t p[MAX_PATH]={};GetSystemDirectoryW(p,MAX_PATH);wcscat_s(p,L"\\d3d9.dll");HMODULE m=LoadLibraryW(p);if(!m)return 2;PFN_Direct3DCreate9 fn=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");IDirect3D9*d=fn?fn(D3D_SDK_VERSION):0;if(!d)return 3;HWND w=Win();D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*dev=0;HRESULT r=d->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(r))r=d->CreateDevice(0,D3DDEVTYPE_REF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(r))return 4;IDirect3DPixelShader9*q4=0,*ad=0;if(FAILED(dev->CreatePixelShader((DWORD*)g_ptarQstage4Diag2Ps,&q4)))return 5;if(FAILED(dev->CreatePixelShader((DWORD*)g_ptarSpec5Hq900Ps,&ad)))return 6;
    double qsum=0,ssum=0,hsum=0;int qmax=0,smax=0,hmax=0;for(int k=0;k<7;k++){std::vector<DWORD>s=Pattern(k,48,32),got,ref((size_t)72*48);IDirect3DTexture9*t=0;Upload(dev,s,48,32,&t);for(int y=0;y<48;y++)for(int x=0;x<72;x++)ref[(size_t)y*72+x]=To(RefQ4(s,48,32,x,y));if(FAILED(Render(dev,q4,t,48,32,72,48,0,&got)))return 7;Err e=Cmp(got,ref);qsum+=e.mae;qmax=std::max(qmax,e.maxe);printf("Q4_PATTERN_%d_MAE=%.6f MAX=%d OVER2=%u\n",k,e.mae,e.maxe,e.over2);for(int y=0;y<48;y++)for(int x=0;x<72;x++)ref[(size_t)y*72+x]=To(RefAdaptive(s,48,32,72,48,x,y,2.88f));if(FAILED(Render(dev,ad,t,48,32,72,48,2.88f,&got)))return 8;e=Cmp(got,ref);ssum+=e.mae;smax=std::max(smax,e.maxe);printf("SPEC5_PATTERN_%d_MAE=%.6f MAX=%d OVER2=%u\n",k,e.mae,e.maxe,e.over2);t->Release();}
    for(int k=0;k<7;k++){std::vector<DWORD>s=Pattern(k,64,36),got,ref((size_t)80*45);IDirect3DTexture9*t=0;Upload(dev,s,64,36,&t);for(int y=0;y<45;y++)for(int x=0;x<80;x++)ref[(size_t)y*80+x]=To(RefAdaptive(s,64,36,80,45,x,y,.96f));if(FAILED(Render(dev,ad,t,64,36,80,45,.96f,&got)))return 9;Err e=Cmp(got,ref);hsum+=e.mae;hmax=std::max(hmax,e.maxe);printf("HQ900_PATTERN_%d_MAE=%.6f MAX=%d OVER2=%u\n",k,e.mae,e.maxe,e.over2);t->Release();}
    qsum/=7;ssum/=7;hsum/=7;printf("Q4_MEAN_MAE=%.6f MAX=%d\n",qsum,qmax);printf("SPEC5_MEAN_MAE=%.6f MAX=%d\n",ssum,smax);printf("HQ900_MEAN_MAE=%.6f MAX=%d\n",hsum,hmax);bool ok=qmax<=2&&smax<=2&&hmax<=2&&qsum<=.10&&ssum<=.10&&hsum<=.10;printf("FULLQUALITY_PIXEL_PARITY=%s\n",ok?"PASS":"FAIL");ad->Release();q4->Release();dev->Release();DestroyWindow(w);d->Release();FreeLibrary(m);return ok?0:1;}
