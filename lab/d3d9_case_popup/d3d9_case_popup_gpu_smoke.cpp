#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <string>
#pragma warning(disable:4505)
#include "ptar_gw16i_hud_d3d9.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);

static const DWORD SENTINEL=0x00112233u;
static const DWORD FRAME=0x00566274u;
static const DWORD BG=0x00030305u;
static const DWORD FG=0x00EBF5FFu;
static const UINT W=960,H=540;
static const LONG OX=16,OY=16;

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_CASE_POPUP_GPU_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"PTAR Case Popup GPU Smoke",
        WS_OVERLAPPEDWINDOW,0,0,960,540,0,0,inst,0);
}

static bool ReadPixels(IDirect3DDevice9* dev,IDirect3DSurface9* rt,std::vector<DWORD>& out)
{
    IDirect3DSurface9* read=0;
    HRESULT hr=dev->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);
    if(FAILED(hr)) return false;
    hr=dev->GetRenderTargetData(rt,read);
    if(FAILED(hr)){read->Release();return false;}
    D3DLOCKED_RECT lr={};
    hr=read->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)){read->Release();return false;}
    out.resize(W*H);
    for(UINT y=0;y<H;++y)
    {
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<W;++x) out[y*W+x]=row[x]&0x00FFFFFFu;
    }
    read->UnlockRect();
    read->Release();
    return true;
}

static bool SaveBmp(const char* path,const std::vector<DWORD>& p)
{
    FILE* f=0;
    if(fopen_s(&f,path,"wb")!=0 || !f) return false;
    BITMAPFILEHEADER bfh={};
    BITMAPINFOHEADER bih={};
    const DWORD bytes=W*H*4;
    bfh.bfType=0x4D42;
    bfh.bfOffBits=sizeof(bfh)+sizeof(bih);
    bfh.bfSize=bfh.bfOffBits+bytes;
    bih.biSize=sizeof(bih);
    bih.biWidth=(LONG)W;
    bih.biHeight=-(LONG)H;
    bih.biPlanes=1;
    bih.biBitCount=32;
    bih.biCompression=BI_RGB;
    bih.biSizeImage=bytes;
    fwrite(&bfh,sizeof(bfh),1,f);
    fwrite(&bih,sizeof(bih),1,f);
    for(UINT y=0;y<H;++y)
    {
        for(UINT x=0;x<W;++x)
        {
            DWORD rgb=p[y*W+x];
            BYTE px[4]={(BYTE)(rgb&255),(BYTE)((rgb>>8)&255),(BYTE)((rgb>>16)&255),0xFF};
            fwrite(px,4,1,f);
        }
    }
    fclose(f);
    return true;
}

static int CountColor(const std::vector<DWORD>& p,DWORD c,LONG x1,LONG y1,LONG x2,LONG y2)
{
    int n=0;
    for(LONG y=y1;y<y2;++y) for(LONG x=x1;x<x2;++x)
        if(p[(size_t)y*W+x]==c) ++n;
    return n;
}

