#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <vector>
#include <set>
#include <cstring>

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_POINT_ORACLE";
    RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"PTAR",WS_OVERLAPPEDWINDOW,0,0,64,64,0,0,inst,0);
}

static DWORD Pixel(int x,int y)
{
    const BYTE r=(BYTE)(20+x*47+y*9);
    const BYTE g=(BYTE)(30+y*43+x*7);
    const BYTE b=(BYTE)(40+x*19+y*31);
    return 0xff000000u|((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;
}

static bool ReadSurface(IDirect3DDevice9* dev,IDirect3DSurface9* src,int w,int h,std::vector<DWORD>* out)
{
    IDirect3DSurface9* sys=0;
    HRESULT hr=dev->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,0);
    if(FAILED(hr)||!sys) return false;
    hr=dev->GetRenderTargetData(src,sys);
    if(FAILED(hr)){sys->Release();return false;}
    D3DLOCKED_RECT lr={};
    hr=sys->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)){sys->Release();return false;}
    out->resize((size_t)w*h);
    for(int y=0;y<h;++y)
        std::memcpy(&(*out)[(size_t)y*w],(BYTE*)lr.pBits+y*lr.Pitch,(size_t)w*4);
    sys->UnlockRect();
    sys->Release();
    return true;
}

int main()
{
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION);
    if(!d3d) return 2;
    HWND hwnd=MakeWindow();
    if(!hwnd) return 3;
    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    std::printf("POINT_ORACLE_DEVICE_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr)||!dev) return 4;

    const int sw=4,sh=4,dw=6,dh=6;
    IDirect3DSurface9* src=0;IDirect3DSurface9* dstPoint=0;IDirect3DSurface9* dstLinear=0;IDirect3DSurface9* upload=0;
    hr=dev->CreateRenderTarget(sw,sh,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&src,0);if(FAILED(hr))return 5;
    hr=dev->CreateRenderTarget(dw,dh,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&dstPoint,0);if(FAILED(hr))return 6;
    hr=dev->CreateRenderTarget(dw,dh,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&dstLinear,0);if(FAILED(hr))return 7;
    hr=dev->CreateOffscreenPlainSurface(sw,sh,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&upload,0);if(FAILED(hr))return 8;
    D3DLOCKED_RECT lr={};hr=upload->LockRect(&lr,0,0);if(FAILED(hr))return 9;
    std::set<DWORD> sourceColors;
    for(int y=0;y<sh;++y)
    {
        DWORD* row=(DWORD*)((BYTE*)lr.pBits+y*lr.Pitch);
        for(int x=0;x<sw;++x){row[x]=Pixel(x,y);sourceColors.insert(row[x]);}
    }
    upload->UnlockRect();
    POINT zero={0,0};
    hr=dev->UpdateSurface(upload,0,src,&zero);if(FAILED(hr))return 10;

    hr=dev->StretchRect(src,0,dstPoint,0,D3DTEXF_POINT);
    std::printf("POINT_ORACLE_STRETCH_POINT_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr))return 11;
    hr=dev->StretchRect(src,0,dstLinear,0,D3DTEXF_LINEAR);
    std::printf("POINT_ORACLE_STRETCH_LINEAR_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr))return 12;

    std::vector<DWORD> p,l;
    if(!ReadSurface(dev,dstPoint,dw,dh,&p) || !ReadSurface(dev,dstLinear,dw,dh,&l))return 13;
    unsigned pointForeign=0,linearForeign=0,pointLinearDifferent=0;
    std::set<DWORD> pointColors;
    for(size_t i=0;i<p.size();++i)
    {
        pointColors.insert(p[i]);
        if(sourceColors.find(p[i])==sourceColors.end())++pointForeign;
        if(sourceColors.find(l[i])==sourceColors.end())++linearForeign;
        if(p[i]!=l[i])++pointLinearDifferent;
    }
    std::printf("POINT_ORACLE_POINT_FOREIGN_COLORS=%u\n",pointForeign);
    std::printf("POINT_ORACLE_LINEAR_FOREIGN_PIXELS=%u\n",linearForeign);
    std::printf("POINT_ORACLE_POINT_LINEAR_DIFFERENT=%u\n",pointLinearDifferent);
    std::printf("POINT_ORACLE_POINT_DISTINCT_COLORS=%u\n",(unsigned)pointColors.size());
    const bool ok=pointForeign==0 && linearForeign>0 && pointLinearDifferent>0 && pointColors.size()>=9;
    std::printf("POINT_STRETCH_ORACLE=%s\n",ok?"PASS":"FAIL");

    upload->Release();dstLinear->Release();dstPoint->Release();src->Release();dev->Release();DestroyWindow(hwnd);d3d->Release();
    return ok?0:1;
}
