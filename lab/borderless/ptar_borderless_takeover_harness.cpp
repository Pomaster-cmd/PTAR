#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include "include/ptar_borderless_geometry.h"

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")

using ptar_lab::Geometry;

static HWND g_game=nullptr,g_presenter=nullptr,g_decoy=nullptr;
static RECT g_output{};
static UINT g_gameW=640,g_gameH=360;
static bool g_takeover=false,g_internalWindowChange=false;
static LONG g_lastLogicalX=-1,g_lastLogicalY=-1;
static unsigned long long g_windowClamps=0,g_focusReturns=0;
static const UINT WM_PTAR_LOGICAL_MOUSE=WM_APP+0x41;

static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}

static Geometry geometry(){return Geometry{g_output,g_gameW,g_gameH};}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(g_takeover&&!g_internalWindowChange&&m==WM_WINDOWPOSCHANGING){
        WINDOWPOS*p=reinterpret_cast<WINDOWPOS*>(l);
        if(!(p->flags&SWP_NOMOVE)){p->x=g_output.left;p->y=g_output.top;}
        if(!(p->flags&SWP_NOSIZE)){p->cx=LONG(g_gameW);p->cy=LONG(g_gameH);}
        ++g_windowClamps;
        return 0;
    }
    if(m==WM_PTAR_LOGICAL_MOUSE){
        g_lastLogicalX=GET_X_LPARAM(l);
        g_lastLogicalY=GET_Y_LPARAM(l);
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE){
        SetForegroundWindow(g_game);
        SetActiveWindow(g_game);
        SetFocus(g_game);
        ++g_focusReturns;
        return MA_NOACTIVATE;
    }
    switch(m){
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:case WM_XBUTTONUP:{
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        ClientToScreen(h,&p);
        POINT logical=geometry().physical_to_logical(p);
        SendMessageW(g_game,WM_PTAR_LOGICAL_MOUSE,w,MAKELPARAM(logical.x,logical.y));
        return 0;
    }
    default:return DefWindowProcW(h,m,w,l);
    }
}

static LRESULT CALLBACK DecoyProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}

static bool set_game_geometry(UINT w,UINT h){
    g_gameW=w;g_gameH=h;
    g_internalWindowChange=true;
    BOOL ok=SetWindowPos(g_game,nullptr,g_output.left,g_output.top,LONG(w),LONG(h),SWP_NOZORDER|SWP_NOACTIVATE);
    g_internalWindowChange=false;
    pump();
    return !!ok;
}

static bool client_is(HWND h,UINT w,UINT hh){RECT r{};return GetClientRect(h,&r)&&UINT(r.right-r.left)==w&&UINT(r.bottom-r.top)==hh;}
static bool window_is(HWND h,const RECT&r){RECT a{};return GetWindowRect(h,&a)&&a.left==r.left&&a.top==r.top&&a.right==r.right&&a.bottom==r.bottom;}

static bool backbuffer_size(IDXGISwapChain*sc,UINT&w,UINT&h){
    ID3D11Texture2D*t=nullptr;
    if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&t))||!t)return false;
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);t->Release();w=d.Width;h=d.Height;return true;
}
static bool verify_swap_size(IDXGISwapChain*sc,UINT w,UINT h){UINT a=0,b=0;return backbuffer_size(sc,a,b)&&a==w&&b==h;}

static IDXGISwapChain* make_swapchain(IDXGIFactory*f,ID3D11Device*d,HWND h,UINT w,UINT hh){
    DXGI_SWAP_CHAIN_DESC x{};
    x.BufferDesc.Width=w;x.BufferDesc.Height=hh;x.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    x.SampleDesc.Count=1;x.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT|DXGI_USAGE_SHADER_INPUT;x.BufferCount=1;
    x.OutputWindow=h;x.Windowed=TRUE;x.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain*s=nullptr;
    return SUCCEEDED(f->CreateSwapChain(d,&x,&s))?s:nullptr;
}

static int fail(const char*what,int code){std::printf("FAIL code=%d %s\n",code,what);return code;}

