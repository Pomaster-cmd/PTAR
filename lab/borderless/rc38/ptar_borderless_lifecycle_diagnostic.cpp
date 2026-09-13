#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include <cstdint>

static HWND g_game=nullptr,g_presenter=nullptr,g_decoy=nullptr;
static bool g_takeover=false;
static bool g_authoritativeChange=false;
static bool g_lifecycleTransition=false;
static LONG g_renderW=640,g_renderH=360,g_originX=0,g_originY=0;
static unsigned long long g_minimizeMsg=0,g_restoreMsg=0,g_explicitMinRestore=0;
static unsigned long long g_focusReturn=0,g_clamps=0,g_outputSets=0,g_hostile=0;
static unsigned long long g_presenterHide=0,g_presenterShow=0;

struct FailureCounters {
    unsigned long long preClient=0;
    unsigned long long prePresenterRect=0;
    unsigned long long owner=0;
    unsigned long long style=0;
    unsigned long long hostileClient=0;
    unsigned long long hostileRect=0;
    unsigned long long focusDecoy=0;
    unsigned long long focusReturnContract=0;
    unsigned long long minIconic=0;
    unsigned long long minPresenterHidden=0;
    unsigned long long restoreNotIconic=0;
    unsigned long long restorePresenterVisible=0;
    unsigned long long restoreOwner=0;
    unsigned long long restoreClient=0;
    unsigned long long restorePresenterRect=0;
    unsigned long long finalFocusCount=0;
    unsigned long long finalMinMsgCount=0;
};

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
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"PTARLifeDiagGame";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARLifeDiagPresenter";
    WNDCLASSW dc{};dc.hInstance=hi;dc.lpfnWndProc=DecoyProc;dc.lpszClassName=L"PTARLifeDiagDecoy";
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
    FailureCounters fc{};
    unsigned firstFailureCycle=0xffffffffu;
    unsigned lastFailureCycle=0;

    auto mark=[&](bool ok,unsigned long long& counter,unsigned cycle){
        if(ok) return;
        ++failures;
        ++counter;
        if(firstFailureCycle==0xffffffffu) firstFailureCycle=cycle;
        lastFailureCycle=cycle;
    };
    auto mark_reason=[&](bool ok,unsigned long long& counter,unsigned cycle){
        if(ok) return;
        ++counter;
        if(firstFailureCycle==0xffffffffu) firstFailureCycle=cycle;
        lastFailureCycle=cycle;
    };

    for(unsigned i=0;i<6000;++i){
        const POINT rs=renders[rng()%renders.size()];
        if(!set_render(mon,rs.x,rs.y)||!set_output(mon)){
            std::printf("DIAG_FATAL cycle=%u stage=set_render_or_output\n",i);
            return 20;
        }

        mark(client_is(g_game,rs.x,rs.y),fc.preClient,i);
        mark(window_is(g_presenter,mon),fc.prePresenterRect,i);
        mark(GetWindow(g_presenter,GW_OWNER)==g_game,fc.owner,i);
        const DWORD pex=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
        mark((pex&WS_EX_NOACTIVATE)&&(pex&WS_EX_TOOLWINDOW)&&!(pex&(WS_EX_TRANSPARENT|WS_EX_TOPMOST)),fc.style,i);

        SetWindowPos(g_game,nullptr,mon.left+17,mon.top+23,outW,outH,SWP_NOZORDER|SWP_NOACTIVATE);
        pump();
        ++g_hostile;
        mark(client_is(g_game,rs.x,rs.y),fc.hostileClient,i);
        const RECT expectedGame{mon.left,mon.top,mon.left+rs.x,mon.top+rs.y};
        mark(window_is(g_game,expectedGame),fc.hostileRect,i);

        if((i%53)==0){
            SetActiveWindow(g_decoy);
            SetFocus(g_decoy);
            mark(GetActiveWindow()==g_decoy&&GetFocus()==g_decoy,fc.focusDecoy,i);
            const LRESULT ma=SendMessageW(g_presenter,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(g_decoy),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
            mark(ma==MA_NOACTIVATE&&GetActiveWindow()==g_game&&GetFocus()==g_game,fc.focusReturnContract,i);
            ++explicitFocusCases;
        }

        if((i%97)==0){
            g_lifecycleTransition=true;
            ShowWindow(g_game,SW_MINIMIZE);
            pump();
            const bool minIconic=IsIconic(g_game)!=FALSE;
            const bool minPresenterHidden=IsWindowVisible(g_presenter)==FALSE;

            ShowWindow(g_game,SW_RESTORE);
            pump();
            g_lifecycleTransition=false;
            if(!set_render(mon,rs.x,rs.y)||!set_output(mon)){
                std::printf("DIAG_FATAL cycle=%u stage=restore_reapply\n",i);
                return 21;
            }
            SetActiveWindow(g_game);
            SetFocus(g_game);
            pump();

            const bool restoreNotIconic=IsIconic(g_game)==FALSE;
            const bool restorePresenterVisible=IsWindowVisible(g_presenter)!=FALSE;
            const bool restoreOwner=GetWindow(g_presenter,GW_OWNER)==g_game;
            const bool restoreClient=client_is(g_game,rs.x,rs.y);
            const bool restorePresenterRect=window_is(g_presenter,mon);

            const bool minOk=minIconic&&minPresenterHidden;
            const bool restoreOk=restoreNotIconic&&restorePresenterVisible&&restoreOwner&&restoreClient&&restorePresenterRect;
            if(!minOk||!restoreOk){
                ++failures;
                if(firstFailureCycle==0xffffffffu) firstFailureCycle=i;
                lastFailureCycle=i;
            }
            mark_reason(minIconic,fc.minIconic,i);
            mark_reason(minPresenterHidden,fc.minPresenterHidden,i);
            mark_reason(restoreNotIconic,fc.restoreNotIconic,i);
            mark_reason(restorePresenterVisible,fc.restorePresenterVisible,i);
            mark_reason(restoreOwner,fc.restoreOwner,i);
            mark_reason(restoreClient,fc.restoreClient,i);
            mark_reason(restorePresenterRect,fc.restorePresenterRect,i);
            ++g_explicitMinRestore;
        }
    }

    mark(g_focusReturn==explicitFocusCases,fc.finalFocusCount,6000);
    mark(g_explicitMinRestore!=0&&g_minimizeMsg>=g_explicitMinRestore,fc.finalMinMsgCount,6000);

    const unsigned first=(firstFailureCycle==0xffffffffu)?999999u:firstFailureCycle;
    std::printf("DIAG failures=%u first_cycle=%u last_cycle=%u hostile=%llu output_sets=%llu explicit_minrestore=%llu minimize_msgs=%llu restore_msgs=%llu focus_cases=%llu focus_return=%llu clamps=%llu output=%ldx%ld\n",
        failures,first,lastFailureCycle,g_hostile,g_outputSets,g_explicitMinRestore,g_minimizeMsg,g_restoreMsg,explicitFocusCases,g_focusReturn,g_clamps,outW,outH);
    std::printf("COUNTERS pre_client=%llu pre_presenter_rect=%llu owner=%llu style=%llu hostile_client=%llu hostile_rect=%llu focus_decoy=%llu focus_return_contract=%llu min_iconic=%llu min_presenter_hidden=%llu restore_not_iconic=%llu restore_presenter_visible=%llu restore_owner=%llu restore_client=%llu restore_presenter_rect=%llu final_focus_count=%llu final_minmsg_count=%llu\n",
        fc.preClient,fc.prePresenterRect,fc.owner,fc.style,fc.hostileClient,fc.hostileRect,fc.focusDecoy,fc.focusReturnContract,fc.minIconic,fc.minPresenterHidden,fc.restoreNotIconic,fc.restorePresenterVisible,fc.restoreOwner,fc.restoreClient,fc.restorePresenterRect,fc.finalFocusCount,fc.finalMinMsgCount);
    std::printf("CONTRACT real_monitor_only; presenter_owned_not_topmost; lifecycle_transition_bypasses_geometry_clamp; restore_reapplies_authoritative_geometry; alt_tab_return_targets_game\n");
    return failures?30:0;
}
