#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <cmath>

static HBRUSH g_black=nullptr;
static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
struct RegBackup{bool existed=false;DWORD value=0;};

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_black);return 1;}
    return DefWindowProcW(h,m,w,l);
}
static void pump(unsigned ms=1){DWORD st=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-st<ms);}
static void backup_pref(RegBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void restore_pref(const RegBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
static bool client_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return b.x>a.x&&b.y>a.y;}
static void client_size(HWND h,UINT& w,UINT& hgt){RECT c{};if(GetClientRect(h,&c)){w=(UINT)(c.right-c.left);hgt=(UINT)(c.bottom-c.top);}else w=hgt=0;}

template<class T>static void rel(T*& p){if(p){p->Release();p=nullptr;}}

struct Pixels{unsigned n=0,expected=0,black=0;unsigned ar=0,ag=0,ab=0;};
static Pixels sample_rect(const RECT& r,BYTE er,BYTE eg,BYTE eb){
    Pixels s{};RECT screen{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)},vis{};if(!IntersectRect(&vis,&r,&screen))return s;
    HDC dc=GetDC(nullptr);if(!dc)return s;
    for(int yy=1;yy<=5;++yy)for(int xx=1;xx<=5;++xx){int x=vis.left+(vis.right-vis.left)*xx/6,y=vis.top+(vis.bottom-vis.top)*yy/6;COLORREF c=GetPixel(dc,x,y);if(c==CLR_INVALID)continue;BYTE rr=GetRValue(c),gg=GetGValue(c),bb=GetBValue(c);++s.n;s.ar+=rr;s.ag+=gg;s.ab+=bb;if(abs((int)rr-er)<=60&&abs((int)gg-eg)<=60&&abs((int)bb-eb)<=60)++s.expected;if(rr<20&&gg<20&&bb<20)++s.black;}
    ReleaseDC(nullptr,dc);return s;
}

static HWND wait_presenter(unsigned timeout=10000){DWORD st=GetTickCount();do{HWND h=FindWindowW(L"Win81USRPresenterV041",nullptr);if(h&&IsWindow(h))return h;pump(5);}while(GetTickCount()-st<timeout);return nullptr;}
static bool wait_client(HWND h,UINT wantW,UINT wantH,unsigned timeout=8000){DWORD st=GetTickCount();do{UINT w=0,hh=0;client_size(h,w,hh);if(w==wantW&&hh==wantH)return true;pump(10);}while(GetTickCount()-st<timeout);return false;}

static constexpr uintptr_t kPresenterSwapchain=0x02C7DFC8u;
static constexpr uintptr_t kPresenterCachedW=0x02C7E000u;
static constexpr uintptr_t kPresenterCachedH=0x02C7E004u;
static constexpr uintptr_t kReleasePresenterResources=0x00024DF0u;
static constexpr uintptr_t kRebuildPresenterBackbuffer=0x0000C200u;
using CleanupFn=void(*)();
using RebuildFn=HRESULT(*)(ID3D11Device*);

struct Contract{
    UINT hwndW=0,hwndH=0,swapW=0,swapH=0,cachedW=0,cachedH=0;
    DXGI_FORMAT fmt=DXGI_FORMAT_UNKNOWN;UINT flags=0;HWND outputWindow=nullptr;
};
static bool read_contract(HMODULE runtime,HWND presenter,Contract& c){
    c={};client_size(presenter,c.hwndW,c.hwndH);
    auto base=reinterpret_cast<BYTE*>(runtime);
    IDXGISwapChain* sc=*reinterpret_cast<IDXGISwapChain**>(base+kPresenterSwapchain);
    if(!sc)return false;
    DXGI_SWAP_CHAIN_DESC d{};if(FAILED(sc->GetDesc(&d)))return false;
    c.swapW=d.BufferDesc.Width;c.swapH=d.BufferDesc.Height;c.fmt=d.BufferDesc.Format;c.flags=d.Flags;c.outputWindow=d.OutputWindow;
    c.cachedW=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedW);c.cachedH=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedH);
    return true;
}
static void print_contract(FILE* f,const wchar_t* tag,const Contract& c){fwprintf(f,L"%ls hwnd=%ux%u swap=%ux%u cached=%ux%u fmt=%u flags=0x%X output=%p match_hwnd_swap=%u match_swap_cached=%u\n",tag,c.hwndW,c.hwndH,c.swapW,c.swapH,c.cachedW,c.cachedH,(unsigned)c.fmt,c.flags,c.outputWindow,(c.hwndW==c.swapW&&c.hwndH==c.swapH)?1u:0u,(c.swapW==c.cachedW&&c.swapH==c.cachedH)?1u:0u);fflush(f);}

