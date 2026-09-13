#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>

static HWND g_game=nullptr,g_presenter=nullptr;
static LONG g_renderW=1280,g_renderH=720,g_originX=0,g_originY=0;
static bool g_takeover=false;
static unsigned long long g_gameSize=0,g_clamps=0,g_styleClamps=0,g_presenterSize=0;

static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(g_takeover && m==WM_WINDOWPOSCHANGING){
        WINDOWPOS* p=reinterpret_cast<WINDOWPOS*>(l);
        if(!(p->flags&SWP_NOMOVE)){p->x=g_originX;p->y=g_originY;}
        if(!(p->flags&SWP_NOSIZE)){p->cx=g_renderW;p->cy=g_renderH;}
        ++g_clamps;
        return 0;
    }
    if(g_takeover && m==WM_STYLECHANGING && w==GWL_STYLE){
        STYLESTRUCT* s=reinterpret_cast<STYLESTRUCT*>(l);
        s->styleNew=(s->styleNew|WS_POPUP)&~(WS_CAPTION|WS_THICKFRAME|WS_CHILD);
        ++g_styleClamps;
        return 0;
    }
    if(m==WM_SIZE)++g_gameSize;
    return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_SIZE)++g_presenterSize;
    return DefWindowProcW(h,m,w,l);
}

static bool exact_client(HWND h,LONG w,LONG hh){RECT r{};return GetClientRect(h,&r)&&r.left==0&&r.top==0&&r.right==w&&r.bottom==hh;}
static bool exact_window(HWND h,LONG x,LONG y,LONG w,LONG hh){RECT r{};return GetWindowRect(h,&r)&&r.left==x&&r.top==y&&r.right-r.left==w&&r.bottom-r.top==hh;}

int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW a{};a.hInstance=hi;a.lpfnWndProc=GameProc;a.lpszClassName=L"PTARLabGameGuard";
    WNDCLASSW b{};b.hInstance=hi;b.lpfnWndProc=PresenterProc;b.lpszClassName=L"PTARLabPresenterGuard";
    if((!RegisterClassW(&a)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&b)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;
    g_game=CreateWindowExW(0,a.lpszClassName,L"game",WS_POPUP,0,0,g_renderW,g_renderH,nullptr,nullptr,hi,nullptr);
    if(!g_game)return 11;
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,b.lpszClassName,L"presenter",WS_POPUP,0,0,1920,1080,g_game,nullptr,hi,nullptr);
    if(!g_presenter)return 12;
    ShowWindow(g_game,SW_SHOW);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump();
    g_takeover=true;

    DWORD ps=DWORD(GetWindowLongPtrW(g_presenter,GWL_STYLE)),pe=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
    if(!(ps&WS_POPUP)||(ps&(WS_CAPTION|WS_THICKFRAME))||!(pe&WS_EX_NOACTIVATE)||!(pe&WS_EX_TOOLWINDOW)||(pe&WS_EX_TRANSPARENT))return 13;
    if(GetWindow(g_presenter,GW_OWNER)!=g_game)return 14;
    if(SendMessageW(g_presenter,WM_MOUSEACTIVATE,(WPARAM)g_game,MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN))!=MA_NOACTIVATE)return 15;

    std::mt19937 rng(0x424F5244u);
    const std::vector<POINT> renders={{320,180},{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800},{1600,900}};
    const std::vector<RECT> monitors={{0,0,1024,768},{0,0,1920,1080},{-1920,0,0,1080},{1920,-200,4480,1240},{-1280,-1024,0,0}};
    unsigned failures=0;
    unsigned long long hostileCalls=0,outputChanges=0,styleAttacks=0,minRestore=0;

    for(unsigned i=0;i<100000;++i){
        POINT rr=renders[rng()%renders.size()];RECT mon=monitors[rng()%monitors.size()];
        g_renderW=rr.x;g_renderH=rr.y;g_originX=mon.left;g_originY=mon.top;

        // PTAR-owned logical change: temporarily perform the authoritative low-res resize.
        g_takeover=false;
        SetWindowLongPtrW(g_game,GWL_STYLE,WS_POPUP);
        SetWindowPos(g_game,nullptr,g_originX,g_originY,g_renderW,g_renderH,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        pump();
        g_takeover=true;

        // Hostile/native reassertion from the game: the guard must reject it.
        LONG hx=mon.left+LONG(int(rng()%2001)-1000),hy=mon.top+LONG(int(rng()%2001)-1000);
        LONG hw=LONG(100+rng()%3000),hh=LONG(100+rng()%2000);
        SetWindowPos(g_game,nullptr,hx,hy,hw,hh,SWP_NOZORDER|SWP_NOACTIVATE);
        pump();++hostileCalls;
        if(!exact_client(g_game,g_renderW,g_renderH)||!exact_window(g_game,g_originX,g_originY,g_renderW,g_renderH))++failures;

        if((i%5)==0){
            LONG before=LONG(g_gameSize);
            LONG ow=mon.right-mon.left,oh=mon.bottom-mon.top;
            SetWindowPos(g_presenter,nullptr,mon.left,mon.top,ow,oh,SWP_NOZORDER|SWP_NOACTIVATE);
            pump();++outputChanges;
            if(LONG(g_gameSize)!=before)++failures;
            if(!exact_window(g_presenter,mon.left,mon.top,ow,oh))++failures;
            if(!exact_client(g_game,g_renderW,g_renderH))++failures;
        }

        if((i%17)==0){
            SetWindowLongPtrW(g_game,GWL_STYLE,WS_OVERLAPPEDWINDOW);
            SetWindowPos(g_game,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
            pump();++styleAttacks;
            DWORD s=DWORD(GetWindowLongPtrW(g_game,GWL_STYLE));
            if(!(s&WS_POPUP)||(s&(WS_CAPTION|WS_THICKFRAME|WS_CHILD)))++failures;
            if(!exact_client(g_game,g_renderW,g_renderH))++failures;
        }

        if((i%997)==0){
            ShowWindow(g_game,SW_MINIMIZE);pump();
            ShowWindow(g_game,SW_RESTORE);pump();
            ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump();
            ++minRestore;
            if(GetWindow(g_presenter,GW_OWNER)!=g_game)++failures;
            if(!exact_client(g_game,g_renderW,g_renderH))++failures;
        }
    }

    if(failures){std::printf("FAIL failures=%u hostile=%llu output_changes=%llu style_attacks=%llu min_restore=%llu\n",failures,hostileCalls,outputChanges,styleAttacks,minRestore);return 20;}
    std::printf("PASS PTAR_BORDERLESS_WINDOW_STRESS\n");
    std::printf("hostile_game_window_requests=%llu output_only_changes=%llu style_attacks=%llu minimize_restore_cycles=%llu\n",hostileCalls,outputChanges,styleAttacks,minRestore);
    std::printf("windowpos_clamps=%llu style_clamps=%llu game_size_events=%llu presenter_size_events=%llu\n",g_clamps,g_styleClamps,g_gameSize,g_presenterSize);
    std::printf("invariant=game_popup_client_equals_logical_render; presenter_popup_equals_monitor; presenter_owned_noactivate\n");
    return 0;
}