int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"PTARIntegratedGame";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARIntegratedPresenter";pc.style=CS_DBLCLKS;
    WNDCLASSW dc{};dc.hInstance=hi;dc.lpfnWndProc=DecoyProc;dc.lpszClassName=L"PTARIntegratedDecoy";
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||
       (!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||
       (!RegisterClassW(&dc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;

    POINT zero{0,0};HMONITOR hm=MonitorFromPoint(zero,MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!hm||!GetMonitorInfoW(hm,&mi))return 11;
    g_output=mi.rcMonitor;
    const UINT outW=UINT(g_output.right-g_output.left),outH=UINT(g_output.bottom-g_output.top);
    if(outW<320||outH<180)return 12;

    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,g_output.left,g_output.top,LONG(g_gameW),LONG(g_gameH),nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,g_output.left,g_output.top,LONG(outW),LONG(outH),g_game,nullptr,hi,nullptr);
    g_decoy=CreateWindowExW(0,dc.lpszClassName,L"decoy",WS_OVERLAPPEDWINDOW,g_output.left+40,g_output.top+40,320,240,nullptr,nullptr,hi,nullptr);
    if(!g_game||!g_presenter||!g_decoy)return 13;
    ShowWindow(g_game,SW_SHOW);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);ShowWindow(g_decoy,SW_SHOWNA);pump();

    DWORD ps=DWORD(GetWindowLongPtrW(g_presenter,GWL_STYLE));
    DWORD pe=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
    if(!(ps&WS_POPUP)||(ps&(WS_CAPTION|WS_THICKFRAME)))return fail("presenter_style",14);
    if(!(pe&WS_EX_NOACTIVATE)||!(pe&WS_EX_TOOLWINDOW)||(pe&(WS_EX_TRANSPARENT|WS_EX_TOPMOST)))return fail("presenter_exstyle",15);
    if(GetWindow(g_presenter,GW_OWNER)!=g_game)return fail("presenter_owner",16);
    if(!window_is(g_presenter,g_output)||!client_is(g_presenter,outW,outH))return fail("presenter_native_geometry",17);

    ID3D11Device*dev=nullptr;ID3D11DeviceContext*ctx=nullptr;D3D_FEATURE_LEVEL fl{};
    D3D_FEATURE_LEVEL req[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,req,ARRAYSIZE(req),D3D11_SDK_VERSION,&dev,&fl,&ctx)))return fail("warp_device",20);
    IDXGIDevice*dx=nullptr;IDXGIAdapter*ad=nullptr;IDXGIFactory*f=nullptr;
    if(FAILED(dev->QueryInterface(__uuidof(IDXGIDevice),(void**)&dx))||FAILED(dx->GetAdapter(&ad))||FAILED(ad->GetParent(__uuidof(IDXGIFactory),(void**)&f)))return fail("dxgi_factory",21);

    IDXGISwapChain*gs=make_swapchain(f,dev,g_game,g_gameW,g_gameH);
    IDXGISwapChain*os=make_swapchain(f,dev,g_presenter,outW,outH);
    if(!gs||!os)return fail("swapchains",22);
    if(FAILED(f->MakeWindowAssociation(g_game,DXGI_MWA_NO_WINDOW_CHANGES|DXGI_MWA_NO_ALT_ENTER)))return fail("window_association",23);
    BOOL fs=TRUE;if(FAILED(gs->GetFullscreenState(&fs,nullptr))||fs)return fail("game_fullscreen",24);
    if(FAILED(os->GetFullscreenState(&fs,nullptr))||fs)return fail("presenter_fullscreen",25);
    if(!verify_swap_size(gs,g_gameW,g_gameH)||!verify_swap_size(os,outW,outH))return fail("initial_swap_sizes",26);

    // Authority starts only after both window and DXGI sides are valid.
    g_takeover=true;

    std::vector<POINT> renders;
    auto add_render=[&](UINT w,UINT h){
        w=(std::max)(1u,(std::min)(w,outW));h=(std::max)(1u,(std::min)(h,outH));
        for(const POINT&p:renders)if(UINT(p.x)==w&&UINT(p.y)==h)return;
        renders.push_back(POINT{LONG(w),LONG(h)});
    };
    add_render(320,180);add_render(640,360);add_render(800,450);add_render(960,540);add_render(1024,576);add_render(1280,720);
    add_render(outW/2,outH/2);add_render((outW*3)/4,(outH*3)/4);add_render(outW,outH);

    std::mt19937 rng(0x50544152u);
    unsigned resizeFailures=0,geometryLeaks=0,mouseFailures=0,presentFailures=0,focusFailures=0;
    unsigned long long transitions=0,pointerProbes=0,hostileRequests=0,presenterGeometryChecks=0;

    for(unsigned i=0;i<20000;++i){
        POINT r=renders[rng()%renders.size()];
        if(!set_game_geometry(UINT(r.x),UINT(r.y)))return fail("set_game_geometry",30);
        HRESULT hr=gs->ResizeBuffers(0,g_gameW,g_gameH,DXGI_FORMAT_UNKNOWN,0);
        if(FAILED(hr))++resizeFailures;
        if(!client_is(g_game,g_gameW,g_gameH)||!verify_swap_size(gs,g_gameW,g_gameH))++geometryLeaks;

        // Simulate the engine trying to reassert native borderless geometry.
        SetWindowPos(g_game,nullptr,g_output.left+13,g_output.top+17,LONG(outW),LONG(outH),SWP_NOZORDER|SWP_NOACTIVATE);
        pump();++hostileRequests;
        RECT expectedGame{g_output.left,g_output.top,g_output.left+LONG(g_gameW),g_output.top+LONG(g_gameH)};
        if(!window_is(g_game,expectedGame)||!client_is(g_game,g_gameW,g_gameH)||!verify_swap_size(gs,g_gameW,g_gameH))++geometryLeaks;

        if((i%97)==0){
            if(!window_is(g_presenter,g_output)||!client_is(g_presenter,outW,outH)||!verify_swap_size(os,outW,outH))++geometryLeaks;
            ++presenterGeometryChecks;
        }

        Geometry geo=geometry();
        for(unsigned k=0;k<16;++k){
            POINT logical{LONG(rng()%g_gameW),LONG(rng()%g_gameH)};
            POINT physical=geo.logical_to_physical(logical);
            POINT pcpt=physical;ScreenToClient(g_presenter,&pcpt);
            g_lastLogicalX=g_lastLogicalY=-1;
            SendMessageW(g_presenter,WM_MOUSEMOVE,0,MAKELPARAM(pcpt.x,pcpt.y));
            if(g_lastLogicalX<0||g_lastLogicalY<0||
               std::abs(g_lastLogicalX-logical.x)>1||std::abs(g_lastLogicalY-logical.y)>1)++mouseFailures;
            ++pointerProbes;
        }

        if((i%211)==0){
            SetActiveWindow(g_decoy);SetFocus(g_decoy);
            LRESULT ma=SendMessageW(g_presenter,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(g_decoy),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
            if(ma!=MA_NOACTIVATE||GetActiveWindow()!=g_game||GetFocus()!=g_game)++focusFailures;
        }

        if((i%5)==0){
            HRESULT gh=gs->Present(0,DXGI_PRESENT_TEST);
            HRESULT oh=os->Present(0,DXGI_PRESENT_TEST);
            if(FAILED(gh)||FAILED(oh))++presentFailures;
        }
        ++transitions;
    }

    if(resizeFailures||geometryLeaks||mouseFailures||presentFailures||focusFailures){
        std::printf("FAIL resize=%u geometry=%u mouse=%u present=%u focus=%u transitions=%llu probes=%llu clamps=%llu\n",
            resizeFailures,geometryLeaks,mouseFailures,presentFailures,focusFailures,transitions,pointerProbes,g_windowClamps);
        return 40;
    }

    std::printf("PASS PTAR_BORDERLESS_TAKEOVER_LAB\n");
    std::printf("transitions=%llu pointer_probes=%llu hostile_native_reassertions=%llu presenter_geometry_checks=%llu window_clamps=%llu focus_returns=%llu\n",
        transitions,pointerProbes,hostileRequests,presenterGeometryChecks,g_windowClamps,g_focusReturns);
    std::printf("native_output=%ux%u resize_failures=0 geometry_leaks=0 mouse_mapping_failures=0 present_failures=0 focus_failures=0\n",outW,outH);
    std::printf("invariant=game_popup_client_equals_game_backbuffer; presenter_popup_client_equals_real_native_output; presenter_output_changes_do_not_resize_game\n");
    std::printf("dxgi=both_windowed+NO_WINDOW_CHANGES+NO_ALT_ENTER; presenter=owned+NOACTIVATE+TOOLWINDOW+NOT_TRANSPARENT+NOT_TOPMOST\n");

    os->Release();gs->Release();f->Release();ad->Release();dx->Release();ctx->ClearState();ctx->Release();dev->Release();
    DestroyWindow(g_presenter);DestroyWindow(g_decoy);DestroyWindow(g_game);
    return 0;
}
