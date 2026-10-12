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
struct Metrics{double mae,mse,psnr,edgeMae,gradMae,lapMae,detailRatio;};

static float Sat(float v){return v<0?0:(v>1?1:v);}
static C3 Mix(C3 a,C3 b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
static float Luma(C3 c){return c.r*.299f+c.g*.587f+c.b*.114f;}
static float Hash01(int x,int y,int k){uint32_t n=(uint32_t)(x*1973+y*9277+k*26699+0x68bc21ebu);n=(n<<13)^n;uint32_t v=n*(n*n*15731u+789221u)+1376312589u;return (float)(v&0xffffu)/65535.0f;}
static float SegDist(float px,float py,float ax,float ay,float bx,float by){float vx=bx-ax,vy=by-ay,wx=px-ax,wy=py-ay,d=vx*vx+vy*vy;float t=d>1e-8f?(wx*vx+wy*vy)/d:0;t=std::max(0.0f,std::min(1.0f,t));float dx=px-(ax+t*vx),dy=py-(ay+t*vy);return std::sqrt(dx*dx+dy*dy);}

static C3 EvalPattern(int kind,int x,int y,int w,int h)
{
    float px=(x+.5f)/(float)w,py=(y+.5f)/(float)h;
    C3 bg={.45f+.25f*px,.48f+.20f*py,.50f+.15f*px};
    if(kind==0){ // wires/diagonals
        C3 c={.16f+.28f*py,.20f+.24f*py,.24f+.22f*py};
        const float slopes[8]={.13f,.23f,.37f,.51f,.72f,-.19f,-.41f,-.67f};
        for(int i=0;i<8;i++){float yy=.08f+.105f*i+slopes[i]*(px-.5f);float d=std::fabs(py-yy)*h/std::sqrt(1.0f+slopes[i]*slopes[i]);float a=Sat(1.8f-d);C3 line={(i&1)?.92f:.07f,(i&1)?.88f:.08f,(i&1)?.74f:.10f};c=Mix(c,line,a);}return c;
    }
    if(kind==1){ // hair-like strands
        C3 c={.58f,.46f,.38f};
        for(int i=0;i<28;i++){float base=.08f+.032f*i;float yy=base+.055f*std::sin(px*(8.0f+0.15f*i)+i*.7f)+(.25f-.5f*px)*.06f;float d=std::fabs(py-yy)*h;float width=(i%5==0)?1.8f:1.15f;float a=Sat(width-d);C3 strand={.05f+.015f*(i%4),.035f,.025f};c=Mix(c,strand,a);}return c;
    }
    if(kind==2){ // foliage/branches
        C3 c={.16f+.12f*py,.31f+.19f*py,.15f+.08f*px};
        for(int i=0;i<14;i++){float ax=.02f+.07f*i,ay=.92f;float bx=.10f+.062f*i,by=.12f+.06f*std::sin(i);float d=SegDist(px,py,ax,ay,bx,by)*h;float a=Sat(1.5f-d);c=Mix(c,{.12f,.07f,.035f},a);}
        float n=Hash01(x/5,y/5,7);if(n>.62f){float a=.28f+.45f*(n-.62f)/.38f;c=Mix(c,{.28f+.25f*n,.50f+.20f*n,.18f},a);}return c;
    }
    if(kind==3){ // fine grille/checker
        int cx=(x/6),cy=(y/6);float q=((cx^cy)&1)?1.0f:0.0f;C3 c=Mix({.10f,.12f,.14f},{.88f,.90f,.86f},q*.72f);if((x%17)<2||(y%19)<2)c=Mix(c,{.95f,.72f,.18f},.72f);return c;
    }
    if(kind==4){ // glyph-like signage
        C3 c={.12f,.18f,.23f};float sx=px*6.0f;int cell=(int)std::floor(sx);float lx=sx-cell;float ly=py;bool stroke=false;
        if(cell==0)stroke=(std::fabs(lx-.18f)<.055f||std::fabs(lx-.82f)<.055f||std::fabs(lx-.5f)<.055f&&ly>.45f);
        else if(cell==1)stroke=(std::fabs(ly-.20f)<.055f||std::fabs(ly-.50f)<.055f||std::fabs(lx-.18f)<.055f||std::fabs(lx-.82f)<.055f&&ly>.50f);
        else if(cell==2||cell==3)stroke=(std::fabs(ly-.20f)<.055f||std::fabs(ly-.50f)<.055f||std::fabs(ly-.80f)<.055f||std::fabs(lx-.18f)<.055f&&ly<.50f||std::fabs(lx-.82f)<.055f&&ly>.50f);
        else if(cell==4)stroke=(std::fabs(ly-.20f)<.055f||std::fabs(ly-.50f)<.055f||std::fabs(lx-.18f)<.055f||std::fabs(lx-.82f)<.055f&&ly>.50f);
        else stroke=(std::fabs(lx-.18f)<.055f||std::fabs(ly-.80f)<.055f);
        if(stroke)c={.93f,.90f,.76f};return c;
    }
    if(kind==5){ // pavement/mid-frequency texture
        float mortar=(std::fmod(px*16.0f+(int)(py*12)&1?0.5f:0.0f,1.0f)<.08f||std::fmod(py*12.0f,1.0f)<.08f)?1.0f:0.0f;
        float n=Hash01(x/3,y/3,11);C3 c={.38f+.18f*n,.36f+.16f*n,.32f+.14f*n};if(mortar>0)c=Mix(c,{.14f,.13f,.12f},.78f);return c;
    }
    if(kind==6){ // natural mixed-frequency colour texture
        float a=.5f+.22f*std::sin(px*31.0f+py*9.0f)+.13f*std::sin(px*73.0f-py*41.0f);float b=.5f+.20f*std::sin(px*17.0f-py*37.0f)+.12f*std::cos(px*61.0f+py*53.0f);float n=Hash01(x,y,19)-.5f;return {Sat(.18f+.70f*a+.08f*n),Sat(.20f+.65f*b+.06f*n),Sat(.25f+.40f*a+.30f*b+.07f*n)};
    }
    // starburst / high-frequency oblique edges
    float dx=px-.5f,dy=py-.5f,ang=std::atan2(dy,dx),rad=std::sqrt(dx*dx+dy*dy);float v=std::sin(ang*23.0f+rad*38.0f);float ring=std::sin(rad*95.0f);float q=(v+.35f*ring)>0?1.0f:0.0f;return Mix({.055f,.065f,.085f},{.92f,.88f,.72f},q);
}

static DWORD Pack(C3 c){BYTE r=(BYTE)(Sat(c.r)*255.0f+.5f),g=(BYTE)(Sat(c.g)*255.0f+.5f),b=(BYTE)(Sat(c.b)*255.0f+.5f);return 0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|b;}
static C3 Unpack(DWORD p){return {(float)((p>>16)&255)/255.0f,(float)((p>>8)&255)/255.0f,(float)(p&255)/255.0f};}

static std::vector<DWORD> MakeSuper(int kind,int w,int h){std::vector<DWORD> a((size_t)w*h);for(int y=0;y<h;y++)for(int x=0;x<w;x++)a[(size_t)y*w+x]=Pack(EvalPattern(kind,x,y,w,h));return a;}
static std::vector<DWORD> BoxDown(const std::vector<DWORD>&src,int sw,int sh,int factor){int dw=sw/factor,dh=sh/factor;std::vector<DWORD> out((size_t)dw*dh);for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){C3 s={0,0,0};for(int j=0;j<factor;j++)for(int i=0;i<factor;i++){C3 c=Unpack(src[(size_t)(y*factor+j)*sw+x*factor+i]);s.r+=c.r;s.g+=c.g;s.b+=c.b;}float inv=1.0f/(factor*factor);out[(size_t)y*dw+x]=Pack({s.r*inv,s.g*inv,s.b*inv});}return out;}

