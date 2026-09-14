#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>

using AttachStableFn=int (WINAPI*)(HWND,HWND,UINT,UINT,UINT,UINT);
using QueryFn=int (WINAPI*)(void*);

struct ModeState {
    UINT size;
    UINT installed;
    UINT borderlessActive;
    UINT presenterVisible;
    ULONG_PTR gameStyle;
    ULONG_PTR presenterStyle;
    unsigned long long transitionsToWindowed;
    unsigned long long transitionsToBorderless;
};

static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static bool query_state(QueryFn query,ModeState& s){s={};s.size=sizeof(s);return query(&s)==0;}
static void print_state(const char* tag,QueryFn query,HWND game,HWND presenter){
    ModeState s{};query_state(query,s);RECT gr{},pr{};GetWindowRect(game,&gr);GetWindowRect(presenter,&pr);
    std::printf("%s installed=%u active=%u visible=%u gameStyle=0x%llx presenterStyle=0x%llx transitions=%llu/%llu gameRect=%ld,%ld,%ld,%ld presenterRect=%ld,%ld,%ld,%ld\n",
        tag,s.installed,s.borderlessActive,s.presenterVisible,(unsigned long long)s.gameStyle,(unsigned long long)s.presenterStyle,
        s.transitionsToWindowed,s.transitionsToBorderless,gr.left,gr.top,gr.right,gr.bottom,pr.left,pr.top,pr.right,pr.bottom);
}
static int fail(int code,const char* what,QueryFn query,HWND game,HWND presenter){
    std::printf("RC44_COEXIST_HOST=FAIL rc=%d what=%s gle=%lu\n",code,what,(unsigned long)GetLastError());
    if(query&&game&&presenter)print_state("FAIL_STATE",query,game,presenter);
    return code;
}
static bool wait_mode(QueryFn query,bool active,unsigned timeoutMs=4000){
    const DWORD start=GetTickCount();
    do{
        ModeState s{};
        if(query_state(query,s) && s.installed && (!!s.borderlessActive)==active) return true;
        MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        Sleep(5);
    }while(GetTickCount()-start<timeoutMs);
    return false;
}
static bool client_is(HWND h,UINT w,UINT hh){RECT r{};return GetClientRect(h,&r)&&UINT(r.right-r.left)==w&&UINT(r.bottom-r.top)==hh;}
static bool outer_is(HWND h,LONG x,LONG y,LONG w,LONG hh){RECT r{};return GetWindowRect(h,&r)&&r.left==x&&r.top==y&&(r.right-r.left)==w&&(r.bottom-r.top)==hh;}
static void request_style(HWND h,LONG_PTR style){
    SetLastError(ERROR_SUCCESS);
    SetWindowLongPtrW(h,GWL_STYLE,style);
    SetWindowPos(h,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
}

int wmain(){
    HINSTANCE inst=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC44_COEXIST_HOST";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 10;

    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,64,64,nullptr,nullptr,inst,nullptr);
    if(!probe)return 11;
    HMONITOR hm=MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi))return 12;
    DestroyWindow(probe);
    const UINT outW=UINT(mi.rcMonitor.right-mi.rcMonitor.left),outH=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);
    const UINT renderW=(outW>=640?640:outW),renderH=(outH>=360?360:outH);
    const LONG_PTR windowed=WS_OVERLAPPEDWINDOW|WS_VISIBLE;
    // Exact field style observed in PTAR_RC43_RESULTS_20260914_115205.zip:
    // 0x14CF0000 -> 0x14000000 on the Windowed -> Borderless switch.
    static_assert((ULONG_PTR)(WS_VISIBLE|WS_CLIPSIBLINGS)==0x14000000ull,"unexpected Win32 style constants");
    const LONG_PTR fieldFrameless=WS_VISIBLE|WS_CLIPSIBLINGS;
    const LONG initialX=mi.rcMonitor.left+47,initialY=mi.rcMonitor.top+39,initialW=533,initialH=401;

    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game",windowed,initialX,initialY,initialW,initialH,nullptr,nullptr,inst,nullptr);
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,game,nullptr,inst,nullptr);
    if(!game||!presenter)return 13;

    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return 14;
    auto attach=reinterpret_cast<AttachStableFn>(GetProcAddress(dll,"PTAR_BorderlessAttachStable"));
    auto query=reinterpret_cast<QueryFn>(GetProcAddress(dll,"PTAR_BorderlessQueryMode"));
    if(!attach||!query)return 15;
    if(attach(game,presenter,renderW,renderH,outW,outH)!=0)return fail(16,"attach",query,game,presenter);

    if(!wait_mode(query,false))return fail(17,"initial-windowed-active",query,game,presenter);
    if(!outer_is(game,initialX,initialY,initialW,initialH))return fail(18,"initial-windowed-geometry",query,game,presenter);
    if(IsWindowVisible(presenter))return fail(19,"presenter-not-hidden-windowed",query,game,presenter);
    LONG_PTR gs=GetWindowLongPtrW(game,GWL_STYLE);
    if((gs&WS_CAPTION)==0 || (gs&WS_THICKFRAME)==0)return fail(20,"windowed-style-lost",query,game,presenter);

    request_style(game,fieldFrameless);
    if(!wait_mode(query,true))return fail(21,"windowed-to-field-frameless-borderless",query,game,presenter);
    Sleep(20);
    if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return fail(22,"field-borderless-geometry",query,game,presenter);
    RECT gr{};GetWindowRect(game,&gr);
    if(gr.left!=mi.rcMonitor.left||gr.top!=mi.rcMonitor.top)return fail(23,"field-borderless-origin",query,game,presenter);

    request_style(game,windowed);
    if(!wait_mode(query,false))return fail(24,"field-borderless-to-windowed",query,game,presenter);
    const LONG wx=mi.rcMonitor.left+73,wy=mi.rcMonitor.top+61,ww=517,wh=389;
    SetWindowPos(game,nullptr,wx,wy,ww,wh,SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    Sleep(20);
    if(!outer_is(game,wx,wy,ww,wh))return fail(25,"windowed-native-geometry",query,game,presenter);
    if(IsWindowVisible(presenter))return fail(26,"presenter-visible-windowed",query,game,presenter);
    gs=GetWindowLongPtrW(game,GWL_STYLE);
    if((gs&WS_CAPTION)==0 || (gs&WS_THICKFRAME)==0)return fail(27,"windowed-style-not-restored",query,game,presenter);

    // Retain compatibility with the previously-tested WS_POPUP borderless style too.
    request_style(game,WS_POPUP|WS_VISIBLE);
    if(!wait_mode(query,true))return fail(28,"popup-borderless-compat",query,game,presenter);
    request_style(game,windowed);
    if(!wait_mode(query,false))return fail(29,"popup-to-windowed-compat",query,game,presenter);

    constexpr unsigned kCycles=500;
    for(unsigned i=0;i<kCycles;++i){
        request_style(game,fieldFrameless);
        if(!wait_mode(query,true,1000))return fail(30,"stress-enter-field-borderless",query,game,presenter);
        if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return fail(31,"stress-field-borderless-geometry",query,game,presenter);
        request_style(game,windowed);
        if(!wait_mode(query,false,1000))return fail(32,"stress-enter-windowed",query,game,presenter);
        LONG x=mi.rcMonitor.left+LONG(20+(i%37)),y=mi.rcMonitor.top+LONG(30+(i%29));
        SetWindowPos(game,nullptr,x,y,420,310,SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        if(!outer_is(game,x,y,420,310)||IsWindowVisible(presenter))return fail(33,"stress-windowed-native",query,game,presenter);
    }

    ModeState final{};if(!query_state(query,final))return fail(40,"final-query",query,game,presenter);
    if(final.transitionsToWindowed<kCycles+2ull||final.transitionsToBorderless<kCycles+2ull)return fail(41,"transition-counters",query,game,presenter);

    std::printf("RC44_COEXIST_HOST=PASS output=%ux%u render=%ux%u cycles=%u exact_field_style=0x14000000 popup_compat=PASS to_windowed=%llu to_borderless=%llu initial_windowed=PASS windowed_native_geometry=PASS borderless_restore=PASS\n",
                outW,outH,renderW,renderH,kCycles,final.transitionsToWindowed,final.transitionsToBorderless);
    DestroyWindow(presenter);DestroyWindow(game);FreeLibrary(dll);UnregisterClassW(wc.lpszClassName,inst);
    return 0;
}
