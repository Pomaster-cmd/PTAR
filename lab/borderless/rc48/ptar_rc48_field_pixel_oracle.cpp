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

struct ModeState{
    UINT size,installed,borderlessActive,presenterVisible;
    ULONG_PTR gameStyle,presenterStyle;
    unsigned long long transitionsToWindowed,transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom,windowStylePreference;
    unsigned long long presenterFollows,gameCoercionClamps,presenterClamps,rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
};

static HBRUSH g_black=nullptr;
static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";

template<class T> static void rel(T*& p){if(p){p->Release();p=nullptr;}}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);FillRect(dc,&ps.rcPaint,g_black);EndPaint(h,&ps);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void pump(unsigned max=128){MSG msg{};unsigned n=0;while(n<max&&PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);++n;}}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&v),sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static bool query(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q&&q(&s)==0;}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return true;}
static bool wait_windowed(QueryFn q,HWND game,HWND presenter,unsigned timeoutMs){DWORD st=GetTickCount();for(;;){pump();ModeState s{};RECT gc{},pr{};LONG_PTR ps=GetWindowLongPtrW(presenter,GWL_STYLE);bool child=(ps&WS_CHILD)!=0 && (ps&WS_POPUP)==0 && GetParent(presenter)==game;bool geom=client_screen(game,gc)&&GetWindowRect(presenter,&pr)&&EqualRect(&gc,&pr);if(query(q,s)&&s.installed&&!s.borderlessActive&&child&&geom&&IsWindowVisible(presenter))return true;if(GetTickCount()-st>=timeoutMs)return false;Sleep(1);}}

struct PixelStats{unsigned samples=0,nearExpected=0,nearBlack=0;BYTE avgR=0,avgG=0,avgB=0;};
static PixelStats sample_screen(const RECT& r,BYTE er,BYTE eg,BYTE eb){
    PixelStats s{};HDC dc=GetDC(nullptr);if(!dc)return s;unsigned long long sr=0,sg=0,sb=0;
    const int xs[5]={1,2,3,4,5};
    for(int iy=1;iy<=5;++iy){for(int ix=1;ix<=5;++ix){
        int x=r.left+(r.right-r.left)*xs[ix-1]/6;int y=r.top+(r.bottom-r.top)*iy/6;COLORREF c=GetPixel(dc,x,y);if(c==CLR_INVALID)continue;BYTE rr=GetRValue(c),gg=GetGValue(c),bb=GetBValue(c);++s.samples;sr+=rr;sg+=gg;sb+=bb;
        if(abs((int)rr-er)<=45&&abs((int)gg-eg)<=45&&abs((int)bb-eb)<=45)++s.nearExpected;
        if(rr<20&&gg<20&&bb<20)++s.nearBlack;
    }}ReleaseDC(nullptr,dc);if(s.samples){s.avgR=(BYTE)(sr/s.samples);s.avgG=(BYTE)(sg/s.samples);s.avgB=(BYTE)(sb/s.samples);}return s;
}
static bool backbuffer_matches(ID3D11Device* dev,ID3D11DeviceContext* ctx,IDXGISwapChain* swap,BYTE er,BYTE eg,BYTE eb){
    ID3D11Texture2D* bb=nullptr;ID3D11Texture2D* st=nullptr;if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&bb)))||!bb)return false;D3D11_TEXTURE2D_DESC d{};bb->GetDesc(&d);D3D11_TEXTURE2D_DESC sd=d;sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;sd.MiscFlags=0;bool ok=false;
    if(SUCCEEDED(dev->CreateTexture2D(&sd,nullptr,&st))&&st){ctx->CopyResource(st,bb);D3D11_MAPPED_SUBRESOURCE m{};if(SUCCEEDED(ctx->Map(st,0,D3D11_MAP_READ,0,&m))){const BYTE* p=(const BYTE*)m.pData+(d.Height/2)*m.RowPitch+(d.Width/2)*4;BYTE br=p[0],bg=p[1],rr=p[2];ok=abs((int)rr-er)<=8&&abs((int)bg-eg)<=8&&abs((int)br-eb)<=8;ctx->Unmap(st,0);}}
    rel(st);rel(bb);return ok;
}
static void print_phase(const char* name,HWND game,HWND presenter,const PixelStats& p,bool gpu){RECT gr{},gc{},pr{};GetWindowRect(game,&gr);client_screen(game,gc);GetWindowRect(presenter,&pr);std::printf("%s gpu=%d samples=%u expected=%u black=%u avg=%u,%u,%u parent=%p owner=%p pstyle=0x%llx pex=0x%llx grect=%ld,%ld,%ld,%ld gclient=%ld,%ld,%ld,%ld prect=%ld,%ld,%ld,%ld\n",name,gpu?1:0,p.samples,p.nearExpected,p.nearBlack,p.avgR,p.avgG,p.avgB,GetParent(presenter),GetWindow(presenter,GW_OWNER),(unsigned long long)GetWindowLongPtrW(presenter,GWL_STYLE),(unsigned long long)GetWindowLongPtrW(presenter,GWL_EXSTYLE),gr.left,gr.top,gr.right,gr.bottom,gc.left,gc.top,gc.right,gc.bottom,pr.left,pr.top,pr.right,pr.bottom);}

