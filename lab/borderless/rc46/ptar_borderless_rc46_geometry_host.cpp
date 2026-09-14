#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>

using AttachStableFn=int (WINAPI*)(HWND,HWND,UINT,UINT,UINT,UINT);
using QueryFn=int (WINAPI*)(void*);

struct ModeState {
    UINT size;UINT installed;UINT borderlessActive;UINT presenterVisible;
    ULONG_PTR gameStyle;ULONG_PTR presenterStyle;
    unsigned long long transitionsToWindowed;unsigned long long transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom;LONG windowStylePreference;
    unsigned long long presenterFollows;unsigned long long gameCoercionClamps;unsigned long long presenterClamps;unsigned long long rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
};
struct RegistryBackup{bool valueExisted=false;DWORD value=0;};
static const wchar_t* kOptionsKey=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms=20){DWORD start=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-start<ms);}
static bool query_state(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q(&s)==0;}
static bool wait_mode(QueryFn q,bool b,unsigned timeout=4000){DWORD st=GetTickCount();do{ModeState s{};if(query_state(q,s)&&s.installed&&((s.borderlessActive!=0)==b))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool client_screen_rect(HWND h,RECT& out){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;out={a.x,a.y,b.x,b.y};return true;}
static bool rect_eq(const RECT&a,const RECT&b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static bool presenter_matches(HWND game,HWND presenter){RECT c{},p{};return client_screen_rect(game,c)&&GetWindowRect(presenter,&p)&&rect_eq(c,p)&&IsWindowVisible(presenter);}
static bool wait_presenter(HWND game,HWND presenter,unsigned timeout=2000){DWORD st=GetTickCount();do{if(presenter_matches(game,presenter))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool wait_outer(HWND h,const RECT& want,unsigned timeout=2000){DWORD st=GetTickCount();do{RECT r{};if(GetWindowRect(h,&r)&&rect_eq(r,want))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptionsKey,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<BYTE*>(&v),sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void backup_pref(RegistryBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptionsKey,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,reinterpret_cast<BYTE*>(&b.value),&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.valueExisted=true;RegCloseKey(k);}
static void restore_pref(const RegistryBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptionsKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.valueExisted)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&b.value),sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
static int fail(int rc,const char* what,QueryFn q,HWND game,HWND presenter,const RegistryBackup& b){ModeState s{};if(q)query_state(q,s);RECT g{},p{};if(game)GetWindowRect(game,&g);if(presenter)GetWindowRect(presenter,&p);std::printf("RC46_GEOMETRY_HOST=FAIL rc=%d what=%s mode=%u pref=%ld game=%ld,%ld,%ld,%ld presenter=%ld,%ld,%ld,%ld clamps=%llu pclamps=%llu rejects=%llu gle=%lu\n",rc,what,s.borderlessActive,s.windowStylePreference,g.left,g.top,g.right,g.bottom,p.left,p.top,p.right,p.bottom,s.gameCoercionClamps,s.presenterClamps,s.rejectedWindowSaves,(unsigned long)GetLastError());restore_pref(b);return rc;}

int wmain(){
    RegistryBackup backup{};backup_pref(backup);if(!set_pref(0)){std::puts("RC46_GEOMETRY_HOST=FAIL registry");return 10;}
    HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=WndProc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC46_GEOMETRY_HOST";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){restore_pref(backup);return 11;}
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,64,64,nullptr,nullptr,inst,nullptr);if(!probe){restore_pref(backup);return 12;}HMONITOR hm=MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(hm,&mi);DestroyWindow(probe);
    const UINT outW=UINT(mi.rcMonitor.right-mi.rcMonitor.left),outH=UINT(mi.rcMonitor.bottom-mi.rcMonitor.top);const UINT rw=(outW>=640?640:outW),rh=(outH>=360?360:outH);
    const LONG_PTR winStyle=WS_OVERLAPPEDWINDOW|WS_VISIBLE;RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,(DWORD)winStyle,FALSE,0);const LONG x=mi.rcMonitor.left+83,y=mi.rcMonitor.top+67;
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game",winStyle,x,y,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,nullptr,nullptr,inst,nullptr);if(!game||!presenter)return fail(13,"create-windows",nullptr,game,presenter,backup);
    RECT initial{};GetWindowRect(game,&initial);
    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return fail(14,"load",nullptr,game,presenter,backup);auto attach=(AttachStableFn)GetProcAddress(dll,"PTAR_BorderlessAttachStable");auto query=(QueryFn)GetProcAddress(dll,"PTAR_BorderlessQueryMode");if(!attach||!query)return fail(15,"exports",query,game,presenter,backup);
    if(attach(game,presenter,rw,rh,outW,outH)!=0)return fail(16,"attach",query,game,presenter,backup);if(!wait_mode(query,false))return fail(17,"initial-windowed",query,game,presenter,backup);if(!wait_outer(game,initial)||!wait_presenter(game,presenter))return fail(18,"initial-real-window",query,game,presenter,backup);

    RECT fullClient{0,0,(LONG)outW,(LONG)outH};AdjustWindowRectEx(&fullClient,(DWORD)winStyle,FALSE,0);const LONG fx=mi.rcMonitor.left+fullClient.left,fy=mi.rcMonitor.top+fullClient.top,fw=fullClient.right-fullClient.left,fh=fullClient.bottom-fullClient.top;
    SetWindowPos(game,nullptr,fx,fy,fw,fh,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);pump(30);
    if(!wait_outer(game,initial)||!wait_presenter(game,presenter))return fail(19,"field-monitor-coercion-not-rejected",query,game,presenter,backup);

    SetWindowPos(presenter,nullptr,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);pump(30);
    if(!wait_presenter(game,presenter))return fail(20,"presenter-origin-coercion-not-rejected",query,game,presenter,backup);

    SendMessageW(game,WM_ENTERSIZEMOVE,0,0);RECT moved=initial;OffsetRect(&moved,137,101);SetWindowPos(game,nullptr,moved.left,moved.top,moved.right-moved.left,moved.bottom-moved.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);SendMessageW(game,WM_EXITSIZEMOVE,0,0);pump(30);
    if(!wait_outer(game,moved)||!wait_presenter(game,presenter))return fail(21,"interactive-move-not-preserved",query,game,presenter,backup);
    SetWindowPos(game,nullptr,fx,fy,fw,fh,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);pump(30);
    if(!wait_outer(game,moved)||!wait_presenter(game,presenter))return fail(22,"second-coercion-did-not-restore-user-rect",query,game,presenter,backup);

    if(!set_pref(1)||!wait_mode(query,true))return fail(23,"to-borderless",query,game,presenter,backup);RECT pp{};GetWindowRect(presenter,&pp);if(pp.left!=mi.rcMonitor.left||pp.top!=mi.rcMonitor.top||UINT(pp.right-pp.left)!=outW||UINT(pp.bottom-pp.top)!=outH)return fail(24,"borderless-presenter",query,game,presenter,backup);
    if(!set_pref(0)||!wait_mode(query,false))return fail(25,"back-windowed",query,game,presenter,backup);if(!wait_outer(game,moved)||!wait_presenter(game,presenter))return fail(26,"windowed-restores-user-rect",query,game,presenter,backup);

    constexpr unsigned kCoercionStress=500;
    for(unsigned i=0;i<kCoercionStress;++i){SetWindowPos(game,nullptr,fx,fy,fw,fh,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);if(!wait_outer(game,moved,500))return fail(30,"stress-game-coercion",query,game,presenter,backup);SetWindowPos(presenter,nullptr,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);if(!wait_presenter(game,presenter,500))return fail(31,"stress-presenter-coercion",query,game,presenter,backup);}
    ModeState s{};if(!query_state(query,s))return fail(32,"query",query,game,presenter,backup);if(s.gameCoercionClamps<kCoercionStress+2ull||s.presenterClamps<kCoercionStress+1ull)return fail(33,"clamp-counters",query,game,presenter,backup);
    std::printf("RC46_GEOMETRY_HOST=PASS output=%ux%u render=%ux%u field_rect_guard=PASS presenter_origin_guard=PASS interactive_move=PASS borderless_roundtrip=PASS coercion_cycles=%u game_clamps=%llu presenter_clamps=%llu rejected_saves=%llu\n",outW,outH,rw,rh,kCoercionStress,s.gameCoercionClamps,s.presenterClamps,s.rejectedWindowSaves);
    restore_pref(backup);DestroyWindow(presenter);DestroyWindow(game);FreeLibrary(dll);UnregisterClassW(wc.lpszClassName,inst);return 0;
}
