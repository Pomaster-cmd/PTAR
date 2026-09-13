#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>

static HWND g_game=nullptr,g_presenter=nullptr,g_decoy=nullptr;
static bool g_takeover=false;
static bool g_authoritativeChange=false;
static LONG g_renderW=1280,g_renderH=720,g_originX=0,g_originY=0;
static unsigned long long g_minimize=0,g_restore=0,g_focusReturn=0,g_clamps=0,g_outputMoves=0,g_hostile=0;

static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(g_takeover&&!g_authoritativeChange&&m==WM_WINDOWPOSCHANGING){
        WINDOWPOS*p=reinterpret_cast<WINDOWPOS*>(l);
        if(!(p->flags&SWP_NOMOVE)){p->x=g_originX;p->y=g_originY;}
        if(!(p->flags&SWP_NOSIZE)){p->cx=g_renderW;p->cy=g_renderH;}
        ++g_clamps;
        return 0;
    }
    if(m==WM_SIZE&&g_takeover&&g_presenter){
        if(w==SIZE_MINIMIZED){ShowWindow(g_presenter,SW_HIDE);++g_minimize;}
        else if(w==SIZE_RESTORED){ShowWindow(g_presenter,SW_SHOWNOACTIVATE);++g_restore;}
    }
    return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE){
        SetForegroundWindow(g_game);
        SetActiveWindow(g_game);
        SetFocus(g_game);
        ++g_focusReturn;
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK DecoyProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}

static bool client_is(HWND h,LONG w,LONG hh){RECT r{};return GetClientRect(h,&r)&&r.left==0&&r.top==0&&r.right==w&&r.bottom==hh;}
static bool window_is(HWND h,const RECT&r){RECT a{};return GetWindowRect(h,&a)&&a.left==r.left&&a.top==r.top&&a.right==r.right&&a.bottom==r.bottom;}

static bool set_render(const RECT&mon,LONG w,LONG h){
    g_originX=mon.left;g_originY=mon.top;g_renderW=w;g_renderH=h;
    g_authoritativeChange=true;
    BOOL ok=SetWindowPos(g_game,nullptr,g_originX,g_originY,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    g_authoritativeChange=false;
    pump();
    return !!ok;
}

static bool set_output(const RECT&mon){
    ++g_outputMoves;
    BOOL ok=SetWindowPos(g_presenter,nullptr,mon.left,mon.top,mon.right-mon.left,mon.bottom-mon.top,SWP_NOZORDER|SWP_NOACTIVATE);
    pump();return !!ok;
}

int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"PTARLifeGame";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARLifePresenter";
    WNDCLASSW dc{};dc.hInstance=hi;dc.lpfnWndProc=DecoyProc;dc.lpszClassName=L"PTARLifeDecoy";
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&dc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;
    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,0,0,1280,720,nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,0,0,1920,1080,g_game,nullptr,hi,nullptr);
    g_decoy=CreateWindowExW(0,dc.lpszClassName,L"decoy",WS_OVERLAPPEDWINDOW,50,50,400,300,nullptr,nullptr,hi,nullptr);
    if(!g_game||!g_presenter||!g_decoy)return 11;
    ShowWindow(g_game,SW_SHOW);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);ShowWindow(g_decoy,SW_SHOWNA);pump();g_takeover=true;

    const std::vector<POINT>renders={{320,180},{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800},{1600,900}};
    const std::vector<RECT>monitors={{0,0,1024,768},{0,0,1366,768},{0,0,1920,1080},{0,0,2560,1440},{-1920,0,0,1080},{1920,-200,4480,1240},{-1280,-1024,0,0}};
    std::mt19937 rng(0x4C494645u);
    unsigned failures=0;

    for(unsigned i=0;i<6000;++i){
        POINT rs=renders[rng()%renders.size()];RECT mon=monitors[rng()%monitors.size()];
        if(!set_render(mon,rs.x,rs.y)||!set_output(mon))return 20;
        if(!client_is(g_game,rs.x,rs.y))++failures;
        if(!window_is(g_presenter,mon))++failures;
        if(GetWindow(g_presenter,GW_OWNER)!=g_game)++failures;
        DWORD pex=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
        if(!(pex&WS_EX_NOACTIVATE)||!(pex&WS_EX_TOOLWINDOW)||(pex&(WS_EX_TRANSPARENT|WS_EX_TOPMOST)))++failures;

        // Game tries to turn its logical HWND back into native/full-output geometry.
        SetWindowPos(g_game,nullptr,mon.left+17,mon.top+23,mon.right-mon.left,mon.bottom-mon.top,SWP_NOZORDER|SWP_NOACTIVATE);
        pump();++g_hostile;
        if(!client_is(g_game,rs.x,rs.y))++failures;
        RECT expectedGame{mon.left,mon.top,mon.left+rs.x,mon.top+rs.y};
        if(!window_is(g_game,expectedGame))++failures;

        if((i%53)==0){
            SetActiveWindow(g_decoy);SetFocus(g_decoy);
            if(GetActiveWindow()!=g_decoy)++failures;
            LRESULT ma=SendMessageW(g_presenter,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(g_decoy),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
            if(ma!=MA_NOACTIVATE||GetActiveWindow()!=g_game||GetFocus()!=g_game)++failures;
        }

        if((i%97)==0){
            ShowWindow(g_game,SW_MINIMIZE);pump();
            if(!IsIconic(g_game)||IsWindowVisible(g_presenter))++failures;
            ShowWindow(g_game,SW_RESTORE);SetActiveWindow(g_game);SetFocus(g_game);pump();
            if(IsIconic(g_game)||!IsWindowVisible(g_presenter)||GetWindow(g_presenter,GW_OWNER)!=g_game)++failures;
            if(!client_is(g_game,rs.x,rs.y)||!window_is(g_presenter,mon))++failures;
        }
    }

    if(failures){std::printf("FAIL failures=%u hostile=%llu output_moves=%llu minimize=%llu restore=%llu focus_return=%llu clamps=%llu\n",failures,g_hostile,g_outputMoves,g_minimize,g_restore,g_focusReturn,g_clamps);return 30;}
    std::printf("PASS PTAR_BORDERLESS_LIFECYCLE_STRESS\n");
    std::printf("cycles=6000 hostile_native_reassertions=%llu output_moves=%llu minimize_events=%llu restore_events=%llu focus_return=%llu clamps=%llu\n",g_hostile,g_outputMoves,g_minimize,g_restore,g_focusReturn,g_clamps);
    std::printf("contract=presenter_owned_not_topmost; minimize_hides_presenter; restore_shows_noactivate; alt_tab_return_targets_game; output_move_never_changes_logical_game_client\n");
    return 0;
}
