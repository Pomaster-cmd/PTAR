#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
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
struct RegBackup{bool existed=false;DWORD value=0;};
static HBRUSH g_black=nullptr;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,g_black);EndPaint(h,&ps);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void pump(unsigned ms=2){DWORD s=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-s<ms);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void backup(RegBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static void restore(const RegBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
static bool query(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q&&q(&s)==0;}
static bool wait_mode(QueryFn q,bool b,unsigned timeout=5000){DWORD st=GetTickCount();do{ModeState s{};if(query(q,s)&&s.installed&&((s.borderlessActive!=0)==b))return true;pump(5);}while(GetTickCount()-st<timeout);return false;}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{0,0},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return true;}
static bool exact_child(HWND g,HWND p){LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);if(!(s&WS_CHILD)||(s&WS_POPUP)||GetParent(p)!=g)return false;RECT a{},b{};if(!client_screen(g,a)||!GetWindowRect(p,&b)||!EqualRect(&a,&b))return false;POINT pt{(a.left+a.right)/2,(a.top+a.bottom)/2};return WindowFromPoint(pt)==p;}
static bool exact_top(HWND g,HWND p,const RECT& mon){LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);if((s&WS_CHILD)||!(s&WS_POPUP))return false;RECT r{};if(!GetWindowRect(p,&r)||!EqualRect(&r,&mon))return false;return GetWindow(p,GW_OWNER)==g;}
static int fail(int rc,const char* what,QueryFn q,HWND g,HWND p,const RegBackup& b){ModeState s{};query(q,s);RECT gr{},pr{};GetWindowRect(g,&gr);GetWindowRect(p,&pr);std::printf("RC48_PREFCYCLE=FAIL rc=%d what=%s mode=%u pref=%ld gstyle=0x%llx pstyle=0x%llx parent=%p owner=%p grect=%ld,%ld,%ld,%ld prect=%ld,%ld,%ld,%ld\n",rc,what,s.borderlessActive,s.windowStylePreference,(unsigned long long)GetWindowLongPtrW(g,GWL_STYLE),(unsigned long long)GetWindowLongPtrW(p,GWL_STYLE),GetParent(p),GetWindow(p,GW_OWNER),gr.left,gr.top,gr.right,gr.bottom,pr.left,pr.top,pr.right,pr.bottom);restore(b);return rc;}
template<class T>static void rel(T*&p){if(p){p->Release();p=nullptr;}}

