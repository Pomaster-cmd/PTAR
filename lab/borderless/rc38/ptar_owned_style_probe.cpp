#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>

static HWND g_game=nullptr,g_presenter=nullptr,g_decoy=nullptr;
static unsigned long long g_minimize=0,g_restore=0;

static void pump(){
    MSG m;
    while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_SIZE && g_presenter){
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
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK DecoyProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}

struct Counts{
    unsigned long long missingNoActivate=0;
    unsigned long long missingToolWindow=0;
    unsigned long long transparentSet=0;
    unsigned long long topmostSet=0;
    unsigned long long invalid=0;
};

static bool classify(DWORD ex,Counts& c){
    bool bad=false;
    if(!(ex&WS_EX_NOACTIVATE)){++c.missingNoActivate;bad=true;}
    if(!(ex&WS_EX_TOOLWINDOW)){++c.missingToolWindow;bad=true;}
    if(ex&WS_EX_TRANSPARENT){++c.transparentSet;bad=true;}
    if(ex&WS_EX_TOPMOST){++c.topmostSet;bad=true;}
    if(bad)++c.invalid;
    return bad;
}

int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"PTARStyleProbeGame";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARStyleProbePresenter";
    WNDCLASSW dc{};dc.hInstance=hi;dc.lpfnWndProc=DecoyProc;dc.lpszClassName=L"PTARStyleProbeDecoy";
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||
       (!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||
       (!RegisterClassW(&dc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)) return 10;

    POINT origin{0,0};
    HMONITOR hm=MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi)) return 11;
    RECT mon=mi.rcMonitor;
    LONG ow=mon.right-mon.left,oh=mon.bottom-mon.top;

    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,mon.left,mon.top,640,360,nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,mon.left,mon.top,ow,oh,g_game,nullptr,hi,nullptr);
    g_decoy=CreateWindowExW(0,dc.lpszClassName,L"decoy",WS_OVERLAPPEDWINDOW,mon.left+40,mon.top+40,320,240,nullptr,nullptr,hi,nullptr);
    if(!g_game||!g_presenter||!g_decoy)return 12;

    ShowWindow(g_game,SW_SHOW);
    ShowWindow(g_presenter,SW_SHOWNOACTIVATE);
    ShowWindow(g_decoy,SW_SHOWNA);
    pump();

    const DWORD initial=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
    DWORD previous=initial,firstBadEx=0;
    unsigned firstBadCycle=0xffffffffu;
    const char* firstBadPhase="none";
    Counts c{};

    auto sample=[&](unsigned cycle,const char* phase){
        DWORD ex=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
        if(ex!=previous){
            std::printf("STYLE_CHANGE cycle=%u phase=%s old=0x%08lX new=0x%08lX xor=0x%08lX\n",cycle,phase,(unsigned long)previous,(unsigned long)ex,(unsigned long)(previous^ex));
            previous=ex;
        }
        if(classify(ex,c) && firstBadCycle==0xffffffffu){firstBadCycle=cycle;firstBadEx=ex;firstBadPhase=phase;}
    };

    for(unsigned i=0;i<6000;++i){
        LONG rw=(i&1)?640:800,rh=(i&1)?360:450;
        SetWindowPos(g_game,nullptr,mon.left,mon.top,rw,rh,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g_presenter,nullptr,mon.left,mon.top,ow,oh,SWP_NOZORDER|SWP_NOACTIVATE);
        pump();
        sample(i,"pre");

        if((i%53)==0){
            SetActiveWindow(g_decoy);SetFocus(g_decoy);
            SendMessageW(g_presenter,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(g_decoy),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
            pump();
            sample(i,"focus");
        }

        if((i%97)==0){
            sample(i,"before_min");
            ShowWindow(g_game,SW_MINIMIZE);pump();sample(i,"after_min");
            ShowWindow(g_game,SW_RESTORE);pump();sample(i,"after_restore");
            SetWindowPos(g_game,nullptr,mon.left,mon.top,rw,rh,SWP_NOZORDER|SWP_NOACTIVATE);
            SetWindowPos(g_presenter,nullptr,mon.left,mon.top,ow,oh,SWP_NOZORDER|SWP_NOACTIVATE);
            SetActiveWindow(g_game);SetFocus(g_game);pump();sample(i,"after_reapply");
        }
    }

    unsigned first=(firstBadCycle==0xffffffffu)?999999u:firstBadCycle;
    std::printf("STYLE_PROBE initial=0x%08lX final=0x%08lX first_bad_cycle=%u first_bad_phase=%s first_bad_ex=0x%08lX invalid_samples=%llu missing_noactivate=%llu missing_toolwindow=%llu transparent_set=%llu topmost_set=%llu minimize_msgs=%llu restore_msgs=%llu\n",
        (unsigned long)initial,(unsigned long)previous,first,firstBadPhase,(unsigned long)firstBadEx,c.invalid,c.missingNoActivate,c.missingToolWindow,c.transparentSet,c.topmostSet,g_minimize,g_restore);
    return 0;
}
