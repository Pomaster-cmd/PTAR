#include "../rc38/ptar_borderless_rc38.cpp"
#include "ptar_borderless_mode_policy.h"
#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"

static volatile LONG g_rc43Starting=0;
static volatile LONG g_rc43Installed=0;
static volatile LONG64 g_rc43ToWindowed=0;
static volatile LONG64 g_rc43ToBorderless=0;
static HMODULE g_rc43Runtime=nullptr;

struct PTARBorderlessModeState {
    UINT size;
    UINT installed;
    UINT borderlessActive;
    UINT presenterVisible;
    ULONG_PTR gameStyle;
    ULONG_PTR presenterStyle;
    unsigned long long transitionsToWindowed;
    unsigned long long transitionsToBorderless;
};

static void rc43_starting_clear() noexcept { InterlockedExchange(&g_rc43Starting,0); }

static void rc43_log_style(const char* tag,LONG_PTR oldStyle,LONG_PTR newStyle) noexcept {
    logfmt(tag,oldStyle,newStyle,InterlockedCompareExchange(&g_active,0,0),InterlockedCompareExchange(&g_rc43Installed,0,0));
}

static void rc43_hide_presenter() noexcept {
    if(!IsWindow(g_presenter)) return;
    InterlockedExchange(&g_internal,1);
    SetWindowPos(g_presenter,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_HIDEWINDOW);
    InterlockedExchange(&g_internal,0);
}

static void rc43_enter_windowed() noexcept {
    if(!InterlockedCompareExchange(&g_active,0,0)) return;
    InterlockedExchange(&g_active,0);
    InterlockedExchange(&g_logicalCapture,0);
    if(GetCapture()==g_presenter) ReleaseCapture();
    ClipCursor(nullptr);
    rc43_hide_presenter();
    InterlockedIncrement64(&g_rc43ToWindowed);
    logline("RC43_MODE=WINDOWED borderless authority passive; game geometry/style native");
}

static void rc43_enter_borderless_pre() noexcept {
    if(InterlockedCompareExchange(&g_active,0,0)) return;
    InterlockedExchange(&g_active,1);
    if(IsWindow(g_presenter)) ShowWindow(g_presenter,SW_SHOWNOACTIVATE);
    InterlockedIncrement64(&g_rc43ToBorderless);
    logline("RC43_MODE=BORDERLESS authority active");
}

static LRESULT CALLBACK RC43GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    const bool internal=InterlockedCompareExchange(&g_internal,0,0)!=0;

    // The requested style is visible here before the inherited RC40 GameProc can
    // clamp it back to WS_POPUP. This is therefore the authoritative mode switch.
    if(!internal && m==WM_STYLECHANGING && l && w==GWL_STYLE){
        STYLESTRUCT* ss=reinterpret_cast<STYLESTRUCT*>(l);
        rc43_log_style("RC43_STYLE_CHANGING",ss->styleOld,ss->styleNew);
        const ptar_rc43::RequestedWindowMode requested=ptar_rc43::classify_style(ss->styleNew);
        if(requested==ptar_rc43::RequestedWindowMode::Windowed && InterlockedCompareExchange(&g_active,0,0)){
            rc43_enter_windowed();
            return call_next(g_gameNext,h,m,w,l);
        }
        if(requested==ptar_rc43::RequestedWindowMode::Borderless && !InterlockedCompareExchange(&g_active,0,0)){
            rc43_enter_borderless_pre();
            return GameProc(h,m,w,l);
        }
    }

    // Defensive second observation point. Some engines/frameworks complete a style
    // mutation through a path where the useful intent is only stable at STYLECHANGED.
    if(!internal && m==WM_STYLECHANGED && w==GWL_STYLE){
        const LONG_PTR now=GetWindowLongPtrW(h,GWL_STYLE);
        rc43_log_style("RC43_STYLE_CHANGED",now,now);
        const ptar_rc43::RequestedWindowMode requested=ptar_rc43::classify_style(now);
        if(requested==ptar_rc43::RequestedWindowMode::Windowed){
            if(InterlockedCompareExchange(&g_active,0,0)) rc43_enter_windowed();
            return call_next(g_gameNext,h,m,w,l);
        }
        if(requested==ptar_rc43::RequestedWindowMode::Borderless){
            if(!InterlockedCompareExchange(&g_active,0,0)) rc43_enter_borderless_pre();
            const LRESULT r=GameProc(h,m,w,l);
            enforce_geometry();
            return r;
        }
    }

    // If a framework mutates style and geometry as one transaction, WINDOWPOSCHANGING
    // is a final safe fallback for borderless re-entry after the new style is committed.
    if(!internal && !InterlockedCompareExchange(&g_active,0,0) && m==WM_WINDOWPOSCHANGING){
        const LONG_PTR now=GetWindowLongPtrW(h,GWL_STYLE);
        if(ptar_rc43::is_borderless_request(now)){
            rc43_log_style("RC43_BORDERLESS_REENTRY_WINDOWPOS",now,now);
            rc43_enter_borderless_pre();
            return GameProc(h,m,w,l);
        }
    }

    if(InterlockedCompareExchange(&g_active,0,0)) return GameProc(h,m,w,l);
    return call_next(g_gameNext,h,m,w,l);
}