int wmain(){
    RegBackup rb{};backup(rb);if(!set_pref(0))return 10;
    g_black=CreateSolidBrush(RGB(0,0,0));HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_RC48_PREFCYCLE";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){restore(rb);return 11;}
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,32,32,nullptr,nullptr,inst,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi);DestroyWindow(probe);
    UINT ow=UINT(mi.rcMonitor.right-mi.rcMonitor.left),oh=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);UINT rw=std::min<UINT>(1280,std::max<UINT>(320,ow*2/3)),rh=std::min<UINT>(720,std::max<UINT>(180,oh*2/3));RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW|WS_VISIBLE,FALSE,0);
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"black-game",WS_OVERLAPPEDWINDOW|WS_VISIBLE,mi.rcMonitor.left+80,mi.rcMonitor.top+60,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);HWND p=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,ow,oh,game,nullptr,inst,nullptr);if(!game||!p)return fail(12,"create",nullptr,game,p,rb);if(GetWindow(p,GW_OWNER)!=game)return fail(13,"owned-field-precondition",nullptr,game,p,rb);

    ID3D11Device*d=nullptr;ID3D11DeviceContext*c=nullptr;IDXGISwapChain*sc=nullptr;ID3D11Texture2D*bb=nullptr;ID3D11RenderTargetView*rtv=nullptr;D3D_FEATURE_LEVEL fl{};DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=ow;sd.BufferDesc.Height=oh;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=p;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&sc,&d,&fl,&c)))return fail(14,"warp",nullptr,game,p,rb);if(FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||FAILED(d->CreateRenderTargetView(bb,nullptr,&rtv)))return fail(15,"rtv",nullptr,game,p,rb);
    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return fail(16,"load",nullptr,game,p,rb);auto attach=(AttachFn)GetProcAddress(dll,"PTAR_BorderlessAttachStable");auto q=(QueryFn)GetProcAddress(dll,"PTAR_BorderlessQueryMode");if(!attach||!q)return fail(17,"exports",q,game,p,rb);if(attach(game,p,rw,rh,ow,oh)!=0)return fail(18,"attach",q,game,p,rb);if(!wait_mode(q,false))return fail(19,"initial-windowed",q,game,p,rb);DWORD st=GetTickCount();while(!exact_child(game,p)&&GetTickCount()-st<5000)pump(10);if(!exact_child(game,p))return fail(20,"initial-child",q,game,p,rb);

    // Thousands of Windowed presents under black parent repaint/move pressure.
    constexpr unsigned kWindowedFrames=10000;
    for(unsigned i=0;i<kWindowedFrames;++i){if((i%17)==0){InvalidateRect(game,nullptr,TRUE);UpdateWindow(game);}if((i%23)==0)SetWindowPos(game,HWND_TOP,mi.rcMonitor.left+40+(int)(i%180),mi.rcMonitor.top+40+(int)(i%120),0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);float x[4]={float(i%101)/100.0f,0.35f,0.65f,1};c->ClearRenderTargetView(rtv,x);if(FAILED(sc->Present(0,0)))return fail(21,"windowed-present",q,game,p,rb);if(!exact_child(game,p))return fail(22,"windowed-child-drift",q,game,p,rb);}

    // Real production path only: registry write -> production 200 ms watcher ->
    // registered sync message -> game-thread transition. 200 pairs = 400 genuine
    // preference transitions, while the pure micro gate separately executes 20k.
    constexpr unsigned kPreferencePairs=200;
    for(unsigned i=0;i<kPreferencePairs;++i){
        if(!set_pref(1)||!wait_mode(q,true,5000))return fail(23,"pref-borderless-mode",q,game,p,rb);pump(30);if(!exact_top(game,p,mi.rcMonitor))return fail(24,"pref-borderless-top",q,game,p,rb);float a[4]={0.8f,0.1f,float(i%97)/96.0f,1};c->ClearRenderTargetView(rtv,a);if(FAILED(sc->Present(0,0)))return fail(25,"pref-borderless-present",q,game,p,rb);
        if(!set_pref(0)||!wait_mode(q,false,5000))return fail(26,"pref-windowed-mode",q,game,p,rb);pump(30);if(!exact_child(game,p))return fail(27,"pref-windowed-child",q,game,p,rb);float b[4]={0.1f,0.2f,float(i%89)/88.0f,1};c->ClearRenderTargetView(rtv,b);if(FAILED(sc->Present(0,0)))return fail(28,"pref-windowed-present",q,game,p,rb);
    }
    DXGI_SWAP_CHAIN_DESC got{};if(FAILED(sc->GetDesc(&got))||got.OutputWindow!=p)return fail(29,"output-window-drift",q,game,p,rb);ModeState s{};if(!query(q,s)||s.transitionsToWindowed<kPreferencePairs||s.transitionsToBorderless<kPreferencePairs)return fail(30,"transition-counters",q,game,p,rb);
    std::printf("RC48_PREFCYCLE=PASS windowed_frames=%u preference_pairs=%u real_preference_transitions=%u black_parent_repaint=PASS move_pressure=PASS child_windowed=PASS top_borderless=PASS output_hwnd_stable=PASS to_windowed=%llu to_borderless=%llu feature_level=0x%x\n",kWindowedFrames,kPreferencePairs,kPreferencePairs*2,s.transitionsToWindowed,s.transitionsToBorderless,(unsigned)fl);
    restore(rb);rel(rtv);rel(bb);rel(sc);rel(c);rel(d);FreeLibrary(dll);DestroyWindow(p);DestroyWindow(game);UnregisterClassW(wc.lpszClassName,inst);DeleteObject(g_black);return 0;
}
