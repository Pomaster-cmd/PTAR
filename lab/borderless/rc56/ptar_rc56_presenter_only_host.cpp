#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cwchar>

static const wchar_t* kOptions=L"Software\\NeoCore Games\\Warhammer Martyr\\Options";
static const wchar_t* kSync=L"PTAR_RC46_WINDOWSTYLE_SYNC_20260914";

struct RegBackup { bool keyExisted=false; bool valueExisted=false; DWORD value=0; };
struct ModeState {
    UINT size,installed,borderlessActive,presenterVisible;
    ULONG_PTR gameStyle,presenterStyle;
    unsigned long long transitionsToWindowed,transitionsToBorderless;
    LONG targetLeft,targetTop,targetRight,targetBottom;
    LONG windowStylePreference;
    unsigned long long presenterFollows,gameCoercionClamps,presenterClamps,rejectedWindowSaves;
    LONG savedLeft,savedTop,savedRight,savedBottom;
};
using AttachFn=int (WINAPI*)(HWND,HWND,UINT,UINT,UINT,UINT);
using QueryFn=int (WINAPI*)(ModeState*);

static LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void pump(DWORD ms=1){DWORD st=GetTickCount();do{MSG x{};while(PeekMessageW(&x,nullptr,0,0,PM_REMOVE)){TranslateMessage(&x);DispatchMessageW(&x);}Sleep(1);}while(GetTickCount()-st<ms);}

