#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <thread>
#include <atomic>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")

static const UINT WM_PTAR_CREATE_PRESENTER = WM_APP + 0x90;
static const UINT WM_PTAR_ROUTED_MOUSE = WM_APP + 0x91;
static HWND g_game=nullptr;
static HWND g_presenter=nullptr;
static HINSTANCE g_hi=nullptr;
static DWORD g_gameTid=0;
static DWORD g_presenterTid=0;
static LONG g_routedMouse=0;
static LONG g_lastMouseX=-1,g_lastMouseY=-1;

static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_MOUSEACTIVATE:
        SetActiveWindow(g_game);
        SetFocus(g_game);
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        SendMessageW(g_game,WM_PTAR_ROUTED_MOUSE,w,l);
        return 0;
    default:
        return DefWindowProcW(h,m,w,l);
    }
}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_PTAR_CREATE_PRESENTER:
        if(!g_presenter){
            g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
                L"PTARAffinityPresenter",L"presenter",WS_POPUP,
                0,0,1920,1080,h,nullptr,g_hi,nullptr);
            if(g_presenter){
                g_presenterTid=GetCurrentThreadId();
                ShowWindow(g_presenter,SW_SHOWNOACTIVATE);
            }
        }
        return reinterpret_cast<LRESULT>(g_presenter);
    case WM_PTAR_ROUTED_MOUSE:
        ++g_routedMouse;
        g_lastMouseX=GET_X_LPARAM(l);
        g_lastMouseY=GET_Y_LPARAM(l);
        return 0;
    default:
        return DefWindowProcW(h,m,w,l);
    }
}

static IDXGISwapChain* make_sc(IDXGIFactory*f,ID3D11Device*d,HWND h,UINT w,UINT hh){
    DXGI_SWAP_CHAIN_DESC x{};
    x.BufferDesc.Width=w;x.BufferDesc.Height=hh;x.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    x.SampleDesc.Count=1;x.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;x.BufferCount=1;
    x.OutputWindow=h;x.Windowed=TRUE;x.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain*s=nullptr;
    return SUCCEEDED(f->CreateSwapChain(d,&x,&s))?s:nullptr;
}

struct WorkerResult{
    HRESULT deviceHr=E_FAIL;
    HRESULT associationHr=E_FAIL;
    unsigned gamePresentOk=0;
    unsigned presenterPresentOk=0;
    unsigned errors=0;
    DWORD tid=0;
};

static void d3d_worker(WorkerResult* r){
    r->tid=GetCurrentThreadId();
    ID3D11Device*d=nullptr;ID3D11DeviceContext*c=nullptr;D3D_FEATURE_LEVEL fl{};
    D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
    r->deviceHr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,&d,&fl,&c);
    if(FAILED(r->deviceHr)){++r->errors;return;}
    IDXGIDevice*xd=nullptr;IDXGIAdapter*ad=nullptr;IDXGIFactory*f=nullptr;
    if(FAILED(d->QueryInterface(__uuidof(IDXGIDevice),(void**)&xd))||FAILED(xd->GetAdapter(&ad))||FAILED(ad->GetParent(__uuidof(IDXGIFactory),(void**)&f))){++r->errors;goto cleanup;}
    {
        IDXGISwapChain*gs=make_sc(f,d,g_game,1280,720);
        IDXGISwapChain*ps=make_sc(f,d,g_presenter,1920,1080);
        if(!gs||!ps){++r->errors;if(ps)ps->Release();if(gs)gs->Release();goto cleanup_factory;}
        r->associationHr=f->MakeWindowAssociation(g_game,DXGI_MWA_NO_WINDOW_CHANGES|DXGI_MWA_NO_ALT_ENTER);
        if(FAILED(r->associationHr))++r->errors;
        BOOL fs=TRUE;
        if(FAILED(gs->GetFullscreenState(&fs,nullptr))||fs)++r->errors;
        if(FAILED(ps->GetFullscreenState(&fs,nullptr))||fs)++r->errors;
        for(unsigned i=0;i<2000;++i){
            HRESULT gh=gs->Present(0,DXGI_PRESENT_TEST);
            HRESULT ph=ps->Present(0,DXGI_PRESENT_TEST);
            if(SUCCEEDED(gh))++r->gamePresentOk;else ++r->errors;
            if(SUCCEEDED(ph))++r->presenterPresentOk;else ++r->errors;
        }
        ps->Release();gs->Release();
    }
cleanup_factory:
    f->Release();ad->Release();xd->Release();
cleanup:
    c->ClearState();c->Release();d->Release();
}

int main(){
    g_hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=g_hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"PTARAffinityGame";
    WNDCLASSW pc{};pc.hInstance=g_hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARAffinityPresenter";pc.style=CS_DBLCLKS;
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;
    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,0,0,1280,720,nullptr,nullptr,g_hi,nullptr);
    if(!g_game)return 11;
    g_gameTid=GetCurrentThreadId();
    ShowWindow(g_game,SW_SHOW);

    // Production analogue: marshal presenter HWND creation through the game WndProc,
    // guaranteeing identical Win32 thread ownership even if rendering runs elsewhere.
    if(!SendMessageW(g_game,WM_PTAR_CREATE_PRESENTER,0,0)||!g_presenter)return 12;
    DWORD gamePid=0,presPid=0;
    DWORD gameTid=GetWindowThreadProcessId(g_game,&gamePid);
    DWORD presTid=GetWindowThreadProcessId(g_presenter,&presPid);
    if(gameTid!=g_gameTid||presTid!=g_presenterTid||gameTid!=presTid||gamePid!=presPid)return 13;
    if(GetWindow(g_presenter,GW_OWNER)!=g_game)return 14;

    SendMessageW(g_presenter,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(321,123));
    if(g_routedMouse!=1||g_lastMouseX!=321||g_lastMouseY!=123)return 15;
    SetActiveWindow(g_game);SetFocus(g_game);
    if(SendMessageW(g_presenter,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(g_game),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN))!=MA_NOACTIVATE)return 16;
    if(GetActiveWindow()!=g_game||GetFocus()!=g_game)return 17;

    WorkerResult wr{};
    std::thread worker(d3d_worker,&wr);
    worker.join();
    if(wr.tid==g_gameTid)return 18;
    if(wr.errors||FAILED(wr.deviceHr)||FAILED(wr.associationHr)||wr.gamePresentOk!=2000||wr.presenterPresentOk!=2000)return 19;

    std::printf("PASS PTAR_BORDERLESS_THREAD_AFFINITY\n");
    std::printf("game_hwnd_tid=%lu presenter_hwnd_tid=%lu d3d_worker_tid=%lu same_ui_thread=YES\n",(unsigned long)gameTid,(unsigned long)presTid,(unsigned long)wr.tid);
    std::printf("routed_mouse=%ld game_present_tests=%u presenter_present_tests=%u\n",g_routedMouse,wr.gamePresentOk,wr.presenterPresentOk);
    std::printf("contract=presenter_HWND_created_on_game_UI_thread; D3D_device_and_swapchains_may_be_created_on_worker_thread\n");
    return 0;
}