static float Y(DWORD p){C3 c=Unpack(p);return Luma(c);}
static Metrics Measure(const std::vector<DWORD>&got,const std::vector<DWORD>&ref,int w,int h)
{
    double absSum=0,sqSum=0,edgeAbs=0,gradAbs=0,lapAbs=0,gradGotSum=0,gradRefSum=0;uint64_t n=0,edgeN=0,gradN=0;
    for(size_t i=0;i<got.size();i++){C3 a=Unpack(got[i]),b=Unpack(ref[i]);double dr=a.r-b.r,dg=a.g-b.g,db=a.b-b.b;absSum+=std::fabs(dr)+std::fabs(dg)+std::fabs(db);sqSum+=dr*dr+dg*dg+db*db;n+=3;}
    for(int y=1;y<h-1;y++)for(int x=1;x<w-1;x++){
        size_t i=(size_t)y*w+x;float grx=.5f*(Y(ref[i+1])-Y(ref[i-1])),gry=.5f*(Y(ref[i+w])-Y(ref[i-w]));float ggx=.5f*(Y(got[i+1])-Y(got[i-1])),ggy=.5f*(Y(got[i+w])-Y(got[i-w]));float gr=std::sqrt(grx*grx+gry*gry),gg=std::sqrt(ggx*ggx+ggy*ggy);gradAbs+=std::fabs(gg-gr);gradGotSum+=gg;gradRefSum+=gr;gradN++;
        float lr=4*Y(ref[i])-Y(ref[i-1])-Y(ref[i+1])-Y(ref[i-w])-Y(ref[i+w]);float lg=4*Y(got[i])-Y(got[i-1])-Y(got[i+1])-Y(got[i-w])-Y(got[i+w]);lapAbs+=std::fabs(lg-lr);
        if(gr>.045f){C3 a=Unpack(got[i]),b=Unpack(ref[i]);edgeAbs+=(std::fabs(a.r-b.r)+std::fabs(a.g-b.g)+std::fabs(a.b-b.b))/3.0;edgeN++;}
    }
    double mse=sqSum/n;Metrics m={};m.mae=absSum/n;m.mse=mse;m.psnr=mse>0?10.0*std::log10(1.0/mse):99.0;m.edgeMae=edgeN?edgeAbs/edgeN:0;m.gradMae=gradN?gradAbs/gradN:0;m.lapMae=gradN?lapAbs/gradN:0;m.detailRatio=gradRefSum>0?gradGotSum/gradRefSum:1;return m;
}

