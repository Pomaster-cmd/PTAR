#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <cmath>

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static HBRUSH g_black=nullptr;
struct RegBackup{bool existed=false;DWORD value=0;};
struct SyncState{UINT size,installed,resyncing;unsigned long long presentCalls,mismatchChecks,resyncs,failures,transitionSkips,zeroClientSkips,swapReplacements;UINT hwndW,hwndH,swapW,swapH,cachedW,cachedH;};
using QuerySyncFn=int (WINAPI*)(SyncState*);

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_black);return 1;}return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms=1){DWORD st=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-st<ms);}
static void backup_pref(RegBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void restore_pref(const RegBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
template<class T>static void rel(T*& p){if(p){p->Release();p=nullptr;}}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return b.x>a.x&&b.y>a.y;}
static void client_size(HWND h,UINT& w,UINT& hh){RECT c{};if(GetClientRect(h,&c)){w=(UINT)(c.right-c.left);hh=(UINT)(c.bottom-c.top);}else w=hh=0;}
static HWND wait_presenter(unsigned timeout=10000){DWORD st=GetTickCount();do{HWND h=FindWindowW(L"Win81USRPresenterV041",nullptr);if(h&&IsWindow(h))return h;pump(5);}while(GetTickCount()-st<timeout);return nullptr;}

struct Pixels{unsigned n=0,expected=0,black=0;};
static Pixels sample(HWND game){Pixels s{};RECT r{},screen{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)},vis{};if(!client_screen(game,r)||!IntersectRect(&vis,&r,&screen))return s;HDC dc=GetDC(nullptr);if(!dc)return s;for(int yy=1;yy<=5;++yy)for(int xx=1;xx<=5;++xx){int x=vis.left+(vis.right-vis.left)*xx/6,y=vis.top+(vis.bottom-vis.top)*yy/6;COLORREF c=GetPixel(dc,x,y);if(c==CLR_INVALID)continue;BYTE rr=GetRValue(c),gg=GetGValue(c),bb=GetBValue(c);++s.n;if(abs((int)rr-255)<=60&&gg<=60&&abs((int)bb-191)<=60)++s.expected;if(rr<20&&gg<20&&bb<20)++s.black;}ReleaseDC(nullptr,dc);return s;}
static bool pixels_ok(const Pixels& p){return p.n>=20&&p.expected>=p.n*3/5&&p.black<=p.n/5;}

static constexpr uintptr_t kPresenterSwapchain=0x02C7DFC8u,kPresenterCachedW=0x02C7E000u,kPresenterCachedH=0x02C7E004u;
static bool physical_contract(HMODULE runtime,HWND presenter,UINT& hw,UINT& hh,UINT& sw,UINT& sh,UINT& cw,UINT& ch){
    client_size(presenter,hw,hh);auto base=(BYTE*)runtime;IDXGISwapChain* sc=*reinterpret_cast<IDXGISwapChain**>(base+kPresenterSwapchain);if(!sc)return false;DXGI_SWAP_CHAIN_DESC d{};if(FAILED(sc->GetDesc(&d)))return false;sw=d.BufferDesc.Width;sh=d.BufferDesc.Height;cw=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedW);ch=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedH);return true;
}

struct Driver{ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;ID3D11RenderTargetView* rtv=nullptr;HWND game=nullptr;FILE* log=nullptr;const float color[4]={1.0f,0.0f,0.75f,1.0f};
    bool frame(){ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->ClearRenderTargetView(rtv,color);HRESULT hr=swap->Present(1,0);pump();return SUCCEEDED(hr);}
};