static int ProveCase(IDirect3DDevice9* dev,IDirect3DSurface9* rt,int type,int a,const char* label,const char* snapshot)
{
    dev->SetRenderTarget(0,rt);
    dev->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0x11,0x22,0x33),1.0f,0);
    PtGw16FeedbackPanel(dev,OX,OY,type,a,0,0,D3DCOLOR_XRGB(3,3,5),D3DCOLOR_XRGB(235,245,255));

    std::vector<DWORD> p;
    if(!ReadPixels(dev,rt,p)) return 20;

    if(p[(size_t)OY*W+OX]!=FRAME) return 21;
    if(p[(size_t)(OY+4)*W+(OX+4)]!=BG) return 22;
    if(p[(size_t)(OY+47)*W+(OX+10)]!=FRAME) return 23;
    if(p[(size_t)(OY+52)*W+(OX+4)]!=BG) return 24;
    if(p[(size_t)OY*W+(OX-1)]!=SENTINEL) return 25;
    if(p[(size_t)(OY+96)*W+OX]!=SENTINEL) return 26;
    if(p[(size_t)OY*W+(OX+440)]!=SENTINEL) return 27;

    LONG minx=9999,miny=9999,maxx=-1,maxy=-1;
    int changed=0;
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x)
    {
        if(p[(size_t)y*W+x]!=SENTINEL)
        {
            ++changed;
            if((LONG)x<minx)minx=(LONG)x; if((LONG)x>maxx)maxx=(LONG)x;
            if((LONG)y<miny)miny=(LONG)y; if((LONG)y>maxy)maxy=(LONG)y;
        }
    }
    if(minx!=OX || miny!=OY || maxx!=OX+439 || maxy!=OY+95) return 28;
    const int topFg=CountColor(p,FG,OX+8,OY+8,OX+432,OY+42);
    const int bottomFg=CountColor(p,FG,OX+8,OY+54,OX+432,OY+90);
    const int framePixels=CountColor(p,FRAME,OX,OY,OX+440,OY+96);
    const int bgPixels=CountColor(p,BG,OX,OY,OX+440,OY+96);
    if(topFg<40) return 29;
    if(bottomFg<20) return 30;
    if(framePixels<3000) return 31;
    if(bgPixels<20000) return 32;

    if(snapshot && snapshot[0] && !SaveBmp(snapshot,p)) return 33;
    std::printf("%s type=%d a=%d bbox=440x96 changed=%d frame=%d bg=%d top_fg=%d bottom_fg=%d CELLS=2 PASS\n",
        label,type,a,changed,framePixels,bgPixels,topFg,bottomFg);
    return 0;
}

int main()
{
    wchar_t sys[MAX_PATH]={0};
    if(!GetSystemDirectoryW(sys,MAX_PATH)) return 2;
    wcscat_s(sys,L"\\d3d9.dll");
    HMODULE mod=LoadLibraryW(sys); if(!mod) return 3;
    PFN_Direct3DCreate9 create9=(PFN_Direct3DCreate9)GetProcAddress(mod,"Direct3DCreate9"); if(!create9) return 4;
    IDirect3D9* d3d=create9(D3D_SDK_VERSION); if(!d3d) return 5;
    HWND hwnd=MakeWindow(); if(!hwnd) return 6;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=W; pp.BackBufferHeight=H; pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE; pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)) hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr)||!dev) return 7;
    IDirect3DTexture9* tex=0; IDirect3DSurface9* rt=0;
    hr=dev->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&tex,0); if(FAILED(hr)) return 8;
    hr=tex->GetSurfaceLevel(0,&rt); if(FAILED(hr)) return 9;
    dev->SetDepthStencilSurface(0);

    struct T{int type;int a;const char* label;const char* snap;};
    const T tests[]={
        {19,0,"F5_PRE","artifact/F5_PRE_CASE.bmp"},
        {20,0,"F5_FG_ACTIVE",0},{21,0,"F5_POST",0},{22,0,"F5_MEASURE",0},{23,0,"F5_DONE",0},
        {24,0,"F1_READY_60",0},{24,1,"F1_READY_120","artifact/F1_READY_120_CASE.bmp"},
        {25,0,"F1_MEASURE_60",0},{25,1,"F1_MEASURE_120",0},
        {26,0,"F1_DONE",0},{27,0,"F1_ERROR",0},{28,0,"F5_ERROR",0}
    };
    for(size_t i=0;i<_countof(tests);++i)
    {
        int rc=ProveCase(dev,rt,tests[i].type,tests[i].a,tests[i].label,tests[i].snap);
        if(rc){std::printf("FAIL %s rc=%d\n",tests[i].label,rc);return rc+(int)i;}
    }

    rt->Release(); tex->Release(); dev->Release(); DestroyWindow(hwnd); d3d->Release(); FreeLibrary(mod);
    std::printf("D3D9_CASE_POPUP_GPU_TOPOLOGY=PASS\n");
    std::printf("CASE_FOOTPRINT=440x96 OUTER_FRAME=YES INNER_CELLS=2 ROWS=2\n");
    return 0;
}
