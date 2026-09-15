#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static HBRUSH g_brush=nullptr;
struct RegBackup{bool existed=false;DWORD value=0;};
struct SyncState{UINT size,installed,resyncing;unsigned long long presentCalls,mismatchChecks,resyncs,failures,transitionSkips,zeroClientSkips,swapReplacements;UINT hwndW,hwndH,swapW,swapH,cachedW,cachedH;};
struct ModeState{UINT size,installed,borderlessActive,presenterVisible;ULONG_PTR gameStyle,presenterStyle;unsigned long long transitionsToWindowed,transitionsToBorderless;LONG targetLeft,targetTop,targetRight,targetBottom,windowStylePreference;unsigned long long presenterFollows;};
using QuerySyncFn=int (WINAPI*)(SyncState*);
using QueryModeFn=int (WINAPI*)(ModeState*);

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect((HDC)w,&r,g_brush);return 1;}return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms=1){DWORD st=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-st<ms);}
static void backup_pref(RegBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void restore_pref(const RegBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
template<class T>static void rel(T*& p){if(p){p->Release();p=nullptr;}}
static void client_size(HWND h,UINT& w,UINT& hh){RECT c{};if(GetClientRect(h,&c)){w=(UINT)(c.right-c.left);hh=(UINT)(c.bottom-c.top);}else w=hh=0;}
static HWND wait_presenter(unsigned timeout=12000){DWORD st=GetTickCount();do{HWND h=FindWindowW(L"Win81USRPresenterV041",nullptr);if(h&&IsWindow(h))return h;pump(5);}while(GetTickCount()-st<timeout);return nullptr;}
static bool resize_client(HWND game,UINT cw,UINT ch){DWORD style=(DWORD)GetWindowLongPtrW(game,GWL_STYLE),ex=(DWORD)GetWindowLongPtrW(game,GWL_EXSTYLE);RECT r{0,0,(LONG)cw,(LONG)ch};if(!AdjustWindowRectEx(&r,style,FALSE,ex))return false;RECT cur{};GetWindowRect(game,&cur);return SetWindowPos(game,nullptr,cur.left,cur.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE;}

static constexpr uintptr_t kPresenterSwapchain=0x02C7DFC8u,kPresenterCachedW=0x02C7E000u,kPresenterCachedH=0x02C7E004u;
static bool physical_contract(HMODULE runtime,HWND presenter,UINT& hw,UINT& hh,UINT& sw,UINT& sh,UINT& cw,UINT& ch){
    client_size(presenter,hw,hh);auto base=(BYTE*)runtime;IDXGISwapChain* sc=*reinterpret_cast<IDXGISwapChain**>(base+kPresenterSwapchain);if(!sc)return false;DXGI_SWAP_CHAIN_DESC d{};if(FAILED(sc->GetDesc(&d)))return false;sw=d.BufferDesc.Width;sh=d.BufferDesc.Height;cw=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedW);ch=*reinterpret_cast<volatile UINT*>(base+kPresenterCachedH);return true;
}
struct Driver{ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;ID3D11RenderTargetView* rtv=nullptr;FILE* log=nullptr;const float color[4]={0.15f,0.55f,0.95f,1.0f};bool frame(){ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->ClearRenderTargetView(rtv,color);HRESULT hr=swap->Present(0,0);pump();return SUCCEEDED(hr);}};

static bool game_unchanged(HWND game,LONG_PTR style0,UINT wantW,UINT wantH,RECT rect0,bool checkRect,FILE* log,const wchar_t* tag){
    UINT w=0,h=0;client_size(game,w,h);LONG_PTR s=GetWindowLongPtrW(game,GWL_STYLE);RECT r{};GetWindowRect(game,&r);const bool rectOk=!checkRect||(r.left==rect0.left&&r.top==rect0.top&&r.right==rect0.right&&r.bottom==rect0.bottom);const bool ok=w==wantW&&h==wantH&&s==style0&&rectOk;fwprintf(log,L"GAME_%ls=%ls client=%ux%u style=0x%llX rect=%ld,%ld,%ld,%ld\n",tag,ok?L"PASS":L"FAIL",w,h,(unsigned long long)s,r.left,r.top,r.right,r.bottom);fflush(log);return ok;
}

static bool drive_contract(Driver& d,HMODULE runtime,HWND presenter,QuerySyncFn qs,QueryModeFn qm,UINT wantW,UINT wantH,bool wantBorderless,HWND game,LONG_PTR style0,UINT gameW,UINT gameH,RECT rect0,bool checkRect,unsigned timeout,const wchar_t* tag){
    DWORD st=GetTickCount();SyncState ss{};ModeState ms{};UINT hw=0,hh=0,sw=0,sh=0,cw=0,ch=0;
    do{
        if(!d.frame())return false;
        ss={};ss.size=sizeof(ss);ms={};ms.size=sizeof(ms);qs(&ss);qm(&ms);
        if(physical_contract(runtime,presenter,hw,hh,sw,sh,cw,ch)&&hw==wantW&&hh==wantH&&sw==wantW&&sh==wantH&&cw==wantW&&ch==wantH&&ss.installed&&ss.failures==0&&ms.installed&&((ms.borderlessActive!=0)==wantBorderless)){
            if(!game_unchanged(game,style0,gameW,gameH,rect0,checkRect,d.log,tag))return false;
            fwprintf(d.log,L"CONTRACT_%ls=PASS hwnd=%ux%u swap=%ux%u cached=%ux%u borderless=%u resyncs=%llu failures=%llu\n",tag,hw,hh,sw,sh,cw,ch,ms.borderlessActive,ss.resyncs,ss.failures);fflush(d.log);return true;
        }
    }while(GetTickCount()-st<timeout);
    fwprintf(d.log,L"CONTRACT_%ls=FAIL hwnd=%ux%u swap=%ux%u cached=%ux%u syncInstalled=%u failures=%llu modeInstalled=%u borderless=%u\n",tag,hw,hh,sw,sh,cw,ch,ss.installed,ss.failures,ms.installed,ms.borderlessActive);fflush(d.log);return false;
}

int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR cmd,int){
    const bool direct=(cmd&&wcsstr(cmd,L"direct")!=nullptr);FILE* log=nullptr;_wfopen_s(&log,direct?L"RC56_P1U46_DIRECT.txt":L"RC56_P1U46_STRESS.txt",L"wb");if(!log)return 90;
    constexpr UINT outW=1920,outH=1080,renderW=1280,renderH=720;RegBackup rb{};backup_pref(rb);if(!set_pref(direct?1u:0u)){fclose(log);return 10;}fwprintf(log,L"RC56_P1U46_BEGIN output=%ux%u render=%ux%u direct=%u\n",outW,outH,renderW,renderH,direct?1u:0u);fflush(log);
    g_brush=CreateSolidBrush(RGB(20,20,20));WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.hbrBackground=g_brush;wc.lpszClassName=L"PTAR_RC56_SYNTH_GAME";RegisterClassW(&wc);DWORD style=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,(LONG)renderW,(LONG)renderH};AdjustWindowRectEx(&wr,style,FALSE,0);HWND game=CreateWindowExW(0,wc.lpszClassName,L"Warhammer: Inquisitor - Martyr",style,20,20,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);if(!game){restore_pref(rb);fclose(log);return 11;}ShowWindow(game,SW_SHOW);UpdateWindow(game);pump(30);LONG_PTR style0=GetWindowLongPtrW(game,GWL_STYLE);RECT rect0{};GetWindowRect(game,&rect0);
    DXGI_SWAP_CHAIN_DESC sd{};sd.BufferDesc.Width=renderW;sd.BufferDesc.Height=renderH;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferDesc.RefreshRate.Numerator=60;sd.BufferDesc.RefreshRate.Denominator=1;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.OutputWindow=game;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;IDXGISwapChain* swap=nullptr;D3D_FEATURE_LEVEL fl{};HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dev,&fl,&ctx);if(FAILED(hr)){fwprintf(log,L"CREATE_FAIL hr=0x%08X\n",(unsigned)hr);restore_pref(rb);fclose(log);return 12;}ID3D11Texture2D* bb=nullptr;ID3D11RenderTargetView* rtv=nullptr;if(FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&bb))||FAILED(dev->CreateRenderTargetView(bb,nullptr,&rtv))){restore_pref(rb);fclose(log);return 13;}rel(bb);Driver d{ctx,swap,rtv,log};for(unsigned i=0;i<120;++i)if(!d.frame()){restore_pref(rb);fclose(log);return 14;}
    HWND presenter=wait_presenter();HMODULE runtime=GetModuleHandleW(L"d3d11.dll"),carrier=GetModuleHandleW(L"ptar_borderless.dll");if(!presenter||!runtime||!carrier){fwprintf(log,L"MODULE_FAIL presenter=%p runtime=%p carrier=%p\n",presenter,runtime,carrier);restore_pref(rb);fclose(log);return 15;}auto qs=(QuerySyncFn)GetProcAddress(carrier,"PTAR_RC51_QueryPresenterSync");auto qm=(QueryModeFn)GetProcAddress(carrier,"PTAR_BorderlessQueryMode");if(!qs||!qm){restore_pref(rb);fclose(log);return 16;}
    if(direct){
        if(!drive_contract(d,runtime,presenter,qs,qm,outW,outH,true,game,style0,renderW,renderH,rect0,true,15000,L"DIRECT_BORDERLESS")){restore_pref(rb);fclose(log);return 20;}
        if(!set_pref(0)||!drive_contract(d,runtime,presenter,qs,qm,renderW,renderH,false,game,style0,renderW,renderH,rect0,true,12000,L"DIRECT_TO_WINDOWED")){restore_pref(rb);fclose(log);return 21;}
        if(!set_pref(1)||!drive_contract(d,runtime,presenter,qs,qm,outW,outH,true,game,style0,renderW,renderH,rect0,true,12000,L"DIRECT_BACK_BORDERLESS")){restore_pref(rb);fclose(log);return 22;}
        fwprintf(log,L"RC56_P1U46_DIRECT=PASS game_untouched=1 presenter_only=1 exact_x15=1\n");fflush(log);
    }else{
        if(!drive_contract(d,runtime,presenter,qs,qm,renderW,renderH,false,game,style0,renderW,renderH,rect0,true,15000,L"INITIAL_WINDOWED")){restore_pref(rb);fclose(log);return 30;}
        constexpr unsigned cycles=80;
        for(unsigned i=0;i<cycles;++i){
            if(!set_pref(1)||!drive_contract(d,runtime,presenter,qs,qm,outW,outH,true,game,style0,renderW,renderH,rect0,true,9000,L"BORDERLESS")){fwprintf(log,L"FAIL_CYCLE=%u phase=borderless\n",i);restore_pref(rb);fclose(log);return 31;}
            if(!set_pref(0)||!drive_contract(d,runtime,presenter,qs,qm,renderW,renderH,false,game,style0,renderW,renderH,rect0,true,9000,L"WINDOWED")){fwprintf(log,L"FAIL_CYCLE=%u phase=windowed\n",i);restore_pref(rb);fclose(log);return 32;}
        }
        fwprintf(log,L"MODE_CYCLES=%u PASS\n",cycles);fflush(log);
        if(!resize_client(game,960,540)){restore_pref(rb);fclose(log);return 33;}RECT resized{};GetWindowRect(game,&resized);if(!drive_contract(d,runtime,presenter,qs,qm,960,540,false,game,style0,960,540,resized,true,12000,L"WINDOWED_RESIZE")){restore_pref(rb);fclose(log);return 34;}
        if(!resize_client(game,renderW,renderH)){restore_pref(rb);fclose(log);return 35;}RECT restored{};GetWindowRect(game,&restored);if(!drive_contract(d,runtime,presenter,qs,qm,renderW,renderH,false,game,style0,renderW,renderH,restored,true,12000,L"WINDOWED_RESTORE_SIZE")){restore_pref(rb);fclose(log);return 36;}
        if(!set_pref(1)||!drive_contract(d,runtime,presenter,qs,qm,outW,outH,true,game,style0,renderW,renderH,restored,true,12000,L"POST_RESIZE_BORDERLESS")){restore_pref(rb);fclose(log);return 37;}
        if(!set_pref(0)||!drive_contract(d,runtime,presenter,qs,qm,renderW,renderH,false,game,style0,renderW,renderH,restored,true,12000,L"FINAL_WINDOWED")){restore_pref(rb);fclose(log);return 38;}
        SyncState ss{};ss.size=sizeof(ss);qs(&ss);ModeState ms{};ms.size=sizeof(ms);qm(&ms);fwprintf(log,L"FINAL syncInstalled=%u resyncs=%llu failures=%llu transitionSkips=%llu replacements=%llu modeInstalled=%u borderless=%u toB=%llu toW=%llu follows=%llu\n",ss.installed,ss.resyncs,ss.failures,ss.transitionSkips,ss.swapReplacements,ms.installed,ms.borderlessActive,ms.transitionsToBorderless,ms.transitionsToWindowed,ms.presenterFollows);fflush(log);if(!ss.installed||ss.failures||ss.resyncs<cycles*2u+3u||ms.borderlessActive){restore_pref(rb);fclose(log);return 39;}
        fwprintf(log,L"RC56_P1U46_STRESS=PASS cycles=%u game_untouched=1 presenter_only=1 exact_x15=1 resize=PASS\n",cycles);fflush(log);
    }
    set_pref(0);for(unsigned i=0;i<10;++i)d.frame();rel(rtv);rel(swap);rel(ctx);rel(dev);DestroyWindow(game);DeleteObject(g_brush);restore_pref(rb);fclose(log);return 0;
}