static bool drive_contract(Driver& d,HMODULE runtime,HWND presenter,QuerySyncFn query,UINT wantW,UINT wantH,unsigned timeout,const wchar_t* tag){
    DWORD st=GetTickCount();SyncState ss{};ss.size=sizeof(ss);UINT hw=0,hh=0,sw=0,sh=0,cw=0,ch=0;
    do{
        if(!d.frame())return false;
        ss={};ss.size=sizeof(ss);query(&ss);
        if(physical_contract(runtime,presenter,hw,hh,sw,sh,cw,ch) && hw==wantW&&hh==wantH&&sw==wantW&&sh==wantH&&cw==wantW&&ch==wantH&&ss.installed&&ss.failures==0){
            fwprintf(d.log,L"CONTRACT_%ls=PASS hwnd=%ux%u swap=%ux%u cached=%ux%u resyncs=%llu presents=%llu skips=%llu\n",tag,hw,hh,sw,sh,cw,ch,ss.resyncs,ss.presentCalls,ss.transitionSkips);fflush(d.log);return true;
        }
    }while(GetTickCount()-st<timeout);
    fwprintf(d.log,L"CONTRACT_%ls=FAIL hwnd=%ux%u swap=%ux%u cached=%ux%u installed=%u failures=%llu resyncs=%llu presents=%llu\n",tag,hw,hh,sw,sh,cw,ch,ss.installed,ss.failures,ss.resyncs,ss.presentCalls);fflush(d.log);return false;
}