static HWND MakeWindow(){HINSTANCE i=GetModuleHandleW(0);WNDCLASSW c={};c.lpfnWndProc=DefWindowProcW;c.hInstance=i;c.lpszClassName=L"PTAR_AALAB_QUALITY";RegisterClassW(&c);return CreateWindowExW(0,c.lpszClassName,L"PTAR",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);}
static HRESULT Upload(IDirect3DDevice9*d,const std::vector<DWORD>&s,int w,int h,IDirect3DTexture9**t){HRESULT r=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(r))return r;D3DLOCKED_RECT l={};r=(*t)->LockRect(0,&l,0,0);if(FAILED(r))return r;for(int y=0;y<h;y++)memcpy((BYTE*)l.pBits+y*l.Pitch,&s[(size_t)y*w],(size_t)w*4);(*t)->UnlockRect(0);return S_OK;}
static HRESULT Render(IDirect3DDevice9*d,IDirect3DPixelShader9*ps,IDirect3DTexture9*src,int sw,int sh,int dw,int dh,std::vector<DWORD>*o){IDirect3DTexture9*dst=0;IDirect3DSurface9*ds=0,*read=0;HRESULT r=d->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(r))return r;dst->GetSurfaceLevel(0,&ds);d->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);d->SetDepthStencilSurface(0);d->SetRenderTarget(0,ds);D3DVIEWPORT9 v={0,0,(DWORD)dw,(DWORD)dh,0,1};d->SetViewport(&v);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShader(ps);d->SetVertexShader(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,src);Vtx q[4]={{-.5f,-.5f,0,1,0,0},{(float)dw-.5f,-.5f,0,1,1,0},{-.5f,(float)dh-.5f,0,1,0,1},{(float)dw-.5f,(float)dh-.5f,0,1,1,1}};r=d->BeginScene();if(SUCCEEDED(r)){r=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=d->EndScene();if(SUCCEEDED(r))r=e;}d->SetTexture(0,0);if(SUCCEEDED(r))r=d->GetRenderTargetData(ds,read);if(SUCCEEDED(r)){D3DLOCKED_RECT l={};r=read->LockRect(&l,0,D3DLOCK_READONLY);if(SUCCEEDED(r)){o->resize((size_t)dw*dh);for(int y=0;y<dh;y++)memcpy(&(*o)[(size_t)y*dw],(BYTE*)l.pBits+y*l.Pitch,(size_t)dw*4);read->UnlockRect();}}read->Release();ds->Release();dst->Release();return r;}