static void backup(RegBackup& b){
    HKEY k=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,kOptions,0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return;
    b.keyExisted=true;DWORD type=0,size=sizeof(b.value);
    if(RegQueryValueExW(k,L"WindowStyle",nullptr,&type,reinterpret_cast<BYTE*>(&b.value),&size)==ERROR_SUCCESS && type==REG_DWORD && size==sizeof(b.value))b.valueExisted=true;
    RegCloseKey(k);
}
static bool set_pref(DWORD value){
    HKEY k=nullptr;DWORD disp=0;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k,&disp)!=ERROR_SUCCESS)return false;
    const LONG rc=RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));RegCloseKey(k);return rc==ERROR_SUCCESS;
}
static void restore(const RegBackup& b){
    HKEY k=nullptr;DWORD disp=0;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,kOptions,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,&disp)!=ERROR_SUCCESS)return;
    if(b.valueExisted)RegSetValueExW(k,L"WindowStyle",0,REG_DWORD,reinterpret_cast<const BYTE*>(&b.value),sizeof(b.value));
    else RegDeleteValueW(k,L"WindowStyle");
    RegCloseKey(k);
    if(!b.keyExisted)RegDeleteKeyW(HKEY_CURRENT_USER,kOptions);
}
static bool rect_eq(const RECT& a,const RECT& b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static bool client_screen(HWND h,RECT& out){RECT c{};if(!GetClientRect(h,&c))return false;POINT a{c.left,c.top},b{c.right,c.bottom};if(!ClientToScreen(h,&a)||!ClientToScreen(h,&b))return false;out={a.x,a.y,b.x,b.y};return b.x>a.x&&b.y>a.y;}
static bool wait_mode(QueryFn q,bool borderless,DWORD timeout=3000){DWORD st=GetTickCount();do{ModeState s{};s.size=sizeof(s);if(q(&s)==0 && s.installed && (!!s.borderlessActive)==borderless)return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool wait_rect(HWND h,const RECT& want,DWORD timeout=3000){DWORD st=GetTickCount();do{RECT r{};if(GetWindowRect(h,&r)&&rect_eq(r,want))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}
static bool wait_presenter_client(HWND presenter,const RECT& want,DWORD timeout=3000){DWORD st=GetTickCount();do{RECT r{};if(client_screen(presenter,r)&&rect_eq(r,want))return true;pump(2);}while(GetTickCount()-st<timeout);return false;}

static int run(bool directBorderless){
    FILE* log=nullptr;_wfopen_s(&log,directBorderless?L"RC56_DIRECT_BORDERLESS_HOST.txt":L"RC56_TRANSITION_HOST.txt",L"wb");if(!log)return 90;
    RegBackup rb{};backup(rb);const DWORD initialPref=directBorderless?1u:0u;if(!set_pref(initialPref)){fclose(log);return 10;}

    HINSTANCE inst=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=inst;wc.lpszClassName=L"PTAR_RC56_HOST_WINDOW";RegisterClassW(&wc);
    HMONITOR hm=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!GetMonitorInfoW(hm,&mi)){restore(rb);fclose(log);return 11;}
    const UINT outW=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),outH=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);
    const UINT rw=(outW>=900?640u:(outW*2u)/3u),rh=(outH>=600?360u:(outH*2u)/3u);
    const DWORD gameStyle=directBorderless?(WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS):(WS_OVERLAPPEDWINDOW|WS_VISIBLE);
    RECT wr{0,0,(LONG)rw,(LONG)rh};if(!directBorderless)AdjustWindowRectEx(&wr,gameStyle,FALSE,0);
    HWND game=CreateWindowExW(0,wc.lpszClassName,L"PTAR RC56 game host",gameStyle,100,90,wr.right-wr.left,wr.bottom-wr.top,nullptr,nullptr,inst,nullptr);
    HWND presenter=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"PTAR RC56 presenter host",WS_POPUP|WS_VISIBLE,100,90,(int)rw,(int)rh,nullptr,nullptr,inst,nullptr);
    if(!game||!presenter){if(game)DestroyWindow(game);if(presenter)DestroyWindow(presenter);restore(rb);fclose(log);return 12;}
    ShowWindow(game,SW_SHOW);ShowWindow(presenter,SW_SHOWNA);pump(20);
    RECT game0{};GetWindowRect(game,&game0);const LONG_PTR style0=GetWindowLongPtrW(game,GWL_STYLE);
    RECT gameClient0{};if(!client_screen(game,gameClient0)){restore(rb);fclose(log);return 13;}

    HMODULE dll=LoadLibraryW(L"ptar_borderless.dll");if(!dll){fwprintf(log,L"FAIL load dll gle=%lu\n",GetLastError());restore(rb);fclose(log);return 14;}
    auto attach=reinterpret_cast<AttachFn>(GetProcAddress(dll,"PTAR_BorderlessAttachStable"));auto query=reinterpret_cast<QueryFn>(GetProcAddress(dll,"PTAR_BorderlessQueryMode"));
    if(!attach||!query){FreeLibrary(dll);restore(rb);fclose(log);return 15;}
    const int ar=attach(game,presenter,rw,rh,outW,outH);fwprintf(log,L"ATTACH rc=%d output=%ux%u render=%ux%u direct=%u\n",ar,outW,outH,rw,rh,directBorderless?1u:0u);fflush(log);if(ar<0){FreeLibrary(dll);restore(rb);fclose(log);return 16;}
    if(!wait_mode(query,directBorderless,5000)){fwprintf(log,L"FAIL initial mode\n");FreeLibrary(dll);restore(rb);fclose(log);return 17;}

    RECT game1{};GetWindowRect(game,&game1);if(!rect_eq(game0,game1)||GetWindowLongPtrW(game,GWL_STYLE)!=style0){fwprintf(log,L"FAIL initial game mutation\n");FreeLibrary(dll);restore(rb);fclose(log);return 18;}
    if(directBorderless){
        if(!wait_rect(presenter,mi.rcMonitor,5000)){RECT x{};GetWindowRect(presenter,&x);fwprintf(log,L"FAIL direct presenter %ld,%ld,%ld,%ld want %ld,%ld,%ld,%ld\n",x.left,x.top,x.right,x.bottom,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right,mi.rcMonitor.bottom);FreeLibrary(dll);restore(rb);fclose(log);return 19;}
        for(unsigned i=0;i<1000;++i){
            SendMessageW(game,WM_MOVE,0,MAKELPARAM((WORD)(100+i%3),(WORD)(90+i%3)));
            SendMessageW(game,WM_SIZE,SIZE_RESTORED,MAKELPARAM((WORD)rw,(WORD)rh));
            if((i&31u)==0)pump(1);
        }
        GetWindowRect(game,&game1);if(!rect_eq(game0,game1)||GetWindowLongPtrW(game,GWL_STYLE)!=style0){fwprintf(log,L"FAIL direct stress mutated game window\n");FreeLibrary(dll);restore(rb);fclose(log);return 20;}
        ModeState s{};s.size=sizeof(s);query(&s);fwprintf(log,L"RC56_DIRECT_BORDERLESS=PASS game_untouched=1 presenter_native=1 messages=2000 to_borderless=%llu\n",s.transitionsToBorderless);fflush(log);
    } else {
        if(!wait_presenter_client(presenter,gameClient0,5000)){fwprintf(log,L"FAIL initial windowed presenter follow\n");FreeLibrary(dll);restore(rb);fclose(log);return 21;}
        const UINT sync=RegisterWindowMessageW(kSync);if(!sync){FreeLibrary(dll);restore(rb);fclose(log);return 22;}
        constexpr unsigned cycles=500;
        for(unsigned i=0;i<cycles;++i){
            if(!set_pref(1)){FreeLibrary(dll);restore(rb);fclose(log);return 23;}
            SendMessageW(game,sync,1,0);
            if(!wait_mode(query,true,1000)||!wait_rect(presenter,mi.rcMonitor,1000)){fwprintf(log,L"FAIL borderless cycle=%u\n",i);FreeLibrary(dll);restore(rb);fclose(log);return 24;}
            GetWindowRect(game,&game1);if(!rect_eq(game0,game1)||GetWindowLongPtrW(game,GWL_STYLE)!=style0){fwprintf(log,L"FAIL game mutation entering borderless cycle=%u\n",i);FreeLibrary(dll);restore(rb);fclose(log);return 25;}
            if(!set_pref(0)){FreeLibrary(dll);restore(rb);fclose(log);return 26;}
            SendMessageW(game,sync,0,0);
            if(!wait_mode(query,false,1000)||!wait_presenter_client(presenter,gameClient0,1000)){fwprintf(log,L"FAIL windowed cycle=%u\n",i);FreeLibrary(dll);restore(rb);fclose(log);return 27;}
            GetWindowRect(game,&game1);if(!rect_eq(game0,game1)||GetWindowLongPtrW(game,GWL_STYLE)!=style0){fwprintf(log,L"FAIL game mutation returning windowed cycle=%u\n",i);FreeLibrary(dll);restore(rb);fclose(log);return 28;}
        }
        ModeState s{};s.size=sizeof(s);query(&s);fwprintf(log,L"RC56_TRANSITION_STRESS=PASS cycles=%u game_untouched=1 presenter_only=1 to_borderless=%llu to_windowed=%llu follows=%llu\n",cycles,s.transitionsToBorderless,s.transitionsToWindowed,s.presenterFollows);fflush(log);
    }

    FreeLibrary(dll);DestroyWindow(presenter);DestroyWindow(game);restore(rb);fclose(log);return 0;
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR cmd,int){
    const bool direct=(cmd && (wcsstr(cmd,L"direct")!=nullptr));
    return run(direct);
}
