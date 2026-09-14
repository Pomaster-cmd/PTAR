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
static bool wait_mode(QueryFn query,bool active,unsigned timeoutMs=4000){
    const DWORD start=GetTickCount();
    do{
        ModeState s{};s.size=sizeof(s);
        if(query(&s)==0 && s.installed && (!!s.borderlessActive)==active) return true;
        Sleep(10);
    }while(GetTickCount()-start<timeoutMs);
    return false;
}
static bool client_is(HWND h,UINT w,UINT hh){RECT r{};return GetClientRect(h,&r)&&UINT(r.right-r.left)==w&&UINT(r.bottom-r.top)==hh;}
static bool outer_is(HWND h,LONG x,LONG y,LONG w,LONG hh){RECT r{};return GetWindowRect(h,&r)&&r.left==x&&r.top==y&&(r.right-r.left)==w&&(r.bottom-r.top)==hh;}

int wmain(){
    HINSTANCE inst=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC43_COEXIST_HOST";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 10;

    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,64,64,nullptr,nullptr,inst,nullptr);
    if(!probe)return 11;
    HMONITOR hm=MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi))return 12;
    DestroyWindow(probe);
    const UINT outW=UINT(mi.rcMonitor.right-mi.rcMonitor.left),outH=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);
    const UINT renderW=(outW>=640?640:outW),renderH=(outH>=360?360:outH);

    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,renderW,renderH,nullptr,nullptr,inst,nullptr);
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,game,nullptr,inst,nullptr);
    if(!game||!presenter)return 13;

    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return 14;
    auto attach=reinterpret_cast<AttachStableFn>(GetProcAddress(dll,"PTAR_BorderlessAttachStable"));
    auto query=reinterpret_cast<QueryFn>(GetProcAddress(dll,"PTAR_BorderlessQueryMode"));
    if(!attach||!query)return 15;
    if(attach(game,presenter,renderW,renderH,outW,outH)!=0)return 16;
    if(!wait_mode(query,true))return 17;
    if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return 18;

    // Borderless -> windowed: the game's own style and geometry must become authoritative.
    const LONG_PTR windowed=WS_OVERLAPPEDWINDOW|WS_VISIBLE;
    SetWindowLongPtrW(game,GWL_STYLE,windowed);
    if(!wait_mode(query,false))return 19;
    const LONG wx=mi.rcMonitor.left+73,wy=mi.rcMonitor.top+61,ww=517,wh=389;
    SetWindowPos(game,nullptr,wx,wy,ww,wh,SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    Sleep(20);
    if(!outer_is(game,wx,wy,ww,wh))return 20;
    if(IsWindowVisible(presenter))return 21;
    LONG_PTR gs=GetWindowLongPtrW(game,GWL_STYLE);
    if((gs&WS_CAPTION)==0 || (gs&WS_THICKFRAME)==0)return 22;

    // Windowed -> borderless: reuse the already validated RC40 takeover path.
    SetWindowLongPtrW(game,GWL_STYLE,WS_POPUP|WS_VISIBLE);
    if(!wait_mode(query,true))return 23;
    Sleep(20);
    if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return 24;
    RECT gr{};GetWindowRect(game,&gr);
    if(gr.left!=mi.rcMonitor.left||gr.top!=mi.rcMonitor.top)return 25;

    constexpr unsigned kCycles=500;
    for(unsigned i=0;i<kCycles;++i){
        SetWindowLongPtrW(game,GWL_STYLE,windowed);
        if(!wait_mode(query,false,1000))return 30;
        LONG x=mi.rcMonitor.left+LONG(20+(i%37)),y=mi.rcMonitor.top+LONG(30+(i%29));
        SetWindowPos(game,nullptr,x,y,420,310,SWP_NOZORDER|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        if(!outer_is(game,x,y,420,310)||IsWindowVisible(presenter))return 31;
        SetWindowLongPtrW(game,GWL_STYLE,WS_POPUP|WS_VISIBLE);
        if(!wait_mode(query,true,1000))return 32;
        if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return 33;
    }

    ModeState final{};final.size=sizeof(final);if(query(&final)!=0)return 40;
    if(final.transitionsToWindowed<kCycles+1ull||final.transitionsToBorderless<kCycles+1ull)return 41;

    std::printf("RC43_COEXIST_HOST=PASS output=%ux%u render=%ux%u cycles=%u to_windowed=%llu to_borderless=%llu windowed_native_geometry=PASS borderless_restore=PASS\n",
                outW,outH,renderW,renderH,kCycles,final.transitionsToWindowed,final.transitionsToBorderless);
    FreeLibrary(dll);
    DestroyWindow(presenter);DestroyWindow(game);UnregisterClassW(wc.lpszClassName,inst);
    return 0;
}