static DWORD WINAPI RC43StableWorker(LPVOID){
    if(!IsWindow(g_game)||!IsWindow(g_presenter)){rc43_starting_clear();return 20;}
    LONG_PTR cur=get_wndproc(g_game);
    if(!cur){logline("FAIL RC43 stable game proc unavailable; fail-open");rc43_starting_clear();return 21;}
    g_gameNext=reinterpret_cast<WNDPROC>(cur);
    if(!set_wndproc(g_game,RC43GameProc)){logline("FAIL RC43 game subclass install");rc43_starting_clear();return 22;}
    g_presenterNext=reinterpret_cast<WNDPROC>(get_wndproc(g_presenter));
    if(!g_presenterNext||!set_wndproc(g_presenter,PresenterProc)){
        set_wndproc(g_game,g_gameNext);logline("FAIL RC43 presenter subclass install");rc43_starting_clear();return 23;
    }
    patch_iat(GetModuleHandleW(nullptr));
    InterlockedExchange(&g_rc43Installed,1);

    const LONG_PTR style=GetWindowLongPtrW(g_game,GWL_STYLE);
    const ptar_rc43::RequestedWindowMode initial=ptar_rc43::classify_style(style);
    if(initial==ptar_rc43::RequestedWindowMode::Windowed){
        InterlockedExchange(&g_active,0);
        rc43_hide_presenter();
        logline("RC43_INITIAL_MODE=WINDOWED; borderless passive");
    } else {
        InterlockedExchange(&g_active,1);
        enforce_geometry();
        logline(initial==ptar_rc43::RequestedWindowMode::Borderless ?
            "RC43_INITIAL_MODE=BORDERLESS" : "RC43_INITIAL_MODE=AMBIGUOUS->BORDERLESS_COMPAT");
    }

    rc43_starting_clear();
    UINT gw=0,gh=0,pw=0,ph=0;client_size(g_game,gw,gh);client_size(g_presenter,pw,ph);
    logfmt("ACTIVE_RC43_COEXIST game/presenter",(LONG_PTR)((gw<<16)^gh),(LONG_PTR)((pw<<16)^ph),g_iatHooks,InterlockedCompareExchange(&g_active,0,0));
    if(g_rc43Runtime) RC41B_StartBootstrap(g_self,g_rc43Runtime);
    return 0;
}

static int rc43_attach(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    if(InterlockedCompareExchange(&g_rc43Installed,0,0)) return 1;
    if(InterlockedCompareExchange(&g_rc43Starting,1,0)) return 2;
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)||!renderW||!renderH||!outputW||!outputH||renderW>outputW||renderH>outputH){
        logline("FAIL RC43 invalid attach args");rc43_starting_clear();return -10;
    }
    HMONITOR hm=MonitorFromWindow(presenter,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi)){logline("FAIL RC43 monitor resolve");rc43_starting_clear();return -11;}
    UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);
    if(mw!=outputW||mh!=outputH){logfmt("FAIL RC43 output/monitor mismatch",mw,mh,outputW,outputH);rc43_starting_clear();return -12;}
    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=mi.rcMonitor;
    g_initialGameProc=reinterpret_cast<WNDPROC>(get_wndproc(game));
    g_presenterStyle0=GetWindowLongPtrW(presenter,GWL_STYLE);g_presenterExStyle0=GetWindowLongPtrW(presenter,GWL_EXSTYLE);
    if(!g_initialGameProc){logline("FAIL RC43 initial game proc");rc43_starting_clear();return -13;}
    HANDLE th=CreateThread(nullptr,0,RC43StableWorker,nullptr,0,nullptr);
    if(!th){logline("FAIL RC43 worker create");rc43_starting_clear();return -14;}
    CloseHandle(th);logfmt("ATTACH_RC43_COEXIST_PENDING",renderW,renderH,outputW,outputH);return 0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    return rc43_attach(game,presenter,renderW,renderH,outputW,outputH);
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessQueryMode(PTARBorderlessModeState* s){
    if(!s||s->size<sizeof(PTARBorderlessModeState)) return -1;
    ZeroMemory(reinterpret_cast<BYTE*>(s)+sizeof(UINT),sizeof(PTARBorderlessModeState)-sizeof(UINT));
    s->installed=(UINT)InterlockedCompareExchange(&g_rc43Installed,0,0);
    s->borderlessActive=(UINT)InterlockedCompareExchange(&g_active,0,0);
    s->presenterVisible=(IsWindow(g_presenter)&&IsWindowVisible(g_presenter))?1u:0u;
    if(IsWindow(g_game)) s->gameStyle=(ULONG_PTR)GetWindowLongPtrW(g_game,GWL_STYLE);
    if(IsWindow(g_presenter)) s->presenterStyle=(ULONG_PTR)GetWindowLongPtrW(g_presenter,GWL_STYLE);
    s->transitionsToWindowed=(unsigned long long)InterlockedCompareExchange64(&g_rc43ToWindowed,0,0);
    s->transitionsToBorderless=(unsigned long long)InterlockedCompareExchange64(&g_rc43ToBorderless,0,0);
    return 0;
}

