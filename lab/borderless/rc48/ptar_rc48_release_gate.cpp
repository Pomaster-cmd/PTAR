#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <algorithm>

using AttachFn=int (WINAPI*)(HWND,HWND,UINT,UINT,UINT,UINT);
using QueryFn=int (WINAPI*)(void*);

struct ModeState{
    UINT size,installed,borderlessActive,presenterVisible;
    ULONG_PTR gameStyle,presenterStyle;
    unsigned long long transitionsToWindowed,transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom,windowStylePreference;
    unsigned long long presenterFollows,gameCoercionClamps,presenterClamps,rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
};
struct RegBackup{bool existed=false;DWORD value=0;};

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static const wchar_t* kSync=L"PTAR_RC46_WINDOWSTYLE_SYNC_20260914";
static HBRUSH g_black=nullptr;
static unsigned long long g_dispatched=0;

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,g_black);EndPaint(h,&ps);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static bool pump_one(){
    MSG m{};
    if(!PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){Sleep(0);return false;}
    TranslateMessage(&m);DispatchMessageW(&m);++g_dispatched;return true;
}
static unsigned service_queue(unsigned maxMessages){unsigned n=0;while(n<maxMessages&&pump_one())++n;return n;}
static bool set_pref(DWORD v){
    HKEY k=nullptr;DWORD d=0;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;
    const LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&v),sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;
}
static void backup_pref(RegBackup& b){
    HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;
    DWORD t=0,n=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,reinterpret_cast<BYTE*>(&b.value),&n)==ERROR_SUCCESS&&t==REG_DWORD&&n==sizeof(b.value))b.existed=true;RegCloseKey(k);
}
static void restore_pref(const RegBackup& b){
    HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;
    if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&b.value),sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);
}
static bool query(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q&&q(&s)==0;}
static bool client_screen(HWND h,RECT& r){
    RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};
    if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b)||b.x<=a.x||b.y<=a.y)return false;r={a.x,a.y,b.x,b.y};return true;
}
static bool exact_child(HWND g,HWND p){
    if(GetParent(p)!=g)return false;const LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);if(!(s&WS_CHILD)||(s&WS_POPUP)||!IsWindowVisible(p))return false;
    RECT gc{},pr{},cc{};if(!client_screen(g,gc)||!GetWindowRect(p,&pr)||!EqualRect(&gc,&pr)||!GetClientRect(g,&cc))return false;
    POINT pt{(cc.right-cc.left)/2,(cc.bottom-cc.top)/2};return ChildWindowFromPointEx(g,pt,CWP_ALL)==p;
}
static bool exact_top(HWND g,HWND p,const RECT& mon){
    const LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);if((s&WS_CHILD)||!(s&WS_POPUP)||!IsWindowVisible(p))return false;
    RECT r{};return GetWindowRect(p,&r)&&EqualRect(&r,&mon)&&GetWindow(p,GW_OWNER)==g;
}
static bool wait_state(QueryFn q,HWND g,HWND p,bool borderless,const RECT& mon,unsigned timeout,unsigned& elapsed){
    const DWORD st=GetTickCount();
    for(;;){
        ModeState s{};
        if(query(q,s)&&s.installed&&((s.borderlessActive!=0)==borderless)&&(borderless?exact_top(g,p,mon):exact_child(g,p))){elapsed=GetTickCount()-st;return true;}
        if(GetTickCount()-st>=timeout)break;
        pump_one();
    }
    elapsed=GetTickCount()-st;ModeState s{};return query(q,s)&&s.installed&&((s.borderlessActive!=0)==borderless)&&(borderless?exact_top(g,p,mon):exact_child(g,p));
}
static void dump_log(){
    FILE* f=nullptr;if(fopen_s(&f,"ptar_borderless_rc38.log","rb")||!f)return;fseek(f,0,SEEK_END);long n=ftell(f);const long start=n>16384?n-16384:0;fseek(f,start,SEEK_SET);
    std::puts("----- RC48 LOG TAIL -----");char buf[2049]{};size_t got=0;while((got=fread(buf,1,2048,f))>0){buf[got]=0;std::fputs(buf,stdout);}std::puts("\n----- END RC48 LOG TAIL -----");fclose(f);
}
static int fail(int rc,const char* tag,QueryFn q,HWND g,HWND p,const RegBackup& b){
    ModeState s{};query(q,s);RECT gr{},pr{},gc{};if(g){GetWindowRect(g,&gr);client_screen(g,gc);}if(p)GetWindowRect(p,&pr);
    std::printf("RC48_RELEASE_GATE=FAIL rc=%d tag=%s installed=%u mode=%u pref=%ld dispatched=%llu gstyle=0x%llx pstyle=0x%llx parent=%p owner=%p grect=%ld,%ld,%ld,%ld gclient=%ld,%ld,%ld,%ld prect=%ld,%ld,%ld,%ld\n",rc,tag,s.installed,s.borderlessActive,s.windowStylePreference,g_dispatched,(unsigned long long)(g?GetWindowLongPtrW(g,GWL_STYLE):0),(unsigned long long)(p?GetWindowLongPtrW(p,GWL_STYLE):0),p?GetParent(p):nullptr,p?GetWindow(p,GW_OWNER):nullptr,gr.left,gr.top,gr.right,gr.bottom,gc.left,gc.top,gc.right,gc.bottom,pr.left,pr.top,pr.right,pr.bottom);dump_log();restore_pref(b);return rc;
}
template<class T>static void rel(T*& p){if(p){p->Release();p=nullptr;}}

