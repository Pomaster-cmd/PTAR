#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdint>
#include "ptar_gw16i_hud_ps_bytecode.h"
#include "ptar_gw16i_feedback_ps_bytecode.h"

typedef IDirect3D9* (WINAPI *PFN_Direct3DCreate9)(UINT);
struct Vtx { float x,y,z,rhw,u,v; };

static HWND MakeWindow()
{
    HINSTANCE inst=GetModuleHandleW(0);
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW;
    wc.hInstance=inst;
    wc.lpszClassName=L"PTAR_GW16I_HUD_GPU_SMOKE";
    RegisterClassW(&wc);
    return CreateWindowExW(
        0,wc.lpszClassName,L"PTAR GW16I HUD GPU Smoke",
        WS_OVERLAPPEDWINDOW,0,0,960,540,0,0,inst,0);
}

static HRESULT DrawOverlay(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* target,
    UINT w,UINT h,
    IDirect3DPixelShader9* ps,
    const float* constants,
    UINT constantCount)
{
    HRESULT hr=dev->SetDepthStencilSurface(0);
    if(FAILED(hr)) return hr;
    hr=dev->SetRenderTarget(0,target);
    if(FAILED(hr)) return hr;

    D3DVIEWPORT9 vp={0,0,w,h,0.0f,1.0f};
    hr=dev->SetViewport(&vp);
    if(FAILED(hr)) return hr;

    dev->SetRenderState(D3DRS_ZENABLE,FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,0xF);

    hr=dev->SetPixelShaderConstantF(0,constants,constantCount);
    if(FAILED(hr)) return hr;

    dev->SetPixelShader(ps);
    dev->SetVertexShader(0);
    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);

    hr=dev->BeginScene();
    if(FAILED(hr)) return hr;

    Vtx q[4]={
        {-0.5f,-0.5f,0,1,0,0},
        {(float)w-0.5f,-0.5f,0,1,1,0},
        {-0.5f,(float)h-0.5f,0,1,0,1},
        {(float)w-0.5f,(float)h-0.5f,0,1,1,1}
    };

    hr=dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vtx));
    HRESULT endHr=dev->EndScene();
    if(FAILED(hr)) return hr;
    return endHr;
}

static int Analyze(
    IDirect3DDevice9* dev,
    IDirect3DSurface9* rt,
    UINT w,UINT h,
    const char* label,
    UINT minChanged)
{
    IDirect3DSurface9* read=0;
    HRESULT hr=dev->CreateOffscreenPlainSurface(
        w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,0);
    if(FAILED(hr)) return 30;
    hr=dev->GetRenderTargetData(rt,read);
    if(FAILED(hr)) return 31;

    D3DLOCKED_RECT lr={};
    hr=read->LockRect(&lr,0,D3DLOCK_READONLY);
    if(FAILED(hr)) return 32;

    uint32_t changed=0;
    uint32_t changedTopLeft=0;
    uint32_t changedOutside=0;
    for(UINT y=0;y<h;++y)
    {
        const DWORD* row=(const DWORD*)((const BYTE*)lr.pBits+y*lr.Pitch);
        for(UINT x=0;x<w;++x)
        {
            const DWORD rgb=row[x]&0x00FFFFFFu;
            const bool isBg=(rgb==0x00112233u);
            if(!isBg)
            {
                ++changed;
                if(x<700 && y<220) ++changedTopLeft;
                else ++changedOutside;
            }
        }
    }
    read->UnlockRect();
    read->Release();

    std::printf(
        "%s changed=%u top_left=%u outside=%u\n",
        label,changed,changedTopLeft,changedOutside);

    if(changed<minChanged || changedTopLeft<minChanged)
    {
        std::printf("FAIL %s no visible HUD pixels\n",label);
        return 33;
    }

    return 0;
}

int main()
{
    wchar_t sys[MAX_PATH]={0};
    if(!GetSystemDirectoryW(sys,MAX_PATH)) return 2;
    wcscat_s(sys,L"\\d3d9.dll");
    HMODULE d3d9=LoadLibraryW(sys);
    if(!d3d9) return 3;

    PFN_Direct3DCreate9 create9=
        (PFN_Direct3DCreate9)GetProcAddress(d3d9,"Direct3DCreate9");
    if(!create9) return 4;

    IDirect3D9* d3d=create9(D3D_SDK_VERSION);
    if(!d3d) return 5;
    HWND hwnd=MakeWindow();
    if(!hwnd) return 6;

    D3DPRESENT_PARAMETERS pp={};
    pp.BackBufferWidth=960;
    pp.BackBufferHeight=540;
    pp.BackBufferFormat=D3DFMT_UNKNOWN;
    pp.BackBufferCount=1;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=hwnd;
    pp.Windowed=TRUE;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9* dev=0;
    HRESULT hr=d3d->CreateDevice(
        D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if(FAILED(hr))
    {
        hr=d3d->CreateDevice(
            D3DADAPTER_DEFAULT,D3DDEVTYPE_REF,hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    }
    if(FAILED(hr)||!dev) return 7;

    IDirect3DTexture9* rtTex=0;
    IDirect3DSurface9* rt=0;
    hr=dev->CreateTexture(
        960,540,1,D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&rtTex,0);
    if(FAILED(hr)) return 8;
    hr=rtTex->GetSurfaceLevel(0,&rt);
    if(FAILED(hr)) return 9;

    IDirect3DPixelShader9* hud=0;
    IDirect3DPixelShader9* feedback=0;
    hr=dev->CreatePixelShader((const DWORD*)g_ptarGw16iHudPs,&hud);
    if(FAILED(hr)||!hud) return 10;
    hr=dev->CreatePixelShader((const DWORD*)g_ptarGw16iFeedbackPs,&feedback);
    if(FAILED(hr)||!feedback) return 11;

    dev->SetRenderTarget(0,rt);
    dev->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0x11,0x22,0x33),1.0f,0);

    float hudC[20]={0};
    hudC[2]=59.0f;
    hudC[3]=1.0f;
    hudC[4]=1280.0f;
    hudC[5]=720.0f;
    hudC[6]=2.0f;
    hudC[7]=0.0f;
    hudC[8]=(float)VK_F10;
    hudC[9]=0.0f;
    hudC[10]=(float)VK_F11;
    hudC[11]=2.0f;
    hudC[12]=123.0f;
    hudC[13]=0.0f;
    hudC[14]=1.0f;

    hr=DrawOverlay(dev,rt,960,540,hud,hudC,5);
    std::printf("HUD_DRAW_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr)) return 12;

    int rc=Analyze(dev,rt,960,540,"HUD_MAIN",200);
    if(rc) return rc;

    dev->SetRenderTarget(0,rt);
    dev->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0x11,0x22,0x33),1.0f,0);

    float fbC[8]={0};
    fbC[0]=16.0f;
    fbC[1]=16.0f;
    fbC[2]=5.0f; // F8 OK
    fbC[4]=0.0f;

    hr=DrawOverlay(dev,rt,960,540,feedback,fbC,2);
    std::printf("FEEDBACK_DRAW_HR=0x%08lX\n",(unsigned long)hr);
    if(FAILED(hr)) return 13;

    rc=Analyze(dev,rt,960,540,"HUD_FEEDBACK",50);
    if(rc) return rc;

    feedback->Release();
    hud->Release();
    rt->Release();
    rtTex->Release();
    dev->Release();
    DestroyWindow(hwnd);
    d3d->Release();
    FreeLibrary(d3d9);

    std::printf("D3D9_GW16I_HUD_GPU_SMOKE=PASS\n");
    return 0;
}
