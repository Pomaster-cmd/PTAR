#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
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
using QueryModeFn=int (WINAPI*)(ModeState*);

struct Geometry{UINT cw=0,ch=0;LONG_PTR style=0;RECT outer{};RECT clientScreen{};};
static bool same_rect(const RECT&a,const RECT&b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static bool same_geom(const Geometry&a,const Geometry&b){return a.cw==b.cw&&a.ch==b.ch&&a.style==b.style&&same_rect(a.outer,b.outer)&&same_rect(a.clientScreen,b.clientScreen);}
static bool geom(HWND h,Geometry&g){if(!IsWindow(h))return false;RECT c{};if(!GetClientRect(h,&c)||!GetWindowRect(h,&g.outer))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b)||b.x<=a.x||b.y<=a.y)return false;g.cw=(UINT)(c.right-c.left);g.ch=(UINT)(c.bottom-c.top);g.style=GetWindowLongPtrW(h,GWL_STYLE);g.clientScreen={a.x,a.y,b.x,b.y};return g.cw&&g.ch;}
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_brush);return 1;}return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms=1){DWORD st=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-st<ms);}
static void backup_pref(RegBackup&b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void restore_pref(const RegBackup&b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
template<class T>static void rel(T*&p){if(p){p->Release();p=nullptr;}}
static HWND wait_presenter(unsigned timeout=12000){DWORD st=GetTickCount();do{HWND h=FindWindowW(L"Win81USRPresenterV041",nullptr);if(h&&IsWindow(h))return h;pump(5);}while(GetTickCount()-st<timeout);return nullptr;}

struct Driver{ID3D11DeviceContext*ctx=nullptr;IDXGISwapChain*swap=nullptr;ID3D11RenderTargetView*rtv=nullptr;const float color[4]={0.2f,0.6f,0.9f,1.0f};bool frame(){ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->ClearRenderTargetView(rtv,color);HRESULT hr=swap->Present(0,0);pump(2);return SUCCEEDED(hr);}};

static void log_geom(FILE*f,const wchar_t*tag,const Geometry&g){fwprintf(f,L"%ls client=%ux%u style=0x%llX outer=%ld,%ld,%ld,%ld screen=%ld,%ld,%ld,%ld\n",tag,g.cw,g.ch,(unsigned long long)g.style,g.outer.left,g.outer.top,g.outer.right,g.outer.bottom,g.clientScreen.left,g.clientScreen.top,g.clientScreen.right,g.clientScreen.bottom);fflush(f);}

static bool wait_installed_stable(Driver&d,QueryModeFn q,HWND game,bool wantBorderless,Geometry&base,FILE*f,unsigned timeout=30000){
    DWORD st=GetTickCount();Geometry last{};bool have=false;unsigned stable=0;ModeState ms{};
    do{
        if(!d.frame())return false;ms={};ms.size=sizeof(ms);if(q(&ms)!=0){pump(20);continue;}
        Geometry now{};if(!ms.installed||((ms.borderlessActive!=0)!=wantBorderless)||!geom(game,now)){stable=0;pump(20);continue;}
        if(have&&same_geom(now,last))++stable;else stable=0;last=now;have=true;
        if(stable>=8){base=now;log_geom(f,L"GAME_BASELINE",base);fwprintf(f,L"MODE_BASELINE installed=%u borderless=%u toB=%llu toW=%llu follows=%llu\n",ms.installed,ms.borderlessActive,ms.transitionsToBorderless,ms.transitionsToWindowed,ms.presenterFollows);fflush(f);return true;}
        pump(80);
    }while(GetTickCount()-st<timeout);
    fwprintf(f,L"WAIT_STABLE_FAIL installed=%u borderless=%u stable=%u\n",ms.installed,ms.borderlessActive,stable);fflush(f);return false;
}

static bool presenter_matches(HWND presenter,HWND game,bool borderless){Geometry p{},g{};if(!geom(presenter,p)||!geom(game,g))return false;if(borderless)return p.cw==1920&&p.ch==1080;return p.cw==g.cw&&p.ch==g.ch&&same_rect(p.clientScreen,g.clientScreen);}

static bool wait_transition(Driver&d,QueryModeFn q,HWND game,HWND presenter,const Geometry&base,bool borderless,FILE*f,const wchar_t*tag,unsigned timeout=10000){
    DWORD st=GetTickCount();ModeState ms{};Geometry g{};unsigned deviations=0;
    do{
        if(!d.frame())return false;ms={};ms.size=sizeof(ms);q(&ms);if(!geom(game,g))return false;
        if(!same_geom(g,base))++deviations;
        if(ms.installed&&((ms.borderlessActive!=0)==borderless)&&same_geom(g,base)&&presenter_matches(presenter,game,borderless)){
            Geometry p{};geom(presenter,p);fwprintf(f,L"TRANSITION_%ls=PASS borderless=%u game=%ux%u presenter=%ux%u deviations=%u toB=%llu toW=%llu follows=%llu\n",tag,ms.borderlessActive,g.cw,g.ch,p.cw,p.ch,deviations,ms.transitionsToBorderless,ms.transitionsToWindowed,ms.presenterFollows);fflush(f);return true;
        }
        pump(20);
    }while(GetTickCount()-st<timeout);
    Geometry p{};geom(presenter,p);fwprintf(f,L"TRANSITION_%ls=FAIL wanted=%u actual=%u installed=%u game=%ux%u presenter=%ux%u deviations=%u\n",tag,borderless?1u:0u,ms.borderlessActive,ms.installed,g.cw,g.ch,p.cw,p.ch,deviations);fflush(f);return false;
}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR cmd,int){
    const bool direct=cmd&&wcsstr(cmd,L"direct");FILE*f=nullptr;_wfopen_s(&f,direct?L"RC56_CONTROLLER_DIRECT.txt":L"RC56_CONTROLLER_STRESS.txt",L"wb");if(!f)return 90;
    RegBackup rb{};backup_pref(rb);if(!set_pref(direct?1u:0u)){fclose(f);return 10;}
    g_brush=CreateSolidBrush(RGB(18,18,18));WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_brush;wc.lpszClassName=L"PTAR_RC56_CONTROLLER_GAME";RegisterClassW(&wc);DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,1280,720};AdjustWindowRectEx(&wr,style,FALSE,0);HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,20,20,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);if(!game){restore_pref(rb);fclose(f);return 11;}ShowWindow(game,SW_SHOW);UpdateWindow(game);pump(30);
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=1280;sd.BufferDesc.Height=720;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferDesc.RefreshRate.Numerator=60;sd.BufferDesc.RefreshRate.Denominator=1;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=game;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;ID3D11Device*dev=nullptr;ID3D11DeviceContext*ctx=nullptr;IDXGISwapChain*swap=nullptr;D3D_FEATURE_LEVEL fl{};HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);if(FAILED(hr)){restore_pref(rb);fclose(f);return 12;}ID3D11Texture2D*bb=nullptr;ID3D11RenderTargetView*rtv=nullptr;if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv))){restore_pref(rb);fclose(f);return 13;}rel(bb);Driver d{ctx,swap,rtv};for(unsigned i=0;i<120;++i)if(!d.frame()){restore_pref(rb);fclose(f);return 14;}
    HWND presenter=wait_presenter();HMODULE carrier=GetModuleHandleW(L"ptar_borderless.dll");if(!presenter||!carrier){restore_pref(rb);fclose(f);return 15;}auto q=(QueryModeFn)GetProcAddress(carrier,"PTAR_BorderlessQueryMode");if(!q){restore_pref(rb);fclose(f);return 16;}
    Geometry base{};if(!wait_installed_stable(d,q,game,direct,base,f)){restore_pref(rb);fclose(f);return 17;}if(!presenter_matches(presenter,game,direct)){restore_pref(rb);fclose(f);return 18;}
    if(direct){
        fwprintf(f,L"DIRECT_INITIAL=PASS\n");if(!set_pref(0)||!wait_transition(d,q,game,presenter,base,false,f,L"DIRECT_TO_WINDOWED")){restore_pref(rb);fclose(f);return 20;}if(!set_pref(1)||!wait_transition(d,q,game,presenter,base,true,f,L"DIRECT_BACK_BORDERLESS")){restore_pref(rb);fclose(f);return 21;}fwprintf(f,L"RC56_CONTROLLER_DIRECT=PASS game_untouched=1 presenter_only=1 exact_runtime=1\n");fflush(f);
    }else{
        constexpr unsigned cycles=80;for(unsigned i=0;i<cycles;++i){if(!set_pref(1)||!wait_transition(d,q,game,presenter,base,true,f,L"BORDERLESS")){fwprintf(f,L"FAIL_CYCLE=%u phase=B\n",i);restore_pref(rb);fclose(f);return 30;}if(!set_pref(0)||!wait_transition(d,q,game,presenter,base,false,f,L"WINDOWED")){fwprintf(f,L"FAIL_CYCLE=%u phase=W\n",i);restore_pref(rb);fclose(f);return 31;}}
        ModeState ms{};ms.size=sizeof(ms);q(&ms);if(ms.borderlessActive||ms.transitionsToBorderless<cycles||ms.transitionsToWindowed<cycles){restore_pref(rb);fclose(f);return 32;}fwprintf(f,L"RC56_CONTROLLER_STRESS=PASS cycles=%u game_untouched=1 presenter_only=1 exact_runtime=1 toB=%llu toW=%llu follows=%llu coercion=%llu presenterClamps=%llu\n",cycles,ms.transitionsToBorderless,ms.transitionsToWindowed,ms.presenterFollows,ms.gameCoercionClamps,ms.presenterClamps);fflush(f);
    }
    set_pref(0);for(unsigned i=0;i<8;++i)d.frame();rel(rtv);rel(swap);rel(ctx);rel(dev);DestroyWindow(game);DeleteObject(g_brush);restore_pref(rb);fclose(f);return 0;
}