int wmain(){
    DeleteFileW(L"ptar_borderless_rc38.log");
    RegBackup rb{};backup_pref(rb);if(!set_pref(0))return fail(10,"initial-pref",nullptr,nullptr,nullptr,rb);
    g_black=CreateSolidBrush(RGB(0,0,0));HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_RC48_RELEASE_GATE";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){restore_pref(rb);return 11;}
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,32,32,nullptr,nullptr,inst,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi)){DestroyWindow(probe);restore_pref(rb);return 12;}DestroyWindow(probe);
    const UINT outW=UINT(mi.rcMonitor.right-mi.rcMonitor.left),outH=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);
    const UINT rw=std::min<UINT>(1280,std::max<UINT>(320,outW*2/3)),rh=std::min<UINT>(720,std::max<UINT>(180,outH*2/3));
    RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW|WS_VISIBLE,FALSE,0);
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game-black-parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,mi.rcMonitor.left+80,mi.rcMonitor.top+60,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter-native",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,game,nullptr,inst,nullptr);
    if(!game||!presenter)return fail(13,"windows",nullptr,game,presenter,rb);if(GetWindow(presenter,GW_OWNER)!=game)return fail(14,"owned-popup-precondition",nullptr,game,presenter,rb);

    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rtv=nullptr;D3D_FEATURE_LEVEL fl{};
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=outW;sd.BufferDesc.Height=outH;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=presenter;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);if(FAILED(hr))return fail(15,"warp",nullptr,game,presenter,rb);
    if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&bb)))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv)))return fail(16,"rtv",nullptr,game,presenter,rb);

    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return fail(17,"load",nullptr,game,presenter,rb);
    auto attach=reinterpret_cast<AttachFn>(GetProcAddress(dll,"PTAR_BorderlessAttachStable"));auto q=reinterpret_cast<QueryFn>(GetProcAddress(dll,"PTAR_BorderlessQueryMode"));if(!attach||!q)return fail(18,"exports",q,game,presenter,rb);
    if(attach(game,presenter,rw,rh,outW,outH)!=0)return fail(19,"attach",q,game,presenter,rb);
    unsigned elapsed=0;if(!wait_state(q,game,presenter,false,mi.rcMonitor,5000,elapsed))return fail(20,"initial-windowed",q,game,presenter,rb);

    constexpr unsigned kWindowedFrames=20000;unsigned long long queueServiceWindowed=0;
    for(unsigned i=0;i<kWindowedFrames;++i){
        if((i%7u)==0){const int maxX=std::max<int>(1,(int)outW-(int)rw-80),maxY=std::max<int>(1,(int)outH-(int)rh-100);const int x=mi.rcMonitor.left+20+(int)((i*17u)%unsigned(maxX));const int y=mi.rcMonitor.top+20+(int)((i*11u)%unsigned(maxY));SetWindowPos(game,HWND_TOP,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);queueServiceWindowed+=service_queue(12);}else SetWindowPos(game,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if((i%13u)==0){InvalidateRect(game,nullptr,TRUE);UpdateWindow(game);queueServiceWindowed+=service_queue(8);}
        float col[4]={float(i%97u)/96.0f,0.2f,float(i%53u)/52.0f,1.0f};ctx->ClearRenderTargetView(rtv,col);hr=swap->Present(0,0);if(FAILED(hr))return fail(21,"windowed-present",q,game,presenter,rb);
        if(!exact_child(game,presenter))return fail(22,"windowed-child-invariant",q,game,presenter,rb);
    }
    queueServiceWindowed+=service_queue(256);

    const UINT sync=RegisterWindowMessageW(kSync);if(!sync)return fail(23,"sync",q,game,presenter,rb);
    constexpr unsigned kPairs=2500;unsigned maxTop=0,maxChild=0;unsigned long long queueServiceModes=0;
    for(unsigned i=0;i<kPairs;++i){
        SendMessageW(game,sync,1,0);if(!wait_state(q,game,presenter,true,mi.rcMonitor,1000,elapsed))return fail(24,"direct-borderless",q,game,presenter,rb);maxTop=std::max(maxTop,elapsed);
        float a[4]={0.8f,0.1f,0.1f,1};ctx->ClearRenderTargetView(rtv,a);if(FAILED(swap->Present(0,0)))return fail(25,"borderless-present",q,game,presenter,rb);
        SendMessageW(game,sync,0,0);if(!wait_state(q,game,presenter,false,mi.rcMonitor,1000,elapsed))return fail(26,"direct-windowed",q,game,presenter,rb);maxChild=std::max(maxChild,elapsed);
        float v[4]={0.1f,0.2f,0.8f,1};ctx->ClearRenderTargetView(rtv,v);if(FAILED(swap->Present(0,0)))return fail(27,"windowed-represent",q,game,presenter,rb);
        queueServiceModes+=service_queue(16);
    }
    queueServiceModes+=service_queue(256);

    const DWORD regStartB=GetTickCount();if(!set_pref(1)||!wait_state(q,game,presenter,true,mi.rcMonitor,3000,elapsed))return fail(28,"registry-borderless",q,game,presenter,rb);const unsigned regB=GetTickCount()-regStartB;service_queue(32);
    const DWORD regStartW=GetTickCount();if(!set_pref(0)||!wait_state(q,game,presenter,false,mi.rcMonitor,3000,elapsed))return fail(29,"registry-windowed",q,game,presenter,rb);const unsigned regW=GetTickCount()-regStartW;service_queue(32);

    DXGI_SWAP_CHAIN_DESC got{};if(FAILED(swap->GetDesc(&got))||got.OutputWindow!=presenter)return fail(30,"swap-output-window",q,game,presenter,rb);
    ModeState final{};if(!query(q,final)||!final.installed||final.borderlessActive||final.windowStylePreference!=0)return fail(31,"final-state",q,game,presenter,rb);
    std::printf("RC48_RELEASE_GATE=PASS d3d_windowed_frames=%u direct_pairs=%u direct_transitions=%u max_direct_borderless_ms=%u max_direct_windowed_ms=%u registry_borderless_ms=%u registry_windowed_ms=%u queue_windowed=%llu queue_modes=%llu dispatched=%llu child_composition=PASS black_parent_repaint=PASS swap_output_stable=PASS final_windowed=PASS feature_level=0x%x\n",kWindowedFrames,kPairs,kPairs*2u,maxTop,maxChild,regB,regW,queueServiceWindowed,queueServiceModes,g_dispatched,(unsigned)fl);

    restore_pref(rb);rel(rtv);rel(bb);rel(swap);rel(ctx);rel(dev);FreeLibrary(dll);DestroyWindow(presenter);DestroyWindow(game);DeleteObject(g_black);return 0;
}
