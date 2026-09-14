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
struct Backup{bool existed=false;DWORD value=0;};
static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static HBRUSH g_black=nullptr;
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_PAINT){PAINTSTRUCT ps{};HDC d=BeginPaint(h,&ps);FillRect(d,&ps.rcPaint,g_black);EndPaint(h,&ps);return 0;}return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms=2){DWORD s=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(0);}while(GetTickCount()-s<ms);}
static bool write_pref(DWORD v,LONG* outRc=nullptr){HKEY k=nullptr;DWORD d=0;LONG r=RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d);if(r!=ERROR_SUCCESS){if(outRc)*outRc=r;return false;}r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&v,sizeof(v));RegCloseKey(k);if(outRc)*outRc=r;return r==ERROR_SUCCESS;}
static int read_pref(){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return -1;DWORD t=0,v=0,n=sizeof(v);LONG r=RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&v,&n);RegCloseKey(k);return r==ERROR_SUCCESS&&t==REG_DWORD&&n==sizeof(v)?(int)v:-1;}
static void backup(Backup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,n=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&n)==ERROR_SUCCESS&&t==REG_DWORD&&n==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static void restore(const Backup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
static bool query(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q&&q(&s)==0;}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{0,0},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return true;}
static bool geom(HWND g,HWND p,bool borderless,const RECT& mon){LONG_PTR s=GetWindowLongPtrW(p,GWL_STYLE);if(borderless){if((s&WS_CHILD)||!(s&WS_POPUP))return false;RECT r{};return GetWindowRect(p,&r)&&EqualRect(&r,&mon)&&GetWindow(p,GW_OWNER)==g;}if(!(s&WS_CHILD)||(s&WS_POPUP)||GetParent(p)!=g)return false;RECT a{},b{};return client_screen(g,a)&&GetWindowRect(p,&b)&&EqualRect(&a,&b)&&IsWindowVisible(p);}
static bool wait_state(QueryFn q,HWND g,HWND p,bool borderless,const RECT& mon,unsigned timeout,unsigned& elapsed,ModeState& last){DWORD st=GetTickCount();do{if(query(q,last)&&last.installed&&((last.borderlessActive!=0)==borderless)&&last.windowStylePreference==(borderless?1:0)&&geom(g,p,borderless,mon)){elapsed=GetTickCount()-st;return true;}pump(2);}while(GetTickCount()-st<timeout);elapsed=GetTickCount()-st;query(q,last);return false;}
template<class T>static void rel(T*&p){if(p){p->Release();p=nullptr;}}
static int fail(int rc,const char* tag,QueryFn q,HWND g,HWND p,const Backup& b,LONG regRc=0){ModeState s{};query(q,s);RECT gr{},pr{};GetWindowRect(g,&gr);GetWindowRect(p,&pr);std::printf("RC48_PREF_STRESS=FAIL rc=%d tag=%s reg_rc=%ld registry=%d installed=%u mode=%u query_pref=%ld gstyle=0x%llx pstyle=0x%llx pex=0x%llx parent=%p owner=%p grect=%ld,%ld,%ld,%ld prect=%ld,%ld,%ld,%ld\n",rc,tag,regRc,read_pref(),s.installed,s.borderlessActive,s.windowStylePreference,(unsigned long long)GetWindowLongPtrW(g,GWL_STYLE),(unsigned long long)GetWindowLongPtrW(p,GWL_STYLE),(unsigned long long)GetWindowLongPtrW(p,GWL_EXSTYLE),GetParent(p),GetWindow(p,GW_OWNER),gr.left,gr.top,gr.right,gr.bottom,pr.left,pr.top,pr.right,pr.bottom);restore(b);return rc;}
int wmain(){
    Backup b{};backup(b);LONG rr=0;if(!write_pref(0,&rr))return fail(10,"initial-pref",nullptr,nullptr,nullptr,b,rr);
    g_black=CreateSolidBrush(RGB(0,0,0));HINSTANCE hi=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=hi;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_RC48_PREF_STRESS";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return fail(11,"class",nullptr,nullptr,nullptr,b);
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,8,8,nullptr,nullptr,hi,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi);DestroyWindow(probe);UINT ow=mi.rcMonitor.right-mi.rcMonitor.left,oh=mi.rcMonitor.bottom-mi.rcMonitor.top;UINT rw=std::min<UINT>(1280,std::max<UINT>(320,ow*2/3)),rh=std::min<UINT>(720,std::max<UINT>(180,oh*2/3));RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW|WS_VISIBLE,FALSE,0);HWND g=CreateWindowExW(0,wc.lpszClassName,L"game",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,60,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,hi,nullptr);HWND p=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,0,0,ow,oh,g,nullptr,hi,nullptr);if(!g||!p)return fail(12,"windows",nullptr,g,p,b);
    ID3D11Device*d=nullptr;ID3D11DeviceContext*c=nullptr;IDXGISwapChain*sc=nullptr;ID3D11Texture2D*bb=nullptr;ID3D11RenderTargetView*rtv=nullptr;D3D_FEATURE_LEVEL fl{};DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=ow;sd.BufferDesc.Height=oh;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=p;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&sc,&d,&fl,&c);if(FAILED(hr))return fail(13,"d3d",nullptr,g,p,b);sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb);if(!bb||FAILED(d->CreateRenderTargetView(bb,nullptr,&rtv)))return fail(14,"rtv",nullptr,g,p,b);
    HMODULE m=LoadLibraryW(L"ptar_borderless.dll");if(!m)return fail(15,"load",nullptr,g,p,b);auto a=(AttachFn)GetProcAddress(m,"PTAR_BorderlessAttachStable");auto q=(QueryFn)GetProcAddress(m,"PTAR_BorderlessQueryMode");if(!a||!q)return fail(16,"exports",q,g,p,b);if(a(g,p,rw,rh,ow,oh)!=0)return fail(17,"attach",q,g,p,b);ModeState s{};unsigned e=0;if(!wait_state(q,g,p,false,mi.rcMonitor,5000,e,s))return fail(18,"initial-windowed",q,g,p,b);
    constexpr unsigned kPairs=100;unsigned maxB=0,maxW=0;unsigned long long sumB=0,sumW=0;
    for(unsigned i=0;i<kPairs;++i){
        rr=0;if(!write_pref(1,&rr)||read_pref()!=1)return fail(20,"write-borderless",q,g,p,b,rr);if(!wait_state(q,g,p,true,mi.rcMonitor,3000,e,s))return fail(21,"watch-borderless",q,g,p,b,rr);maxB=std::max(maxB,e);sumB+=e;float r[4]={0.8f,0.1f,0.1f,1};c->ClearRenderTargetView(rtv,r);if(FAILED(sc->Present(0,0)))return fail(22,"present-borderless",q,g,p,b);
        rr=0;if(!write_pref(0,&rr)||read_pref()!=0)return fail(23,"write-windowed",q,g,p,b,rr);if(!wait_state(q,g,p,false,mi.rcMonitor,3000,e,s))return fail(24,"watch-windowed",q,g,p,b,rr);maxW=std::max(maxW,e);sumW+=e;InvalidateRect(g,nullptr,TRUE);UpdateWindow(g);float v[4]={0.1f,0.2f,0.8f,1};c->ClearRenderTargetView(rtv,v);if(FAILED(sc->Present(0,0)))return fail(25,"present-windowed",q,g,p,b);
    }
    DXGI_SWAP_CHAIN_DESC got{};if(FAILED(sc->GetDesc(&got))||got.OutputWindow!=p)return fail(26,"swap-hwnd",q,g,p,b);
    std::printf("RC48_PREF_STRESS=PASS pairs=%u transitions=%u max_borderless_ms=%u max_windowed_ms=%u avg_borderless_ms=%llu avg_windowed_ms=%llu swap_output_stable=PASS black_parent_repaint=PASS\n",kPairs,kPairs*2,maxB,maxW,sumB/kPairs,sumW/kPairs);
    restore(b);rel(rtv);rel(bb);rel(sc);rel(c);rel(d);FreeLibrary(m);DestroyWindow(p);DestroyWindow(g);DeleteObject(g_black);return 0;
}