static HRESULT resync_presenter_to_hwnd(FILE* f,HMODULE runtime,HWND presenter,ID3D11Device* dev){
    Contract before{};if(!read_contract(runtime,presenter,before))return E_FAIL;print_contract(f,L"CONTRACT_BEFORE_RESYNC",before);
    if(!before.hwndW||!before.hwndH)return E_INVALIDARG;
    if(before.swapW==before.hwndW&&before.swapH==before.hwndH&&before.cachedW==before.swapW&&before.cachedH==before.swapH){fwprintf(f,L"RESYNC_NOOP already_matched=1\n");fflush(f);return S_OK;}
    auto base=reinterpret_cast<BYTE*>(runtime);auto cleanup=reinterpret_cast<CleanupFn>(base+kReleasePresenterResources);auto rebuild=reinterpret_cast<RebuildFn>(base+kRebuildPresenterBackbuffer);
    IDXGISwapChain* sc=*reinterpret_cast<IDXGISwapChain**>(base+kPresenterSwapchain);if(!sc||!cleanup||!rebuild)return E_FAIL;
    sc->AddRef();
    cleanup();
    HRESULT hr=sc->ResizeBuffers(0,before.hwndW,before.hwndH,DXGI_FORMAT_UNKNOWN,before.flags);
    fwprintf(f,L"RESYNC_RESIZE hr=0x%08X target=%ux%u\n",(unsigned)hr,before.hwndW,before.hwndH);fflush(f);
    if(SUCCEEDED(hr)){
        HRESULT rb=rebuild(dev);fwprintf(f,L"RESYNC_REBUILD hr=0x%08X\n",(unsigned)rb);fflush(f);if(FAILED(rb))hr=rb;
    }
    sc->Release();
    Contract after{};if(read_contract(runtime,presenter,after))print_contract(f,L"CONTRACT_AFTER_RESYNC",after);
    return hr;
}

