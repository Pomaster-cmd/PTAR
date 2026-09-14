#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <algorithm>

using AttachFn=int (WINAPI*)(HWND,HWND,UINT,UINT,UINT,UINT);
using QueryFn=int (WINAPI*)(void*);

struct ModeState {
    UINT size,installed,borderlessActive,presenterVisible;
    ULONG_PTR gameStyle,presenterStyle;
    unsigned long long transitionsToWindowed,transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom,windowStylePreference;
    unsigned long long presenterFollows,gameCoercionClamps,presenterClamps,rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
};

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static const wchar_t* kSync=L"PTAR_RC46_WINDOWSTYLE_SYNC_20260914";
struct RegBackup{bool existed=false;DWORD value=0;};
static HBRUSH g_black=nullptr;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,g_black);EndPaint(h,&ps);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void pump(unsigned ms=2){DWORD s=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(0);}while(GetTickCount()-s<ms);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void backup(RegBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static void restore(const RegBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
static bool query(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q&&q(&s)==0;}
static bool wait_mode(QueryFn q,bool b,unsigned timeout=5000){DWORD st=GetTickCount();do{ModeState s{};if(query(q,s)&&s.installed&&((s.borderlessActive!=0)==b))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{0,0},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return b.x>a.x&&b.y>a.y;}
static bool exact_child(HWND g,HWND p){
    if(GetParent(p)!=g)return false;LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);if(!(s&WS_CHILD)||(s&WS_POPUP))return false;
    RECT gc{},pr{};if(!client_screen(g,gc)||!GetWindowRect(p,&pr)||!EqualRect(&gc,&pr))return false;
    POINT pt{(gc.left+gc.right)/2,(gc.top+gc.bottom)/2};return WindowFromPoint(pt)==p;
}
static bool exact_top(HWND g,HWND p,const RECT& mon){
    LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);if((s&WS_CHILD)||!(s&WS_POPUP))return false;
    RECT r{};if(!GetWindowRect(p,&r))return false;
    if(r.left!=mon.left||r.top!=mon.top||r.right!=mon.right||r.bottom!=mon.bottom)return false;
    return GetWindow(p,GW_OWNER)==g;
}
static unsigned wait_exact_top(HWND g,HWND p,const RECT& mon,unsigned timeout){DWORD st=GetTickCount();do{if(exact_top(g,p,mon))return GetTickCount()-st;pump(1);}while(GetTickCount()-st<timeout);return 0xffffffffu;}
static unsigned wait_exact_child(HWND g,HWND p,unsigned timeout){DWORD st=GetTickCount();do{if(exact_child(g,p))return GetTickCount()-st;pump(1);}while(GetTickCount()-st<timeout);return 0xffffffffu;}
static void dump_runtime_log(){
    FILE* f=nullptr;if(fopen_s(&f,"ptar_borderless_rc38.log","rb")||!f){std::puts("RC48_RUNTIME_LOG=ABSENT");return;}
    fseek(f,0,SEEK_END);long n=ftell(f);long start=n>32768?n-32768:0;fseek(f,start,SEEK_SET);
    std::puts("----- PTAR RUNTIME LOG TAIL -----");char buf[2049]{};size_t got=0;while((got=fread(buf,1,2048,f))>0){buf[got]=0;std::fputs(buf,stdout);}std::puts("\n----- END PTAR RUNTIME LOG TAIL -----");fclose(f);
}
static int fail(int rc,const char* what,QueryFn q,HWND g,HWND p,const RegBackup& b){ModeState s{};query(q,s);RECT gr{},pr{},gc{};GetWindowRect(g,&gr);GetWindowRect(p,&pr);client_screen(g,gc);std::printf("RC48_CHILD_D3D=FAIL rc=%d what=%s mode=%u gstyle=0x%llx pstyle=0x%llx parent=%p owner=%p grect=%ld,%ld,%ld,%ld gclient=%ld,%ld,%ld,%ld prect=%ld,%ld,%ld,%ld target=%ld,%ld,%ld,%ld gle=%lu\n",rc,what,s.borderlessActive,(unsigned long long)GetWindowLongPtrW(g,GWL_STYLE),(unsigned long long)GetWindowLongPtrW(p,GWL_STYLE),GetParent(p),GetWindow(p,GW_OWNER),gr.left,gr.top,gr.right,gr.bottom,gc.left,gc.top,gc.right,gc.bottom,pr.left,pr.top,pr.right,pr.bottom,s.targetLeft,s.targetTop,s.targetRight,s.targetBottom,(unsigned long)GetLastError());dump_runtime_log();restore(b);return rc;}
template<class T>static void rel(T*&p){if(p){p->Release();p=nullptr;}}

int wmain(){
    DeleteFileW(L"ptar_borderless_rc38.log");
    RegBackup rb{};backup(rb);if(!set_pref(0)){std::puts("RC48_CHILD_D3D=FAIL registry");return 10;}
    g_black=CreateSolidBrush(RGB(0,0,0));HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_RC48_CHILD_D3D";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){restore(rb);return 11;}
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,64,64,nullptr,nullptr,inst,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi);DestroyWindow(probe);
    const UINT outW=UINT(mi.rcMonitor.right-mi.rcMonitor.left),outH=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);
    const UINT rw=std::min<UINT>(1280,std::max<UINT>(320,outW*2/3)),rh=std::min<UINT>(720,std::max<UINT>(180,outH*2/3));
    RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW|WS_VISIBLE,FALSE,0);
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game-black-parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,mi.rcMonitor.left+80,mi.rcMonitor.top+60,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter-native",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,game,nullptr,inst,nullptr);
    if(!game||!presenter)return fail(12,"create",nullptr,game,presenter,rb);
    if(GetWindow(presenter,GW_OWNER)!=game)return fail(13,"field-owner-precondition",nullptr,game,presenter,rb);

    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rtv=nullptr;D3D_FEATURE_LEVEL fl{};
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=outW;sd.BufferDesc.Height=outH;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=presenter;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);if(FAILED(hr))return fail(14,"warp-create",nullptr,game,presenter,rb);
    if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv)))return fail(15,"rtv",nullptr,game,presenter,rb);
    float c0[4]={0.0f,0.75f,0.25f,1.0f};ctx->ClearRenderTargetView(rtv,c0);hr=swap->Present(0,0);if(FAILED(hr))return fail(16,"pre-present",nullptr,game,presenter,rb);

    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return fail(17,"load",nullptr,game,presenter,rb);auto attach=(AttachFn)GetProcAddress(dll,"PTAR_BorderlessAttachStable");auto q=(QueryFn)GetProcAddress(dll,"PTAR_BorderlessQueryMode");if(!attach||!q)return fail(18,"exports",q,game,presenter,rb);
    if(attach(game,presenter,rw,rh,outW,outH)!=0)return fail(19,"attach",q,game,presenter,rb);if(!wait_mode(q,false))return fail(20,"windowed-mode",q,game,presenter,rb);
    if(wait_exact_child(game,presenter,5000)==0xffffffffu)return fail(21,"child-not-established",q,game,presenter,rb);

    constexpr unsigned kWindowedFrames=20000;
    for(unsigned i=0;i<kWindowedFrames;++i){
        if((i%7)==0){
            const int maxX=std::max<int>(1,(int)outW-(int)rw-80),maxY=std::max<int>(1,(int)outH-(int)rh-100);
            const int x=mi.rcMonitor.left+20+(int)((i*17u)%unsigned(maxX));const int y=mi.rcMonitor.top+20+(int)((i*11u)%unsigned(maxY));
            SetWindowPos(game,HWND_TOP,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        } else SetWindowPos(game,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if((i%13)==0){InvalidateRect(game,nullptr,TRUE);UpdateWindow(game);}
        float col[4]={float((i%97u))/96.0f,0.2f,float((i%53u))/52.0f,1.0f};ctx->ClearRenderTargetView(rtv,col);hr=swap->Present(0,0);if(FAILED(hr))return fail(22,"windowed-present",q,game,presenter,rb);
        if(!exact_child(game,presenter))return fail(23,"windowed-invariant",q,game,presenter,rb);
    }

    const UINT syncMsg=RegisterWindowMessageW(kSync);if(!syncMsg)return fail(24,"sync-register",q,game,presenter,rb);
    constexpr unsigned kModePairs=2500;unsigned maxTopMs=0,maxChildMs=0;
    for(unsigned i=0;i<kModePairs;++i){
        SendMessageW(game,syncMsg,1,0);if(!wait_mode(q,true,1000))return fail(25,"sync-borderless-mode",q,game,presenter,rb);
        unsigned topMs=wait_exact_top(game,presenter,mi.rcMonitor,250);if(topMs==0xffffffffu)return fail(26,"sync-borderless-top",q,game,presenter,rb);maxTopMs=std::max(maxTopMs,topMs);
        float a[4]={0.8f,0.1f,0.1f,1.0f};ctx->ClearRenderTargetView(rtv,a);hr=swap->Present(0,0);if(FAILED(hr))return fail(27,"borderless-present",q,game,presenter,rb);
        SendMessageW(game,syncMsg,0,0);if(!wait_mode(q,false,1000))return fail(28,"sync-windowed-mode",q,game,presenter,rb);
        unsigned childMs=wait_exact_child(game,presenter,250);if(childMs==0xffffffffu)return fail(29,"sync-windowed-child",q,game,presenter,rb);maxChildMs=std::max(maxChildMs,childMs);
        float b[4]={0.1f,0.2f,0.8f,1.0f};ctx->ClearRenderTargetView(rtv,b);hr=swap->Present(0,0);if(FAILED(hr))return fail(30,"windowed-represent",q,game,presenter,rb);
    }
    std::printf("RC48_CONVERGENCE mode_pairs=%u max_borderless_ms=%u max_windowed_ms=%u\n",kModePairs,maxTopMs,maxChildMs);

    if(!set_pref(1)||!wait_mode(q,true,6000))return fail(31,"pref-borderless",q,game,presenter,rb);unsigned prefTop=wait_exact_top(game,presenter,mi.rcMonitor,500);if(prefTop==0xffffffffu)return fail(32,"pref-borderless-top",q,game,presenter,rb);
    if(!set_pref(0)||!wait_mode(q,false,6000))return fail(33,"pref-windowed",q,game,presenter,rb);unsigned prefChild=wait_exact_child(game,presenter,500);if(prefChild==0xffffffffu)return fail(34,"pref-windowed-child",q,game,presenter,rb);
    std::printf("RC48_PREF_CONVERGENCE borderless_ms=%u windowed_ms=%u\n",prefTop,prefChild);

    DXGI_SWAP_CHAIN_DESC got{};if(FAILED(swap->GetDesc(&got))||got.OutputWindow!=presenter)return fail(35,"swap-output-window-drift",q,game,presenter,rb);
    std::printf("RC48_CHILD_D3D=PASS field_owned_popup=PASS child_composition=PASS black_parent_repaint=PASS d3d_warp_present=%u mode_pairs=%u mode_transitions=%u child_hit_test=PASS swap_output_stable=PASS pref_roundtrip=PASS max_borderless_ms=%u max_windowed_ms=%u feature_level=0x%x\n",kWindowedFrames,kModePairs,kModePairs*2u,maxTopMs,maxChildMs,(unsigned)fl);

    restore(rb);rel(rtv);rel(bb);rel(swap);rel(ctx);rel(dev);FreeLibrary(dll);DestroyWindow(presenter);DestroyWindow(game);UnregisterClassW(wc.lpszClassName,inst);DeleteObject(g_black);return 0;
}