int wmain(){
    if(!set_pref(0)){std::puts("RC48_FIELD_PIXEL_ORACLE=FAIL pref");return 10;}
    HINSTANCE inst=GetModuleHandleW(nullptr);g_black=CreateSolidBrush(RGB(0,0,0));WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_RC48_FIELD_PIXEL_ORACLE";RegisterClassW(&wc);
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,16,16,nullptr,nullptr,inst,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi))return 11;DestroyWindow(probe);UINT outW=mi.rcMonitor.right-mi.rcMonitor.left,outH=mi.rcMonitor.bottom-mi.rcMonitor.top;UINT rw=std::min<UINT>(1280,std::max<UINT>(640,outW*2/3)),rh=std::min<UINT>(720,std::max<UINT>(360,outH*2/3));RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW|WS_VISIBLE,FALSE,0);
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game-black-parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,mi.rcMonitor.left+70,mi.rcMonitor.top+55,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);
    const DWORD fieldEx=WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW;
    HWND presenter=CreateWindowExW(fieldEx,wc.lpszClassName,L"field-presenter",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,game,nullptr,inst,nullptr);
    if(!game||!presenter)return 12;ShowWindow(game,SW_SHOW);ShowWindow(presenter,SW_SHOW);UpdateWindow(game);UpdateWindow(presenter);pump(256);

    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rtv=nullptr;D3D_FEATURE_LEVEL fl{};DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=outW;sd.BufferDesc.Height=outH;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=presenter;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);if(FAILED(hr))return 13;if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&bb)))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv)))return 14;rel(bb);

    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return 15;auto attach=reinterpret_cast<AttachFn>(GetProcAddress(dll,"PTAR_BorderlessAttachStable"));auto q=reinterpret_cast<QueryFn>(GetProcAddress(dll,"PTAR_BorderlessQueryMode"));if(!attach||!q)return 16;if(attach(game,presenter,rw,rh,outW,outH)!=0)return 17;if(!wait_windowed(q,game,presenter,5000)){std::puts("RC48_FIELD_PIXEL_ORACLE=FAIL attach-windowed");return 18;}

    const float magenta[4]={1.0f,0.0f,0.75f,1.0f};
    for(unsigned i=0;i<240;++i){ctx->ClearRenderTargetView(rtv,magenta);hr=swap->Present(0,0);if(FAILED(hr))return 19;pump(32);Sleep(1);}RECT gc{};client_screen(game,gc);auto quiet=sample_screen(gc,255,0,191);bool quietGpu=backbuffer_matches(dev,ctx,swap,255,0,191);print_phase("QUIET_EXACT_FIELD_STYLE",game,presenter,quiet,quietGpu);

    // Reproduce the production presenter's native-output recovery pressure. The real
    // P1U46 presenter was observed in the field with EXSTYLE 0x080000A8 and top-level
    // output geometry before RC48 attaches. Reassert those native attributes between
    // Presents and require RC48 to keep a visibly composed child, not merely S_OK Present.
    for(unsigned i=0;i<2000;++i){
        if((i%3u)==0){SetWindowLongPtrW(presenter,GWL_EXSTYLE,fieldEx);SetWindowPos(presenter,HWND_TOPMOST,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,SWP_NOACTIVATE|SWP_SHOWWINDOW|SWP_FRAMECHANGED);}
        if((i%17u)==0){LONG_PTR st=GetWindowLongPtrW(presenter,GWL_STYLE);SetWindowLongPtrW(presenter,GWL_STYLE,(st&~(LONG_PTR)WS_CHILD)|WS_POPUP|WS_VISIBLE);SetParent(presenter,nullptr);SetWindowLongPtrW(presenter,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(game));}
        ctx->ClearRenderTargetView(rtv,magenta);hr=swap->Present(0,0);if(FAILED(hr))return 20;pump(64);Sleep(1);
    }
    if(!wait_windowed(q,game,presenter,5000)){std::puts("RC48_FIELD_PIXEL_ORACLE=FAIL hostile-windowed-invariant");return 21;}
    for(unsigned i=0;i<120;++i){ctx->ClearRenderTargetView(rtv,magenta);if(FAILED(swap->Present(0,0)))return 22;pump(32);Sleep(1);}client_screen(game,gc);auto hostile=sample_screen(gc,255,0,191);bool hostileGpu=backbuffer_matches(dev,ctx,swap,255,0,191);print_phase("HOSTILE_FIELD_RECOVERY",game,presenter,hostile,hostileGpu);

    bool quietVisible=quietGpu&&quiet.samples>=20&&quiet.nearExpected>=quiet.samples*3/5&&quiet.nearBlack<=quiet.samples/5;
    bool hostileVisible=hostileGpu&&hostile.samples>=20&&hostile.nearExpected>=hostile.samples*3/5&&hostile.nearBlack<=hostile.samples/5;
    if(!quietVisible||!hostileVisible){std::printf("RC48_FIELD_PIXEL_ORACLE=FAIL quiet_visible=%d hostile_visible=%d\n",quietVisible?1:0,hostileVisible?1:0);return 30;}
    std::printf("RC48_FIELD_PIXEL_ORACLE=PASS quiet_expected=%u/%u hostile_expected=%u/%u gpu_quiet=%d gpu_hostile=%d feature_level=0x%x\n",quiet.nearExpected,quiet.samples,hostile.nearExpected,hostile.samples,quietGpu?1:0,hostileGpu?1:0,(unsigned)fl);
    rel(rtv);rel(swap);rel(ctx);rel(dev);FreeLibrary(dll);DestroyWindow(presenter);DestroyWindow(game);DeleteObject(g_black);return 0;
}
