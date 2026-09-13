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
static bool g_lifecycleTransition=false;
static LONG g_renderW=640,g_renderH=360,g_originX=0,g_originY=0;
static unsigned long long g_minimizeMsg=0,g_restoreMsg=0,g_explicitMinRestore=0;
static unsigned long long g_focusReturn=0,g_clamps=0,g_outputSets=0,g_hostile=0;
static unsigned long long g_presenterHide=0,g_presenterShow=0;

static void pump(){
    MSG m;
    while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(g_takeover&&!g_authoritativeChange&&!g_lifecycleTransition&&m==WM_WINDOWPOSCHANGING){
        WINDOWPOS*p=reinterpret_cast<WINDOWPOS*>(l);
        if(!(p->flags&SWP_NOMOVE)){p->x=g_originX;p->y=g_originY;}
        if(!(p->flags&SWP_NOSIZE)){p->cx=g_renderW;p->cy=g_renderH;}
        ++g_clamps;
        return 0;
    }
    if(m==WM_SIZE&&g_takeover&&g_presenter){
        if(w==SIZE_MINIMIZED){
            ShowWindow(g_presenter,SW_HIDE);
            ++g_minimizeMsg;
            ++g_presenterHide;
        }else if(w==SIZE_RESTORED){
            // WM_SIZE(SIZE_RESTORED) also occurs during ordinary authoritative
            // logical resizes. Re-showing an already visible owned presenter is
            // harmless; do not interpret this counter as an explicit restore count.
            ShowWindow(g_presenter,SW_SHOWNOACTIVATE);
            ++g_restoreMsg;
            ++g_presenterShow;
        }
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

static LRESULT CALLBACK DecoyProc(HWND h,UINT m,WPARAM w,LPARAM l){
    return DefWindowProcW(h,m,w,l);
}

static bool client_is(HWND h,LONG w,LONG hh){
    RECT r{};
    return GetClientRect(h,&r)&&r.left==0&&r.top==0&&r.right==w&&r.bottom==hh;
}

static bool window_is(HWND h,const RECT&r){
    RECT a{};
    return GetWindowRect(h,&a)&&a.left==r.left&&a.top==r.top&&a.right==r.right&&a.bottom==r.bottom;
}

static bool set_render(const RECT&mon,LONG w,LONG h){
    g_originX=mon.left;
    g_originY=mon.top;
    g_renderW=w;
    g_renderH=h;
    g_authoritativeChange=true;
    const BOOL ok=SetWindowPos(g_game,nullptr,g_originX,g_originY,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    g_authoritativeChange=false;
    pump();
    return !!ok;
}

static bool set_output(const RECT&mon){
    ++g_outputSets;
    const BOOL ok=SetWindowPos(g_presenter,nullptr,mon.left,mon.top,mon.right-mon.left,mon.bottom-mon.top,SWP_NOZORDER|SWP_NOACTIVATE);
    pump();
    return !!ok;
}

int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"PTARLifeGame";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARLifePresenter";
    WNDCLASSW dc{};dc.hInstance=hi;dc.lpfnWndProc=DecoyProc;dc.lpszClassName=L"PTARLifeDecoy";
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||
       (!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||
       (!RegisterClassW(&dc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)) return 10;

    POINT origin{0,0};
    HMONITOR hm=MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi)) return 11;
    const RECT mon=mi.rcMonitor;
    const LONG outW=mon.right-mon.left;
    const LONG outH=mon.bottom-mon.top;
    if(outW<320||outH<180) return 12;

    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,mon.left,mon.top,640,360,nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,mon.left,mon.top,outW,outH,g_game,nullptr,hi,nullptr);
    g_decoy=CreateWindowExW(0,dc.lpszClassName,L"decoy",WS_OVERLAPPEDWINDOW,mon.left+40,mon.top+40,320,240,nullptr,nullptr,hi,nullptr);
    if(!g_game||!g_presenter||!g_decoy) return 13;

    ShowWindow(g_game,SW_SHOW);
    ShowWindow(g_presenter,SW_SHOWNOACTIVATE);
    ShowWindow(g_decoy,SW_SHOWNA);
    pump();
    g_takeover=true;

    std::vector<POINT> renders;
    auto add_render=[&](LONG w,LONG h){
        w=(std::max)(1L,(std::min)(w,outW));
        h=(std::max)(1L,(std::min)(h,outH));
        for(const POINT&p:renders) if(p.x==w&&p.y==h) return;
        renders.push_back(POINT{w,h});
    };
    add_render(320,180);
    add_render(640,360);
    add_render(800,450);
    add_render(960,540);
    add_render(1024,576);
    add_render(1280,720);
    add_render(outW/2,outH/2);
    add_render((outW*3)/4,(outH*3)/4);
    add_render(outW,outH);

    std::mt19937 rng(0x4C494645u);
    unsigned failures=0;
    unsigned long long explicitFocusCases=0;

    for(unsigned i=0;i<6000;++i){
        const POINT rs=renders[rng()%renders.size()];
        if(!set_render(mon,rs.x,rs.y)||!set_output(mon)) return 20;

        if(!client_is(g_game,rs.x,rs.y)) ++failures;
        if(!window_is(g_presenter,mon)) ++failures;
        if(GetWindow(g_presenter,GW_OWNER)!=g_game) ++failures;
        const DWORD pex=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
        if(!(pex&WS_EX_NOACTIVATE)||!(pex&WS_EX_TOOLWINDOW)||(pex&(WS_EX_TRANSPARENT|WS_EX_TOPMOST))) ++failures;

        // Hostile game request: expand/move the logical HWND to native output.
        // The authority guard must clamp it back to render/backbuffer geometry.
        SetWindowPos(g_game,nullptr,mon.left+17,mon.top+23,outW,outH,SWP_NOZORDER|SWP_NOACTIVATE);
        pump();
        ++g_hostile;
        if(!client_is(g_game,rs.x,rs.y)) ++failures;
        const RECT expectedGame{mon.left,mon.top,mon.left+rs.x,mon.top+rs.y};
        if(!window_is(g_game,expectedGame)) ++failures;

        if((i%53)==0){
            SetActiveWindow(g_decoy);
            SetFocus(g_decoy);
            if(GetActiveWindow()!=g_decoy||GetFocus()!=g_decoy) ++failures;
            const LRESULT ma=SendMessageW(g_presenter,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(g_decoy),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
            if(ma!=MA_NOACTIVATE||GetActiveWindow()!=g_game||GetFocus()!=g_game) ++failures;
            ++explicitFocusCases;
        }

        if((i%97)==0){
            // Minimize/restore is a legitimate OS lifecycle transition, not a
            // hostile geometry mutation. The authority clamp is suspended only
            // for this explicit transition, then authoritative geometry is
            // reapplied before normal takeover resumes.
            g_lifecycleTransition=true;
            ShowWindow(g_game,SW_MINIMIZE);
            pump();
            const bool minOk=IsIconic(g_game)&&!IsWindowVisible(g_presenter);

            ShowWindow(g_game,SW_RESTORE);
            pump();
            g_lifecycleTransition=false;
            if(!set_render(mon,rs.x,rs.y)||!set_output(mon)) return 21;
            SetActiveWindow(g_game);
            SetFocus(g_game);
            pump();

            const bool restoreOk=!IsIconic(g_game)&&IsWindowVisible(g_presenter)&&
                                 GetWindow(g_presenter,GW_OWNER)==g_game&&
                                 client_is(g_game,rs.x,rs.y)&&window_is(g_presenter,mon);
            if(!minOk||!restoreOk) ++failures;
            ++g_explicitMinRestore;
        }
    }

    if(g_focusReturn!=explicitFocusCases) ++failures;
    if(g_explicitMinRestore==0||g_minimizeMsg<g_explicitMinRestore) ++failures;

    if(failures){
        std::printf("FAIL failures=%u hostile=%llu output_sets=%llu explicit_minrestore=%llu minimize_msgs=%llu restore_msgs=%llu focus_cases=%llu focus_return=%llu clamps=%llu output=%ldx%ld\n",
            failures,g_hostile,g_outputSets,g_explicitMinRestore,g_minimizeMsg,g_restoreMsg,explicitFocusCases,g_focusReturn,g_clamps,outW,outH);
        return 30;
    }

    std::printf("PASS PTAR_BORDERLESS_LIFECYCLE_STRESS\n");
    std::printf("cycles=6000 hostile_native_reassertions=%llu output_sets=%llu explicit_minrestore=%llu minimize_msgs=%llu restore_msgs=%llu focus_return=%llu clamps=%llu\n",
        g_hostile,g_outputSets,g_explicitMinRestore,g_minimizeMsg,g_restoreMsg,g_focusReturn,g_clamps);
    std::printf("presenter_hide_calls=%llu presenter_show_calls=%llu actual_output=%ldx%ld\n",g_presenterHide,g_presenterShow,outW,outH);
    std::printf("contract=real_monitor_only; presenter_owned_not_topmost; lifecycle_transition_bypasses_geometry_clamp; restore_reapplies_authoritative_geometry; alt_tab_return_targets_game\n");
    return 0;
}
