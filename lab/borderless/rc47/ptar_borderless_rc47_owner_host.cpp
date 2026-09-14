#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>

using AttachFn=int (WINAPI*)(HWND,HWND,UINT,UINT,UINT,UINT);
using QueryFn=int (WINAPI*)(void*);

struct ModeState {
    UINT size,installed,borderlessActive,presenterVisible;
    ULONG_PTR gameStyle,presenterStyle;
    unsigned long long transitionsToWindowed,transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom,windowStylePreference;
    unsigned long long presenterFollows,gameCoercionClamps,presenterClamps,rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
    ULONG_PTR presenterOwner,originalPresenterOwner;
    unsigned long long ownerAttach,ownerRestore,zRepairs;
};

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
struct RegBackup{bool existed=false;DWORD value=0;};
static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms=20){DWORD s=GetTickCount();do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}while(GetTickCount()-s<ms);}
static bool set_pref(DWORD v){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return false;LONG r=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(BYTE*)&v,sizeof(v));RegCloseKey(k);return r==ERROR_SUCCESS;}
static void backup(RegBackup& b){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;DWORD t=0,s=sizeof(b.value);if(RegQueryValueExW(k,L"WindowStyle",nullptr,&t,(BYTE*)&b.value,&s)==ERROR_SUCCESS&&t==REG_DWORD&&s==sizeof(b.value))b.existed=true;RegCloseKey(k);}
static void restore(const RegBackup& b){HKEY k=nullptr;DWORD d=0;if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&d)!=ERROR_SUCCESS)return;if(b.existed)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,(const BYTE*)&b.value,sizeof(b.value));else RegDeleteValueW(k,L"WindowStyle");RegCloseKey(k);}
static bool query(QueryFn q,ModeState& s){s={};s.size=sizeof(s);return q(&s)==0;}
static bool wait_mode(QueryFn q,bool b,unsigned timeout=5000){DWORD st=GetTickCount();do{ModeState s{};if(query(q,s)&&s.installed&&((s.borderlessActive!=0)==b))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool client_rect_screen(HWND h,RECT& r){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;r={a.x,a.y,b.x,b.y};return true;}
static bool presenter_matches(HWND game,HWND presenter){RECT c{},p{};return client_rect_screen(game,c)&&GetWindowRect(presenter,&p)&&EqualRect(&c,&p)&&IsWindowVisible(presenter);}
static bool wait_owned(HWND p,HWND g,unsigned timeout=3000){DWORD st=GetTickCount();do{if(GetWindow(p,GW_OWNER)==g)return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool wait_unowned(HWND p,HWND original,unsigned timeout=3000){DWORD st=GetTickCount();do{if(GetWindow(p,GW_OWNER)==original)return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool wait_presenter(HWND g,HWND p,unsigned timeout=3000){DWORD st=GetTickCount();do{if(presenter_matches(g,p))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static int fail(int rc,const char* what,QueryFn q,HWND g,HWND p,const RegBackup& b){ModeState s{};if(q)query(q,s);std::printf("RC47_OWNER_HOST=FAIL rc=%d what=%s mode=%u visible=%u owner=%p game=%p original=%p attach=%llu restore=%llu z=%llu gle=%lu\n",rc,what,s.borderlessActive,s.presenterVisible,(void*)s.presenterOwner,g,(void*)s.originalPresenterOwner,s.ownerAttach,s.ownerRestore,s.zRepairs,(unsigned long)GetLastError());restore(b);return rc;}

int wmain(){
    RegBackup rb{};backup(rb);if(!set_pref(0)){std::puts("RC47_OWNER_HOST=FAIL registry");return 10;}
    HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC47_OWNER_HOST";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){restore(rb);return 11;}
    HWND probe=CreateWindowExW(0,wc.lpszClassName,L"probe",WS_POPUP,0,0,64,64,nullptr,nullptr,inst,nullptr);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(probe,MONITOR_DEFAULTTONEAREST),&mi);DestroyWindow(probe);
    const UINT outW=mi.rcMonitor.right-mi.rcMonitor.left,outH=mi.rcMonitor.bottom-mi.rcMonitor.top,rw=outW>=640?640:outW,rh=outH>=360?360:outH;
    RECT wr{0,0,(LONG)rw,(LONG)rh};AdjustWindowRectEx(&wr,WS_OVERLAPPEDWINDOW|WS_VISIBLE,FALSE,0);
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"game",WS_OVERLAPPEDWINDOW|WS_VISIBLE,mi.rcMonitor.left+100,mi.rcMonitor.top+80,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);
    // Critical field condition: the real P1U46 presenter is unowned. RC45's old host
    // accidentally created it owned by game, hiding the z-order defect.
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,mi.rcMonitor.left,mi.rcMonitor.top,outW,outH,nullptr,nullptr,inst,nullptr);
    if(!game||!presenter)return fail(12,"create",nullptr,game,presenter,rb);
    if(GetWindow(presenter,GW_OWNER)!=nullptr)return fail(13,"presenter-not-unowned",nullptr,game,presenter,rb);
    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll)return fail(14,"load",nullptr,game,presenter,rb);auto attach=(AttachFn)GetProcAddress(dll,"PTAR_BorderlessAttachStable");auto q=(QueryFn)GetProcAddress(dll,"PTAR_BorderlessQueryMode");if(!attach||!q)return fail(15,"exports",q,game,presenter,rb);
    if(attach(game,presenter,rw,rh,outW,outH)!=0)return fail(16,"attach",q,game,presenter,rb);if(!wait_mode(q,false))return fail(17,"windowed-mode",q,game,presenter,rb);if(!wait_owned(presenter,game))return fail(18,"windowed-owner-not-attached",q,game,presenter,rb);if(!wait_presenter(game,presenter))return fail(19,"windowed-presenter-geometry",q,game,presenter,rb);

    // Reproduce the field occlusion: runtime repeatedly raises game HWND_TOP. Owned
    // presenter must remain the hit-test top window over the client after every raise.
    constexpr unsigned kRaises=500;
    for(unsigned i=0;i<kRaises;++i){
        SetWindowPos(game,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);pump(2);
        RECT c{};if(!client_rect_screen(game,c))return fail(20,"client",q,game,presenter,rb);POINT pt{(c.left+c.right)/2,(c.top+c.bottom)/2};HWND top=WindowFromPoint(pt);
        if(top!=presenter)return fail(21,"presenter-occluded-by-owner",q,game,presenter,rb);
    }

    if(!set_pref(1)||!wait_mode(q,true))return fail(22,"to-borderless",q,game,presenter,rb);if(!wait_unowned(presenter,nullptr))return fail(23,"borderless-owner-not-restored",q,game,presenter,rb);
    RECT p{};GetWindowRect(presenter,&p);if(p.left!=mi.rcMonitor.left||p.top!=mi.rcMonitor.top||UINT(p.right-p.left)!=outW||UINT(p.bottom-p.top)!=outH)return fail(24,"borderless-geometry",q,game,presenter,rb);
    if(!set_pref(0)||!wait_mode(q,false))return fail(25,"back-windowed",q,game,presenter,rb);if(!wait_owned(presenter,game)||!wait_presenter(game,presenter))return fail(26,"windowed-reentry",q,game,presenter,rb);
    ModeState s{};if(!query(q,s)||s.ownerAttach<2||s.ownerRestore<1)return fail(27,"owner-counters",q,game,presenter,rb);
    std::printf("RC47_OWNER_HOST=PASS unowned_field_presenter=PASS windowed_owner_attach=PASS hwnd_top_occlusion_guard=PASS raises=%u borderless_owner_restore=PASS windowed_reentry=PASS attach=%llu restore=%llu\n",kRaises,s.ownerAttach,s.ownerRestore);
    restore(rb);DestroyWindow(presenter);DestroyWindow(game);FreeLibrary(dll);UnregisterClassW(wc.lpszClassName,inst);return 0;
}
