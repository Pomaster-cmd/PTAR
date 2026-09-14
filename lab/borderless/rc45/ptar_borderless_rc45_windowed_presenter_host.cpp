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
    LONG targetLeft;
    LONG targetTop;
    LONG targetRight;
    LONG targetBottom;
    LONG windowStylePreference;
    unsigned long long presenterFollows;
};

struct RegistryBackup { bool keyExisted=false; bool valueExisted=false; DWORD value=0; };
static const wchar_t* kOptionsKey=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}

static bool query_state(QueryFn query,ModeState& s){s={};s.size=sizeof(s);return query(&s)==0;}
static bool wait_mode(QueryFn query,bool borderless,unsigned timeoutMs=5000){
    const DWORD start=GetTickCount();
    do{
        ModeState s{};
        if(query_state(query,s)&&s.installed&&((s.borderlessActive!=0)==borderless)) return true;
        MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        Sleep(2);
    }while(GetTickCount()-start<timeoutMs);
    return false;
}
static bool client_screen_rect(HWND h,RECT& out){
    RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};
    if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;out={a.x,a.y,b.x,b.y};return true;
}
static bool rect_equal(const RECT& a,const RECT& b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static bool presenter_matches_client(HWND game,HWND presenter){RECT g{},p{};if(!client_screen_rect(game,g)||!GetWindowRect(presenter,&p))return false;return rect_equal(g,p)&&IsWindowVisible(presenter);}
static bool client_is(HWND h,UINT w,UINT hh){RECT r{};return GetClientRect(h,&r)&&UINT(r.right-r.left)==w&&UINT(r.bottom-r.top)==hh;}
static bool wait_presenter_client(HWND game,HWND presenter,unsigned timeoutMs=3000){
    const DWORD start=GetTickCount();
    do{if(presenter_matches_client(game,presenter))return true;MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}while(GetTickCount()-start<timeoutMs);
    return false;
}
static bool wait_presenter_hidden(HWND presenter,unsigned timeoutMs=3000){
    const DWORD start=GetTickCount();
    do{if(!IsWindowVisible(presenter))return true;MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(2);}while(GetTickCount()-start<timeoutMs);
    return false;
}
static void pump(unsigned ms=20){const DWORD start=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-start<ms);}
static bool set_pref(DWORD value){
    HKEY key=nullptr;DWORD disp=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptionsKey,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,&disp)!=ERROR_SUCCESS)return false;
    const LONG rc=RegSetValueExW(key,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));RegCloseKey(key);return rc==ERROR_SUCCESS;
}
static bool backup_pref(RegistryBackup& b){
    HKEY key=nullptr;LONG rc=RegOpenKeyExW(HKEY_CURRENT_USER,kOptionsKey,0,KEY_QUERY_VALUE,&key);if(rc!=ERROR_SUCCESS)return true;
    b.keyExisted=true;DWORD type=0,size=sizeof(b.value);rc=RegQueryValueExW(key,L"WindowStyle",nullptr,&type,reinterpret_cast<BYTE*>(&b.value),&size);
    if(rc==ERROR_SUCCESS&&type==REG_DWORD&&size==sizeof(b.value))b.valueExisted=true;RegCloseKey(key);return true;
}
static void restore_pref(const RegistryBackup& b){
    HKEY key=nullptr;DWORD disp=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptionsKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,&disp)!=ERROR_SUCCESS)return;
    if(b.valueExisted)RegSetValueExW(key,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&b.value),sizeof(b.value));else RegDeleteValueW(key,L"WindowStyle");RegCloseKey(key);
}
static void request_style(HWND h,LONG_PTR style){SetLastError(ERROR_SUCCESS);SetWindowLongPtrW(h,GWL_STYLE,style);SetWindowPos(h,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);}
static bool set_client_size(HWND h,UINT w,UINT hh){
    LONG_PTR s=GetWindowLongPtrW(h,GWL_STYLE),ex=GetWindowLongPtrW(h,GWL_EXSTYLE);RECT r{0,0,(LONG)w,(LONG)hh};if(!AdjustWindowRectEx(&r,(DWORD)s,FALSE,(DWORD)ex))return false;
    RECT old{};if(!GetWindowRect(h,&old))return false;return SetWindowPos(h,nullptr,old.left,old.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW)!=FALSE;
}
static int fail(int code,const char* what,QueryFn query,HWND game,HWND presenter,const RegistryBackup& backup){
    ModeState s{};if(query)query_state(query,s);RECT g{},p{};if(game)GetWindowRect(game,&g);if(presenter)GetWindowRect(presenter,&p);
    std::printf("RC45_WINDOWED_PRESENTER_HOST=FAIL rc=%d what=%s installed=%u borderless=%u visible=%u pref=%ld follows=%llu game=%ld,%ld,%ld,%ld presenter=%ld,%ld,%ld,%ld gle=%lu\n",code,what,s.installed,s.borderlessActive,s.presenterVisible,s.windowStylePreference,s.presenterFollows,g.left,g.top,g.right,g.bottom,p.left,p.top,p.right,p.bottom,(unsigned long)GetLastError());
    restore_pref(backup);return code;
}