static bool rc43_runtime_layout_ok(HMODULE runtime,BYTE*& base,IMAGE_NT_HEADERS64*& nt){
    if(!runtime)return false;base=(BYTE*)runtime;
    auto* dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return false;
    nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    if(nt->OptionalHeader.SizeOfImage<0x02C7E010u)return false;
    FARPROC exported=GetProcAddress(runtime,"D3D11CreateDeviceAndSwapChain");
    if(exported!=(FARPROC)(base+0x000021C0u))return false;
    static const BYTE gameAnchor[]={0x49,0x8B,0x49,0x30,0x48,0x89,0x0D,0xBA,0xC2,0xC3,0x02};
    if(memcmp(base+0x000037FBu,gameAnchor,sizeof(gameAnchor))!=0)return false;
    static const BYTE restoredPresenterLog[]={0xE8,0xAD,0x70,0xFF,0xFF};
    if(memcmp(base+0x0000C2FEu,restoredPresenterLog,sizeof(restoredPresenterLog))!=0)return false;
    BYTE* call=base+0x00003806u;if(call[0]!=0xE8)return false;
    INT32 disp=0;memcpy(&disp,call+1,sizeof(disp));BYTE* target=call+5+disp;
    if(target!=base+0x03500500u)return false;
    static const BYTE loaderCallsP1U46[]={0x49,0x8D,0x83,0x60,0xC4,0x00,0x00};
    if(memcmp(base+0x03500517u,loaderCallsP1U46,sizeof(loaderCallsP1U46))!=0)return false;
    auto* sh=IMAGE_FIRST_SECTION(nt);bool rc38=false;
    for(WORD i=0;i<nt->FileHeader.NumberOfSections;++i){if(memcmp(sh[i].Name,".rc38",5)==0&&sh[i].VirtualAddress==0x03500000u){rc38=true;break;}}
    return rc38;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_rc43Installed,0,0))return 1;
    BYTE* base=nullptr;IMAGE_NT_HEADERS64* nt=nullptr;
    if(!rc43_runtime_layout_ok(runtime,base,nt)){logline("FAIL RC43 runtime layout guard; fail-open");return -20;}
    HWND game=*(HWND*)(base+0x02C3FAC0u);HWND presenter=*(HWND*)(base+0x02C7DFE0u);
    UINT renderW=*(UINT*)(base+0x02C3FB78u),renderH=*(UINT*)(base+0x02C3FB7Cu);
    UINT outputW=*(UINT*)(base+0x0004B040u),outputH=*(UINT*)(base+0x0004B044u);
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)){logline("FAIL RC43 windows unavailable; fail-open");return -21;}
    if(!renderW||!renderH||!outputW||!outputH){logfmt("FAIL RC43 runtime dimensions",renderW,renderH,outputW,outputH);return -22;}
    LONG_PTR proc=get_wndproc(game);
    if(proc!=(LONG_PTR)(base+0x0000DC50u)){logfmt("FAIL RC43 expected P1U46 WndProc not active",proc,(LONG_PTR)(base+0x0000DC50u));return -23;}
    g_rc43Runtime=runtime;
    logfmt("AUTO_START_RC43_COEXIST geometry",renderW,renderH,outputW,outputH);
    return rc43_attach(game,presenter,renderW,renderH,outputW,outputH);
}
