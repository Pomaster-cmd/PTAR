#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>
#include <vector>

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static const char* kStartupMarker="P1U46 STARTUP/INPUT CONTRACT COMPLETE";
static HBRUSH g_brush=nullptr;

struct RegBackup{bool existed=false;DWORD value=0;};
struct ModeState{
    UINT size,installed,borderlessActive,presenterVisible;
    ULONG_PTR gameStyle,presenterStyle;
    unsigned long long transitionsToWindowed,transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom,windowStylePreference;
    unsigned long long presenterFollows,gameCoercionClamps,presenterClamps,rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
};
struct SyncState{
    UINT size,installed,resyncing;
    unsigned long long presentCalls,mismatchChecks,resyncs,failures,transitionSkips,zeroClientSkips,swapReplacements;
    UINT hwndW,hwndH,swapW,swapH,cachedW,cachedH;
};
using QueryModeFn=int (WINAPI*)(ModeState*);
using QuerySyncFn=int (WINAPI*)(SyncState*);

struct Geometry{UINT cw=0,ch=0;LONG_PTR style=0;RECT outer{};RECT clientScreen{};};
static bool same_rect(const RECT&a,const RECT&b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static bool geom(HWND h,Geometry&g){
    if(!IsWindow(h))return false;RECT c{};if(!GetClientRect(h,&c)||!GetWindowRect(h,&g.outer))return false;
    POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b)||b.x<=a.x||b.y<=a.y)return false;
    g.cw=(UINT)(c.right-c.left);g.ch=(UINT)(c.bottom-c.top);g.style=GetWindowLongPtrW(h,GWL_STYLE);g.clientScreen={a.x,a.y,b.x,b.y};return g.cw&&g.ch;
}
static bool game_style_windowed(LONG_PTR s){return (s&WS_POPUP)==0 && (s&WS_CAPTION)==WS_CAPTION;}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_brush);return 1;}return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms=1){DWORD st=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-st<ms);}
static void backup_pref(RegBackup&b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void restore_pref(const RegBackup&b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
template<class T>static void rel(T*&p){if(p){p->Release();p=nullptr;}}
static HWND wait_presenter(unsigned timeout=12000){DWORD st=GetTickCount();do{HWND h=FindWindowW(L"Win81USRPresenterV041",nullptr);if(h&&IsWindow(h))return h;pump(5);}while(GetTickCount()-st<timeout);return nullptr;}

static bool file_contains_shared(const wchar_t*path,const char*needle){
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;LARGE_INTEGER sz{};if(!GetFileSizeEx(h,&sz)||sz.QuadPart<=0||sz.QuadPart>16*1024*1024){CloseHandle(h);return false;}
    std::vector<char> b((size_t)sz.QuadPart+1u,0);DWORD got=0;const BOOL ok=ReadFile(h,b.data(),(DWORD)sz.QuadPart,&got,nullptr);CloseHandle(h);if(!ok)return false;b[got]=0;return std::strstr(b.data(),needle)!=nullptr;
}

struct Driver{
    ID3D11DeviceContext*ctx=nullptr;IDXGISwapChain*swap=nullptr;ID3D11RenderTargetView*rtv=nullptr;
    const float color[4]={0.18f,0.52f,0.91f,1.0f};
    bool frame(){ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->ClearRenderTargetView(rtv,color);HRESULT hr=swap->Present(0,0);pump(2);return SUCCEEDED(hr);}
};
static void log_geom(FILE*f,const wchar_t*tag,const Geometry&g){fwprintf(f,L"%ls client=%ux%u style=0x%llX outer=%ld,%ld,%ld,%ld screen=%ld,%ld,%ld,%ld\n",tag,g.cw,g.ch,(unsigned long long)g.style,g.outer.left,g.outer.top,g.outer.right,g.outer.bottom,g.clientScreen.left,g.clientScreen.top,g.clientScreen.right,g.clientScreen.bottom);fflush(f);}
static bool presenter_native(HWND p){Geometry g{};return geom(p,g)&&g.cw==1920&&g.ch==1080;}
static bool presenter_follows_game(HWND p,HWND game){Geometry a{},b{};return geom(p,a)&&geom(game,b)&&a.cw==b.cw&&a.ch==b.ch&&same_rect(a.clientScreen,b.clientScreen);}

static bool wait_mode(Driver&d,QueryModeFn qm,QuerySyncFn qs,HWND game,HWND presenter,bool borderless,FILE*f,const wchar_t*tag,unsigned timeout=15000){
    const DWORD st=GetTickCount();ModeState ms{};SyncState ss{};Geometry gg{},pg{};
    do{
        if(!d.frame())return false;ms={};ms.size=sizeof(ms);qm(&ms);ss={};ss.size=sizeof(ss);qs(&ss);if(!geom(game,gg)||!geom(presenter,pg))return false;
        const bool visual=borderless?presenter_native(presenter):presenter_follows_game(presenter,game);
        if(ms.installed&&((ms.borderlessActive!=0)==borderless)&&visual&&game_style_windowed(gg.style)&&ss.failures==0){
            fwprintf(f,L"MODE_%ls=PASS borderless=%u game=%ux%u presenter=%ux%u style=0x%llX syncInstalled=%u resyncs=%llu failures=%llu toB=%llu toW=%llu clamps=%llu follows=%llu\n",tag,ms.borderlessActive,gg.cw,gg.ch,pg.cw,pg.ch,(unsigned long long)gg.style,ss.installed,ss.resyncs,ss.failures,ms.transitionsToBorderless,ms.transitionsToWindowed,ms.gameCoercionClamps,ms.presenterFollows);fflush(f);return true;
        }
        pump(20);
    }while(GetTickCount()-st<timeout);
    fwprintf(f,L"MODE_%ls=FAIL wanted=%u actual=%u installed=%u game=%ux%u presenter=%ux%u style=0x%llX syncInstalled=%u syncFailures=%llu\n",tag,borderless?1u:0u,ms.borderlessActive,ms.installed,gg.cw,gg.ch,pg.cw,pg.ch,(unsigned long long)gg.style,ss.installed,ss.failures);fflush(f);return false;
}

static bool wait_runtime_marker(Driver&d,FILE*f,unsigned timeout=45000){
    const DWORD st=GetTickCount();unsigned frames=0;
    do{if(!d.frame())return false;++frames;if(file_contains_shared(L"win81_nis.log",kStartupMarker)){fwprintf(f,L"P1U46_STARTUP_MARKER=PASS frames=%u ageMs=%lu\n",frames,(unsigned long)(GetTickCount()-st));fflush(f);return true;}pump(20);}while(GetTickCount()-st<timeout);
    fwprintf(f,L"P1U46_STARTUP_MARKER=FAIL frames=%u\n",frames);fflush(f);return false;
}

static bool wait_rc51_live(Driver&d,QuerySyncFn qs,FILE*f,unsigned timeout=15000){
    const DWORD st=GetTickCount();SyncState s{};
    do{if(!d.frame())return false;s={};s.size=sizeof(s);qs(&s);if(s.installed&&s.presentCalls>0&&s.failures==0){fwprintf(f,L"RC51_LIVE=PASS presents=%llu resyncs=%llu failures=%llu last=%ux%u/%ux%u/%ux%u\n",s.presentCalls,s.resyncs,s.failures,s.hwndW,s.hwndH,s.swapW,s.swapH,s.cachedW,s.cachedH);fflush(f);return true;}pump(20);}while(GetTickCount()-st<timeout);
    fwprintf(f,L"RC51_LIVE=FAIL installed=%u presents=%llu failures=%llu\n",s.installed,s.presentCalls,s.failures);fflush(f);return false;
}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR cmd,int){
    const bool direct=cmd&&wcsstr(cmd,L"direct");FILE*f=nullptr;_wfopen_s(&f,direct?L"RC56_DEFINITIVE_DIRECT.txt":L"RC56_DEFINITIVE_STRESS.txt",L"wb");if(!f)return 90;
    RegBackup rb{};backup_pref(rb);if(!set_pref(direct?1u:0u)){fclose(f);return 10;}
    g_brush=CreateSolidBrush(RGB(18,18,18));WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_brush;wc.lpszClassName=L"PTAR_RC56_DEFINITIVE_GAME";RegisterClassW(&wc);DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,1280,720};AdjustWindowRectEx(&wr,style,FALSE,0);HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,20,20,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);if(!game){restore_pref(rb);fclose(f);return 11;}ShowWindow(game,SW_SHOW);UpdateWindow(game);pump(30);
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=1280;sd.BufferDesc.Height=720;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferDesc.RefreshRate.Numerator=60;sd.BufferDesc.RefreshRate.Denominator=1;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=game;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;ID3D11Device*dev=nullptr;ID3D11DeviceContext*ctx=nullptr;IDXGISwapChain*swap=nullptr;D3D_FEATURE_LEVEL fl{};HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);if(FAILED(hr)){restore_pref(rb);fclose(f);return 12;}ID3D11Texture2D*bb=nullptr;ID3D11RenderTargetView*rtv=nullptr;if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv))){restore_pref(rb);fclose(f);return 13;}rel(bb);Driver d{ctx,swap,rtv};for(unsigned i=0;i<120;++i)if(!d.frame()){restore_pref(rb);fclose(f);return 14;}
    HWND presenter=wait_presenter();HMODULE carrier=GetModuleHandleW(L"ptar_borderless.dll");if(!presenter||!carrier){restore_pref(rb);fclose(f);return 15;}auto qm=(QueryModeFn)GetProcAddress(carrier,"PTAR_BorderlessQueryMode");auto qs=(QuerySyncFn)GetProcAddress(carrier,"PTAR_RC51_QueryPresenterSync");if(!qm||!qs){restore_pref(rb);fclose(f);return 16;}

    // First prove the controller is installed in the requested initial mode.
    if(!wait_mode(d,qm,qs,game,presenter,direct,f,direct?L"DIRECT_INITIAL":L"INITIAL_WINDOWED",25000)){restore_pref(rb);fclose(f);return 17;}

    if(direct){
        // Direct Borderless gives P1U46 the unblocked native input envelope. Wait
        // for its own completion marker, then require RC51 to be live too.
        if(!wait_runtime_marker(d,f,50000)){restore_pref(rb);fclose(f);return 20;}
        if(!wait_rc51_live(d,qs,f,20000)){restore_pref(rb);fclose(f);return 21;}
        if(!set_pref(0)||!wait_mode(d,qm,qs,game,presenter,false,f,L"DIRECT_TO_WINDOWED",20000)){restore_pref(rb);fclose(f);return 22;}
        if(!set_pref(1)||!wait_mode(d,qm,qs,game,presenter,true,f,L"DIRECT_BACK_BORDERLESS",20000)){restore_pref(rb);fclose(f);return 23;}
        fwprintf(f,L"RC56_DEFINITIVE_DIRECT=PASS presenter_only=1 game_style_windowed=1 p1u46_complete=1 rc51_live=1\n");fflush(f);
    }else{
        // Priming transition is intentional. On the hosted 1024x768 desktop,
        // RC46 Windowed legitimately clamps P1U46's native-input recovery. RC56
        // Borderless removes only that Windowed clamp and never invokes legacy
        // RC38 game geometry. Let P1U46 complete before the long stress phase.
        if(!set_pref(1)||!wait_mode(d,qm,qs,game,presenter,true,f,L"PRIME_BORDERLESS",20000)){restore_pref(rb);fclose(f);return 30;}
        if(!wait_runtime_marker(d,f,50000)){restore_pref(rb);fclose(f);return 31;}
        if(!wait_rc51_live(d,qs,f,20000)){restore_pref(rb);fclose(f);return 32;}
        if(!set_pref(0)||!wait_mode(d,qm,qs,game,presenter,false,f,L"PRIME_WINDOWED",20000)){restore_pref(rb);fclose(f);return 33;}

        constexpr unsigned cycles=80;
        for(unsigned i=0;i<cycles;++i){
            if(!set_pref(1)||!wait_mode(d,qm,qs,game,presenter,true,f,L"BORDERLESS",12000)){fwprintf(f,L"FAIL_CYCLE=%u phase=B\n",i);restore_pref(rb);fclose(f);return 34;}
            if(!set_pref(0)||!wait_mode(d,qm,qs,game,presenter,false,f,L"WINDOWED",12000)){fwprintf(f,L"FAIL_CYCLE=%u phase=W\n",i);restore_pref(rb);fclose(f);return 35;}
        }
        ModeState ms{};ms.size=sizeof(ms);qm(&ms);SyncState ss{};ss.size=sizeof(ss);qs(&ss);Geometry gg{};geom(game,gg);log_geom(f,L"GAME_FINAL",gg);
        if(ms.borderlessActive||ms.transitionsToBorderless<cycles+1u||ms.transitionsToWindowed<cycles+1u||!ss.installed||ss.failures||ss.resyncing||!game_style_windowed(gg.style)){restore_pref(rb);fclose(f);return 36;}
        fwprintf(f,L"FINAL toB=%llu toW=%llu follows=%llu coercion=%llu presenterClamps=%llu rejectedSaves=%llu rc51Presents=%llu rc51Resyncs=%llu rc51Failures=%llu\n",ms.transitionsToBorderless,ms.transitionsToWindowed,ms.presenterFollows,ms.gameCoercionClamps,ms.presenterClamps,ms.rejectedWindowSaves,ss.presentCalls,ss.resyncs,ss.failures);fflush(f);
        fwprintf(f,L"RC56_DEFINITIVE_STRESS=PASS cycles=%u presenter_only=1 game_style_windowed=1 p1u46_complete=1 rc51_live=1\n",cycles);fflush(f);
    }

    // Return to the field-good Windowed policy before destroying the synthetic
    // game window. Do not unload the carrier while it owns WndProc subclasses.
    set_pref(0);wait_mode(d,qm,qs,game,presenter,false,f,L"TEARDOWN_WINDOWED",8000);for(unsigned i=0;i<8;++i)d.frame();
    rel(rtv);rel(swap);rel(ctx);rel(dev);DestroyWindow(game);DeleteObject(g_brush);restore_pref(rb);fclose(f);return 0;
}