static bool render_frames(ID3D11DeviceContext* ctx,IDXGISwapChain* gameSwap,ID3D11RenderTargetView* gameRtv,HWND game,unsigned frames,FILE* log,Pixels& last){
    const float magenta[4]={1.0f,0.0f,0.75f,1.0f};RECT cr{};last={};
    for(unsigned i=0;i<frames;++i){ctx->OMSetRenderTargets(1,&gameRtv,nullptr);ctx->ClearRenderTargetView(gameRtv,magenta);HRESULT hr=gameSwap->Present(1,0);pump();if(FAILED(hr)){fwprintf(log,L"PRESENT_FAIL i=%u hr=0x%08X\n",i,(unsigned)hr);return false;}if(i>30&&i%10==0&&client_screen(game,cr))last=sample_rect(cr,255,0,191);}
    if(client_screen(game,cr))last=sample_rect(cr,255,0,191);fwprintf(log,L"PIXELS n=%u expected=%u black=%u avg=%u,%u,%u\n",last.n,last.expected,last.black,last.n?last.ar/last.n:0,last.n?last.ag/last.n:0,last.n?last.ab/last.n:0);fflush(log);return true;
}
static bool pixels_ok(const Pixels& p){return p.n>=20&&p.expected>=p.n*3/5&&p.black<=p.n/5;}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int){
    FILE* log=nullptr;_wfopen_s(&log,L"RC51_CONTRACT_HOST.txt",L"wb");if(!log)return 90;
    RegBackup rb{};backup_pref(rb);if(!set_pref(0)){fwprintf(log,L"FAIL registry\n");fclose(log);return 10;}
    const UINT outW=(UINT)GetSystemMetrics(SM_CXSCREEN),outH=(UINT)GetSystemMetrics(SM_CYSCREEN),renderW=(outW*2u)/3u,renderH=(outH*2u)/3u;
    fwprintf(log,L"RC51_HOST_BEGIN output=%ux%u render=%ux%u\n",outW,outH,renderW,renderH);fflush(log);
    g_black=CreateSolidBrush(RGB(0,0,0));WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_black;wc.lpszClassName=L"PTAR_RC51_GAME";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){restore_pref(rb);fclose(log);return 11;}
    DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,(LONG)renderW,(LONG)renderH};AdjustWindowRectEx(&wr,style,FALSE,0);HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,80,55,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);if(!game){restore_pref(rb);fclose(log);return 12;}ShowWindow(game,SW_SHOW);UpdateWindow(game);SetForegroundWindow(game);pump(20);
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=renderW;sd.BufferDesc.Height=renderH;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferDesc.RefreshRate.Numerator=60;sd.BufferDesc.RefreshRate.Denominator=1;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=game;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* gameSwap=nullptr;D3D_FEATURE_LEVEL fl{};HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&gameSwap,&dev,&fl,&ctx);fwprintf(log,L"D3D_CREATE hr=0x%08X fl=0x%X\n",(unsigned)hr,(unsigned)fl);fflush(log);if(FAILED(hr)||!gameSwap||!dev||!ctx){restore_pref(rb);fclose(log);return 13;}
    ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rtv=nullptr;if(FAILED(gameSwap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&bb)))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv))){restore_pref(rb);fclose(log);return 14;}rel(bb);
    Pixels px{};if(!render_frames(ctx,gameSwap,rtv,game,120,log,px)){restore_pref(rb);fclose(log);return 15;}
    HWND presenter=wait_presenter();HMODULE runtime=GetModuleHandleW(L"d3d11.dll");if(!presenter||!runtime){fwprintf(log,L"FAIL presenter/runtime presenter=%p runtime=%p\n",presenter,runtime);restore_pref(rb);fclose(log);return 16;}
    if(!wait_client(presenter,renderW,renderH,8000)){UINT w=0,h=0;client_size(presenter,w,h);fwprintf(log,L"FAIL RC46 windowed presenter geometry observed=%ux%u expected=%ux%u\n",w,h,renderW,renderH);restore_pref(rb);fclose(log);return 17;}
    Contract initial{};if(!read_contract(runtime,presenter,initial)){restore_pref(rb);fclose(log);return 18;}print_contract(log,L"WINDOWED_INITIAL_CONTRACT",initial);
    const bool mismatchObserved=(initial.hwndW==renderW&&initial.hwndH==renderH&&initial.swapW==outW&&initial.swapH==outH&&initial.cachedW==outW&&initial.cachedH==outH);
    fwprintf(log,L"WINDOWED_NATIVE_SWAPCHAIN_MISMATCH=%ls\n",mismatchObserved?L"PASS":L"FAIL");fflush(log);if(!mismatchObserved){restore_pref(rb);fclose(log);return 19;}
    hr=resync_presenter_to_hwnd(log,runtime,presenter,dev);if(FAILED(hr)){restore_pref(rb);fclose(log);return 20;}
    Contract matched{};if(!read_contract(runtime,presenter,matched)){restore_pref(rb);fclose(log);return 21;}const bool matchedNow=matched.hwndW==matched.swapW&&matched.hwndH==matched.swapH&&matched.swapW==matched.cachedW&&matched.swapH==matched.cachedH;fwprintf(log,L"WINDOWED_CONTRACT_MATCHED=%ls\n",matchedNow?L"PASS":L"FAIL");fflush(log);if(!matchedNow){restore_pref(rb);fclose(log);return 22;}
    if(!render_frames(ctx,gameSwap,rtv,game,180,log,px)||!pixels_ok(px)){fwprintf(log,L"WINDOWED_PIXELS_AFTER_RESYNC=FAIL\n");restore_pref(rb);fclose(log);return 23;}fwprintf(log,L"WINDOWED_PIXELS_AFTER_RESYNC=PASS\n");fflush(log);

    if(!set_pref(1)){restore_pref(rb);fclose(log);return 24;}if(!wait_client(presenter,outW,outH,10000)){UINT w=0,h=0;client_size(presenter,w,h);fwprintf(log,L"FAIL borderless presenter geometry=%ux%u\n",w,h);restore_pref(rb);fclose(log);return 25;}
    Contract borderBefore{};read_contract(runtime,presenter,borderBefore);print_contract(log,L"BORDERLESS_BEFORE_RESYNC",borderBefore);hr=resync_presenter_to_hwnd(log,runtime,presenter,dev);if(FAILED(hr)){restore_pref(rb);fclose(log);return 26;}Contract borderAfter{};read_contract(runtime,presenter,borderAfter);print_contract(log,L"BORDERLESS_AFTER_RESYNC",borderAfter);if(borderAfter.hwndW!=outW||borderAfter.hwndH!=outH||borderAfter.swapW!=outW||borderAfter.swapH!=outH||borderAfter.cachedW!=outW||borderAfter.cachedH!=outH){restore_pref(rb);fclose(log);return 27;}
    if(!render_frames(ctx,gameSwap,rtv,game,120,log,px)||!pixels_ok(px)){fwprintf(log,L"BORDERLESS_PIXELS_AFTER_RESYNC=FAIL\n");restore_pref(rb);fclose(log);return 28;}fwprintf(log,L"BORDERLESS_PIXELS_AFTER_RESYNC=PASS\n");fflush(log);

    if(!set_pref(0)){restore_pref(rb);fclose(log);return 29;}if(!wait_client(presenter,renderW,renderH,10000)){restore_pref(rb);fclose(log);return 30;}hr=resync_presenter_to_hwnd(log,runtime,presenter,dev);if(FAILED(hr)){restore_pref(rb);fclose(log);return 31;}Contract final{};read_contract(runtime,presenter,final);print_contract(log,L"WINDOWED_REENTRY_AFTER_RESYNC",final);if(final.hwndW!=renderW||final.hwndH!=renderH||final.swapW!=renderW||final.swapH!=renderH||final.cachedW!=renderW||final.cachedH!=renderH){restore_pref(rb);fclose(log);return 32;}
    if(!render_frames(ctx,gameSwap,rtv,game,120,log,px)||!pixels_ok(px)){fwprintf(log,L"WINDOWED_REENTRY_PIXELS=FAIL\n");restore_pref(rb);fclose(log);return 33;}fwprintf(log,L"WINDOWED_REENTRY_PIXELS=PASS\nRC51_PRESENTER_SWAPCHAIN_CONTRACT=PASS\n");fflush(log);

    rel(rtv);rel(gameSwap);rel(ctx);rel(dev);DestroyWindow(game);DeleteObject(g_black);restore_pref(rb);fclose(log);return 0;
}
