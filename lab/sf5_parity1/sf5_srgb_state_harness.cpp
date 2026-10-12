#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <cstring>
#include "../../build_x86/ptar_old_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx{float x,y,z,rhw,u,v;};

static HWND MakeWindow(){HINSTANCE i=GetModuleHandleW(0);WNDCLASSW w={};w.lpfnWndProc=DefWindowProcW;w.hInstance=i;w.lpszClassName=L"PTAR_SRGB_STATE";RegisterClassW(&w);return CreateWindowExW(0,w.lpszClassName,L"PTAR sRGB state",WS_OVERLAPPEDWINDOW,0,0,320,240,0,0,i,0);}

static HRESULT MakeSource(IDirect3DDevice9* d,int w,int h,IDirect3DTexture9** t)
{
    HRESULT hr=d->CreateTexture(w,h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,t,0);if(FAILED(hr))return hr;D3DLOCKED_RECT lr={};hr=(*t)->LockRect(0,&lr,0,0);if(FAILED(hr))return hr;
    for(int y=0;y<h;++y){DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);for(int x=0;x<w;++x){BYTE r=(BYTE)(16+(x*220)/(w-1));BYTE g=(BYTE)(16+(y*220)/(h-1));BYTE b=(BYTE)((((x/3)^(y/5))&1)?230:25);row[x]=0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|b;}}
    (*t)->UnlockRect(0);return S_OK;
}

static HRESULT Render(IDirect3DDevice9* d,IDirect3DPixelShader9* ps,IDirect3DTexture9* src,int sw,int sh,int dw,int dh,bool inheritedSrgb,bool isolate,std::vector<DWORD>* out)
{
    IDirect3DTexture9* dst=0;IDirect3DSurface9* ds=0,*read=0;HRESULT hr=d->CreateTexture(dw,dh,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&dst,0);if(FAILED(hr))return hr;hr=dst->GetSurfaceLevel(0,&ds);if(FAILED(hr))return hr;hr=d->CreateOffscreenPlainSurface(dw,dh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);if(FAILED(hr))return hr;
    d->SetRenderTarget(0,ds);d->SetDepthStencilSurface(0);D3DVIEWPORT9 vp={0,0,(DWORD)dw,(DWORD)dh,0,1};d->SetViewport(&vp);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,inheritedSrgb?TRUE:FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,inheritedSrgb?TRUE:FALSE);
    if(isolate){d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);}
    float c0[4]={(float)sw,(float)sh,(float)dw,(float)dh};d->SetPixelShaderConstantF(0,c0,1);d->SetPixelShader(ps);d->SetVertexShader(0);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,src);
    Vtx q[4]={{-.5f,-.5f,0,1,0,0},{(float)dw-.5f,-.5f,0,1,1,0},{-.5f,(float)dh-.5f,0,1,0,1},{(float)dw-.5f,(float)dh-.5f,0,1,1,1}};hr=d->BeginScene();if(SUCCEEDED(hr)){hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));HRESULT e=d->EndScene();if(SUCCEEDED(hr))hr=e;}d->SetTexture(0,0);if(FAILED(hr))return hr;
    hr=d->GetRenderTargetData(ds,read);if(FAILED(hr))return hr;D3DLOCKED_RECT lr={};hr=read->LockRect(&lr,0,D3DLOCK_READONLY);if(FAILED(hr))return hr;out->resize((size_t)dw*dh);for(int y=0;y<dh;++y)memcpy(&(*out)[(size_t)y*dw],(BYTE*)lr.pBits+y*lr.Pitch,(size_t)dw*4);read->UnlockRect();read->Release();ds->Release();dst->Release();return S_OK;
}

static unsigned Diff(const std::vector<DWORD>& a,const std::vector<DWORD>& b,uint64_t* absSum)
{
    unsigned n=0;*absSum=0;for(size_t i=0;i<a.size();++i){bool px=false;for(int s=0;s<=16;s+=8){int x=(a[i]>>s)&255,y=(b[i]>>s)&255,d=x-y;if(d<0)d=-d;*absSum+=(uint64_t)d;if(d)px=true;}if(px)++n;}return n;
}

int main()
{
    wchar_t p[MAX_PATH]={0};if(!GetSystemDirectoryW(p,MAX_PATH))return 2;wcscat_s(p,L"\\d3d9.dll");HMODULE m=LoadLibraryW(p);if(!m)return 3;PFN_Direct3DCreate9 c=(PFN_Direct3DCreate9)GetProcAddress(m,"Direct3DCreate9");if(!c)return 4;IDirect3D9* d=c(D3D_SDK_VERSION);if(!d)return 5;HWND w=MakeWindow();if(!w)return 6;
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;IDirect3DDevice9* dev=0;HRESULT hr=d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr))hr=d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);if(FAILED(hr)||!dev)return 7;
    IDirect3DPixelShader9* ps=0;if(FAILED(dev->CreatePixelShader((DWORD*)g_ptarOldPs,&ps)))return 8;IDirect3DTexture9* src=0;if(FAILED(MakeSource(dev,48,32,&src)))return 9;std::vector<DWORD> off,on,isolated;
    if(FAILED(Render(dev,ps,src,48,32,72,48,false,false,&off)))return 10;if(FAILED(Render(dev,ps,src,48,32,72,48,true,false,&on)))return 11;if(FAILED(Render(dev,ps,src,48,32,72,48,true,true,&isolated)))return 12;
    uint64_t sumLeak=0,sumFixed=0;unsigned leak=Diff(off,on,&sumLeak),fixed=Diff(off,isolated,&sumFixed);std::printf("SF5_SRGB_INHERITED_CHANGED_PIXELS=%u\n",leak);std::printf("SF5_SRGB_INHERITED_ABS_SUM=%llu\n",(unsigned long long)sumLeak);std::printf("SF5_SRGB_ISOLATED_CHANGED_PIXELS=%u\n",fixed);std::printf("SF5_SRGB_ISOLATED_ABS_SUM=%llu\n",(unsigned long long)sumFixed);
    bool exposure=leak>0&&sumLeak>0;bool stable=fixed==0&&sumFixed==0;std::printf("SF5_SRGB_STATE_LEAK_EXPOSED=%s\n",exposure?"PASS":"FAIL");std::printf("SF5_SRGB_STATE_ISOLATION=%s\n",stable?"PASS":"FAIL");
    src->Release();ps->Release();dev->Release();DestroyWindow(w);d->Release();FreeLibrary(m);return(exposure&&stable)?0:1;
}