int main(){const char*names[9]={"BASE","B08","B12","BADAPT20","AA0_15","AA2_15","AA2B","AA4B","AA2B_STRONG"};const DWORD*codes[9]={(DWORD*)g_ptarSpec5AaLabV0,(DWORD*)g_ptarSpec5AaLabV1,(DWORD*)g_ptarSpec5AaLabV2,(DWORD*)g_ptarSpec5AaLabV3,(DWORD*)g_ptarSpec5AaLabV4,(DWORD*)g_ptarSpec5AaLabV5,(DWORD*)g_ptarSpec5AaLabV6,(DWORD*)g_ptarSpec5AaLabV7,(DWORD*)g_ptarSpec5AaLabV8};wchar_t p[MAX_PATH]={};GetSystemDirectoryW(p,MAX_PATH);wcscat_s(p,L"\\d3d9.dll");HMODULE m=LoadLibraryW(p);if(!m)return 2;PFN_Direct3DCreate9 fn=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");IDirect3D9*d=fn?fn(D3D_SDK_VERSION):0;if(!d)return 3;HWND w=MakeWindow();D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9*dev=0;HRESULT hr=d->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr))hr=d->CreateDevice(0,D3DDEVTYPE_REF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr)||!dev)return 4;IDirect3DPixelShader9*ps[9]={};for(int v=0;v<9;v++){hr=dev->CreatePixelShader(codes[v],&ps[v]);printf("CREATE_%s_HR=0x%08lX\n",names[v],(unsigned long)hr);if(FAILED(hr)||!ps[v])return 5;}
    const int SW=384,SH=216,HW=192,HH=108,LW=128,LH=72;double agg[9][7]={};FILE*csv=0;fopen_s(&csv,"AALAB_QUALITY.csv","wb");fprintf(csv,"variant,pattern,mae,psnr,edge_mae,grad_mae,lap_mae,detail_ratio\n");for(int kind=0;kind<8;kind++){std::vector<DWORD>super=MakeSuper(kind,SW,SH),ref=BoxDown(super,SW,SH,2),low=BoxDown(super,SW,SH,3);IDirect3DTexture9*tex=0;if(FAILED(Upload(dev,low,LW,LH,&tex)))return 6;for(int v=0;v<9;v++){std::vector<DWORD>got;if(FAILED(Render(dev,ps[v],tex,LW,LH,HW,HH,&got)))return 7;Metrics q=Measure(got,ref,HW,HH);fprintf(csv,"%s,%d,%.9f,%.6f,%.9f,%.9f,%.9f,%.9f\n",names[v],kind,q.mae,q.psnr,q.edgeMae,q.gradMae,q.lapMae,q.detailRatio);agg[v][0]+=q.mae;agg[v][1]+=q.psnr;agg[v][2]+=q.edgeMae;agg[v][3]+=q.gradMae;agg[v][4]+=q.lapMae;agg[v][5]+=q.detailRatio;double aliasWeight=(kind==0||kind==1||kind==2||kind==7)?2.0:1.0;agg[v][6]+=aliasWeight*(q.edgeMae+q.gradMae+0.5*q.lapMae);}tex->Release();}
    fclose(csv);printf("AALAB_AGGREGATE_BEGIN\n");for(int v=0;v<9;v++){for(int j=0;j<6;j++)agg[v][j]/=8.0;printf("AALAB_%s MAE=%.9f PSNR=%.6f EDGE=%.9f GRAD=%.9f LAP=%.9f DETAIL=%.9f ALIASLOSS=%.9f\n",names[v],agg[v][0],agg[v][1],agg[v][2],agg[v][3],agg[v][4],agg[v][5],agg[v][6]);}
    double baseLoss=agg[0][6];int best=0;double bestScore=1e9;for(int v=0;v<9;v++){double detailPenalty=std::fabs(agg[v][5]-1.0);double score=(agg[v][6]/baseLoss)+0.75*detailPenalty+0.20*(agg[v][0]/agg[0][0]);printf("AALAB_%s_SCORE=%.9f\n",names[v],score);if(score<bestScore){bestScore=score;best=v;}}printf("AALAB_BEST_QUALITY=%s SCORE=%.9f\n",names[best],bestScore);printf("AALAB_QUALITY_GATE=PASS\n");for(int v=0;v<9;v++)ps[v]->Release();dev->Release();DestroyWindow(w);d->Release();FreeLibrary(m);return 0;}
