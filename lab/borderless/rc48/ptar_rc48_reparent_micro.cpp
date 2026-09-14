#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <algorithm>

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
template<class T> static void rel(T*&p){if(p){p->Release();p=nullptr;}}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{0,0},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return true;}
static bool as_child(HWND game,HWND p){
    LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);s=(s&WS_VISIBLE)|WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN;SetWindowLongPtrW(p,GWL_STYLE,s);SetParent(p,game);
    RECT c{};GetClientRect(game,&c);if(!SetWindowPos(p,HWND_TOP,0,0,c.right,c.bottom,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW))return false;
    RECT gs{},pr{};client_screen(game,gs);GetWindowRect(p,&pr);return (GetWindowLongPtrW(p,GWL_STYLE)&WS_CHILD)&&GetParent(p)==game&&EqualRect(&gs,&pr);
}
static bool as_top(HWND game,HWND p,const RECT& mon,UINT outW,UINT outH){
    if(GetWindowLongPtrW(p,GWL_STYLE)&WS_CHILD)SetParent(p,nullptr);
    LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);s=(s&WS_VISIBLE)|WS_POPUP|WS_CLIPSIBLINGS;SetWindowLongPtrW(p,GWL_STYLE,s);SetWindowLongPtrW(p,GWLP_HWNDPARENT,(LONG_PTR)game);
    if(!SetWindowPos(p,HWND_NOTOPMOST,mon.left,mon.top,(int)outW,(int)outH,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW))return false;
    RECT r{};GetWindowRect(p,&r);return !(GetWindowLongPtrW(p,GWL_STYLE)&WS_CHILD)&&r.left==mon.left&&r.top==mon.top&&r.right-r.left==(LONG)outW&&r.bottom-r.top==(LONG)outH&&GetWindow(p,GW_OWNER)==game;
}
int wmain(){
    HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC48_REPARENT_MICRO";RegisterClassW(&wc);
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"p",WS_POPUP,0,0,32,32,nullptr,nullptr,inst,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi);DestroyWindow(probe);
    UINT ow=mi.rcMonitor.right-mi.rcMonitor.left,oh=mi.rcMonitor.bottom-mi.rcMonitor.top,rw=std::max<UINT>(320,ow*2/3),rh=std::max<UINT>(180,oh*2/3);rw=std::min(rw,ow);rh=std::min(rh,oh);
    RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRect(&wr,WS_OVERLAPPEDWINDOW,FALSE);HWND game=CreateWindowExW(0,wc.lpszClassName,L"game",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,60,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);HWND p=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,0,0,ow,oh,game,nullptr,inst,nullptr);if(!game||!p)return 10;
    ID3D11Device*d=nullptr;ID3D11DeviceContext*c=nullptr;IDXGISwapChain*sc=nullptr;ID3D11Texture2D*bb=nullptr;ID3D11RenderTargetView*rtv=nullptr;D3D_FEATURE_LEVEL fl{};DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=ow;sd.BufferDesc.Height=oh;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=p;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&sc,&d,&fl,&c)))return 11;if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||FAILED(d->CreateRenderTargetView(bb,nullptr,&rtv)))return 12;
    constexpr unsigned kPairs=10000;constexpr unsigned kPresentsPerEdge=2;
    for(unsigned i=0;i<kPairs;++i){
        if(!as_child(game,p)){RECT r{};GetWindowRect(p,&r);std::printf("RC48_REPARENT_MICRO=FAIL child i=%u style=%llx parent=%p rect=%ld,%ld,%ld,%ld\n",i,(unsigned long long)GetWindowLongPtrW(p,GWL_STYLE),GetParent(p),r.left,r.top,r.right,r.bottom);return 20;}
        for(unsigned j=0;j<kPresentsPerEdge;++j){float x[4]={0.1f,float(i%101)/100.0f,0.7f,1};c->ClearRenderTargetView(rtv,x);if(FAILED(sc->Present(0,0)))return 21;}
        if(!as_top(game,p,mi.rcMonitor,ow,oh)){RECT r{};GetWindowRect(p,&r);std::printf("RC48_REPARENT_MICRO=FAIL top i=%u style=%llx parent=%p owner=%p mon=%ld,%ld,%ld,%ld out=%ux%u rect=%ld,%ld,%ld,%ld gle=%lu\n",i,(unsigned long long)GetWindowLongPtrW(p,GWL_STYLE),GetParent(p),GetWindow(p,GW_OWNER),mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right,mi.rcMonitor.bottom,ow,oh,r.left,r.top,r.right,r.bottom,(unsigned long)GetLastError());return 22;}
        for(unsigned j=0;j<kPresentsPerEdge;++j){float x[4]={0.8f,0.2f,float(i%97)/96.0f,1};c->ClearRenderTargetView(rtv,x);if(FAILED(sc->Present(0,0)))return 23;}
    }
    DXGI_SWAP_CHAIN_DESC got{};sc->GetDesc(&got);if(got.OutputWindow!=p)return 24;
    std::printf("RC48_REPARENT_MICRO=PASS pairs=%u transitions=%u presents=%u output=%ux%u render=%ux%u output_hwnd_stable=PASS feature_level=0x%x\n",kPairs,kPairs*2,kPairs*2*kPresentsPerEdge,ow,oh,rw,rh,(unsigned)fl);
    rel(rtv);rel(bb);rel(sc);rel(c);rel(d);DestroyWindow(p);DestroyWindow(game);UnregisterClassW(wc.lpszClassName,inst);return 0;
}