int wmain(){
    RegistryBackup backup{};backup_pref(backup);
    if(!set_pref(1)){std::printf("RC45_WINDOWED_PRESENTER_HOST=FAIL rc=10 registry-init\n");return 10;}
    HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC45_WINDOWED_PRESENTER_HOST";
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){restore_pref(backup);return 11;}
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,64,64,nullptr,nullptr,inst,nullptr);if(!probe){restore_pref(backup);return 12;}
    HMONITOR hm=MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!hm||!GetMonitorInfoW(hm,&mi)){DestroyWindow(probe);restore_pref(backup);return 13;}DestroyWindow(probe);
    const UINT outW=UINT(mi.rcMonitor.right-mi.rcMonitor.left),outH=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);const UINT renderW=(outW>=640?640:outW),renderH=(outH>=360?360:outH);
    const LONG_PTR windowed=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT outer{0,0,(LONG)renderW,(LONG)renderH};AdjustWindowRectEx(&outer,(DWORD)windowed,FALSE,0);
    const LONG initialX=mi.rcMonitor.left+83,initialY=mi.rcMonitor.top+67;
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game",windowed,initialX,initialY,outer.right-outer.left,outer.bottom-outer.top,nullptr,nullptr,inst,nullptr);
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,game,nullptr,inst,nullptr);
    if(!game||!presenter){restore_pref(backup);return 14;}RECT initialGameOuter{};GetWindowRect(game,&initialGameOuter);
    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return fail(15,"load-dll",nullptr,game,presenter,backup);
    auto attach=reinterpret_cast<AttachStableFn>(GetProcAddress(dll,"PTAR_BorderlessAttachStable"));auto query=reinterpret_cast<QueryFn>(GetProcAddress(dll,"PTAR_BorderlessQueryMode"));
    if(!attach||!query)return fail(16,"exports",query,game,presenter,backup);if(attach(game,presenter,renderW,renderH,outW,outH)!=0)return fail(17,"attach",query,game,presenter,backup);

    if(!wait_mode(query,true))return fail(18,"initial-pref-borderless",query,game,presenter,backup);
    if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return fail(19,"initial-borderless-geometry",query,game,presenter,backup);
    RECT pr{};GetWindowRect(presenter,&pr);if(pr.left!=mi.rcMonitor.left||pr.top!=mi.rcMonitor.top||UINT(pr.right-pr.left)!=outW||UINT(pr.bottom-pr.top)!=outH)return fail(20,"initial-presenter-full-output",query,game,presenter,backup);

    if(!set_pref(0))return fail(21,"set-pref-windowed",query,game,presenter,backup);
    if(!wait_mode(query,false))return fail(22,"pref-only-to-windowed",query,game,presenter,backup);
    if(!wait_presenter_client(game,presenter))return fail(23,"windowed-presenter-follow-initial",query,game,presenter,backup);
    LONG_PTR gs=GetWindowLongPtrW(game,GWL_STYLE);if((gs&WS_CAPTION)==0||(gs&WS_THICKFRAME)==0)return fail(24,"windowed-style-restored",query,game,presenter,backup);
    RECT restored{};GetWindowRect(game,&restored);if(!rect_equal(restored,initialGameOuter))return fail(25,"windowed-outer-restored",query,game,presenter,backup);

    const LONG moveX=mi.rcMonitor.left+137,moveY=mi.rcMonitor.top+101;SetWindowPos(game,nullptr,moveX,moveY,restored.right-restored.left,restored.bottom-restored.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    if(!wait_presenter_client(game,presenter))return fail(26,"windowed-presenter-follow-move",query,game,presenter,backup);
    if(!set_client_size(game,700,400))return fail(27,"windowed-resize-request",query,game,presenter,backup);
    if(!wait_presenter_client(game,presenter)||!client_is(game,700,400))return fail(28,"windowed-presenter-follow-resize",query,game,presenter,backup);
    ShowWindow(game,SW_MINIMIZE);pump(30);if(!wait_presenter_hidden(presenter))return fail(29,"windowed-minimize-hides-presenter",query,game,presenter,backup);
    ShowWindow(game,SW_RESTORE);pump(30);if(!wait_presenter_client(game,presenter))return fail(30,"windowed-restore-presenter",query,game,presenter,backup);

    if(!set_pref(1))return fail(31,"set-pref-borderless",query,game,presenter,backup);if(!wait_mode(query,true))return fail(32,"pref-only-to-borderless",query,game,presenter,backup);
    if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return fail(33,"borderless-reentry-geometry",query,game,presenter,backup);

    constexpr unsigned kCycles=500;const LONG_PTR frameless=WS_VISIBLE|WS_CLIPSIBLINGS;
    for(unsigned i=0;i<kCycles;++i){
        request_style(game,windowed);if(!wait_mode(query,false,1000))return fail(40,"stress-windowed-mode",query,game,presenter,backup);if(!wait_presenter_client(game,presenter,1000))return fail(41,"stress-windowed-presenter",query,game,presenter,backup);
        request_style(game,frameless);if(!wait_mode(query,true,1000))return fail(42,"stress-borderless-mode",query,game,presenter,backup);if(!client_is(game,renderW,renderH)||!client_is(presenter,outW,outH)||!IsWindowVisible(presenter))return fail(43,"stress-borderless-geometry",query,game,presenter,backup);
    }
    ModeState final{};if(!query_state(query,final))return fail(50,"final-query",query,game,presenter,backup);
    if(final.transitionsToWindowed<kCycles+1ull||final.transitionsToBorderless<kCycles+1ull||final.presenterFollows<kCycles+3ull)return fail(51,"transition-counters",query,game,presenter,backup);
    std::printf("RC45_WINDOWED_PRESENTER_HOST=PASS output=%ux%u render=%ux%u pref_initial_borderless=PASS pref_only_windowed=PASS presenter_visible_windowed=PASS move_follow=PASS resize_follow=PASS minimize_restore=PASS borderless_reentry=PASS cycles=%u to_windowed=%llu to_borderless=%llu follows=%llu\n",outW,outH,renderW,renderH,kCycles,final.transitionsToWindowed,final.transitionsToBorderless,final.presenterFollows);
    restore_pref(backup);DestroyWindow(presenter);DestroyWindow(game);FreeLibrary(dll);UnregisterClassW(wc.lpszClassName,inst);return 0;
}