static bool resize_client(HWND game,UINT cw,UINT ch){DWORD style=(DWORD)GetWindowLongPtrW(game,GWL_STYLE),ex=(DWORD)GetWindowLongPtrW(game,GWL_EXSTYLE);RECT r{0,0,(LONG)cw,(LONG)ch};if(!AdjustWindowRectEx(&r,style,FALSE,ex))return false;RECT cur{};GetWindowRect(game,&cur);return SetWindowPos(game,nullptr,cur.left,cur.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE;}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int){
    FILE* log=nullptr;_wfopen_s(&log,L"RC51_AUTO_STRESS.txt",L"wb");if(!log)return 90;RegBackup rb{};backup_pref(rb);if(!set_pref(0)){fclose(log);return 10;}
    const UINT outW=(UINT)GetSystemMetrics(SM_CXSCREEN),outH=(UINT)GetSystemMetrics(SM_CYSCREEN),renderW=(outW*2u)/3u,renderH=(outH*2u)/3u;fwprintf(log,L"RC51_AUTO_BEGIN output=%ux%u render=%ux%u\n",outW,outH,renderW,renderH);fflush(log);
    g_black=CreateSolidBrush(RGB(0,0,0));WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_RC51_AUTO_GAME";RegisterClassW(&wc);DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,(LONG)renderW,(LONG)renderH};AdjustWindowRectEx(&wr,style,FALSE,0);HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,80,55,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);if(!game){restore_pref(rb);fclose(log);return 11;}ShowWindow(game,SW_SHOW);UpdateWindow(game);SetForegroundWindow(game);pump(20);
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=renderW;sd.BufferDesc.Height=renderH;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferDesc.RefreshRate.Numerator=60;sd.BufferDesc.RefreshRate.Denominator=1;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=game;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;D3D_FEATURE_LEVEL fl{};HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);if(FAILED(hr)){restore_pref(rb);fclose(log);return 12;}ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rtv=nullptr;if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv))){restore_pref(rb);fclose(log);return 13;}rel(bb);Driver d{ctx,swap,rtv,game,log};for(unsigned i=0;i<90;++i)if(!d.frame()){restore_pref(rb);fclose(log);return 14;}
    HWND presenter=wait_presenter();HMODULE runtime=GetModuleHandleW(L"d3d11.dll"),carrier=GetModuleHandleW(L"ptar_borderless.dll");if(!presenter||!runtime||!carrier){restore_pref(rb);fclose(log);return 15;}auto query=(QuerySyncFn)GetProcAddress(carrier,"PTAR_RC51_QueryPresenterSync");if(!query){restore_pref(rb);fclose(log);return 16;}
    HWND owner0=GetWindow(presenter,GW_OWNER);LONG_PTR style0=GetWindowLongPtrW(presenter,GWL_STYLE);fwprintf(log,L"PRESENTER_BASE owner=%p style=0x%llX\n",owner0,(unsigned long long)style0);fflush(log);
    if(!drive_contract(d,runtime,presenter,query,renderW,renderH,12000,L"INITIAL_WINDOWED")){restore_pref(rb);fclose(log);return 17;}Pixels p=sample(game);fwprintf(log,L"PIXELS_INITIAL n=%u expected=%u black=%u\n",p.n,p.expected,p.black);if(!pixels_ok(p)){restore_pref(rb);fclose(log);return 18;}

    constexpr unsigned cycles=60;
    for(unsigned i=0;i<cycles;++i){
        if(!set_pref(1)||!drive_contract(d,runtime,presenter,query,outW,outH,7000,L"BORDERLESS_STRESS")){restore_pref(rb);fclose(log);return 20;}
        if(!set_pref(0)||!drive_contract(d,runtime,presenter,query,renderW,renderH,7000,L"WINDOWED_STRESS")){restore_pref(rb);fclose(log);return 21;}
        if((i%10)==0){p=sample(game);if(!pixels_ok(p)){fwprintf(log,L"STRESS_PIXELS_FAIL cycle=%u n=%u expected=%u black=%u\n",i,p.n,p.expected,p.black);restore_pref(rb);fclose(log);return 22;}}
    }
    fwprintf(log,L"MODE_CYCLES=%u PASS\n",cycles);fflush(log);

    const UINT sizes[][2]={{560,420},{640,460},{720,520},{600,440},{renderW,renderH}};
    for(const auto& z:sizes){if(!resize_client(game,z[0],z[1])){restore_pref(rb);fclose(log);return 23;}if(!drive_contract(d,runtime,presenter,query,z[0],z[1],8000,L"WINDOW_RESIZE")){restore_pref(rb);fclose(log);return 24;}}
    fwprintf(log,L"WINDOW_RESIZE_STRESS=PASS\n");fflush(log);

    ShowWindow(game,SW_MINIMIZE);pump(250);for(unsigned i=0;i<10;++i)d.frame();ShowWindow(game,SW_RESTORE);SetForegroundWindow(game);pump(100);if(!drive_contract(d,runtime,presenter,query,renderW,renderH,10000,L"MINIMIZE_RESTORE")){restore_pref(rb);fclose(log);return 25;}p=sample(game);if(!pixels_ok(p)){restore_pref(rb);fclose(log);return 26;}fwprintf(log,L"MINIMIZE_RESTORE_PIXELS=PASS\n");

    SyncState s{};s.size=sizeof(s);query(&s);HWND owner1=GetWindow(presenter,GW_OWNER);LONG_PTR style1=GetWindowLongPtrW(presenter,GWL_STYLE);fwprintf(log,L"FINAL_SYNC installed=%u resyncing=%u presents=%llu checks=%llu resyncs=%llu failures=%llu transitionSkips=%llu zeroSkips=%llu replacements=%llu last=%ux%u/%ux%u/%ux%u owner=%p style=0x%llX\n",s.installed,s.resyncing,s.presentCalls,s.mismatchChecks,s.resyncs,s.failures,s.transitionSkips,s.zeroClientSkips,s.swapReplacements,s.hwndW,s.hwndH,s.swapW,s.swapH,s.cachedW,s.cachedH,owner1,(unsigned long long)style1);fflush(log);
    const unsigned long long maxExpected=1ull+cycles*2ull+sizeof(sizes)/sizeof(sizes[0])+2ull;
    if(!s.installed||s.failures||s.resyncs<cycles*2ull+2ull||s.resyncs>maxExpected+10ull||s.resyncing){restore_pref(rb);fclose(log);return 27;}
    if((style1&WS_CHILD)!=0){restore_pref(rb);fclose(log);return 28;}
    fwprintf(log,L"RC51_AUTO_STRESS=PASS cycles=%u resyncs=%llu bounded=PASS pixels=PASS no_ws_child=PASS\n",cycles,s.resyncs);fflush(log);
    rel(rtv);rel(swap);rel(ctx);rel(dev);DestroyWindow(game);DeleteObject(g_black);restore_pref(rb);fclose(log);return 0;
}
