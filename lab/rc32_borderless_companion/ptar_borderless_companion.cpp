#define PTAR_BORDERLESS_EXPORTS
#include "ptar_borderless_api.h"
#include "ptar_borderless_geometry.h"
#include <atomic>
#include <stdint.h>

namespace {
using ptar_bctl::Geometry;

struct State {
    HMODULE self=nullptr;
    HWND game=nullptr;
    HWND presenter=nullptr;
    WNDPROC oldGame=nullptr;
    WNDPROC oldPresenter=nullptr;
    Geometry geo{};
    DWORD gameTid=0;
    DWORD presenterTid=0;
    std::atomic<uint32_t> status{0};
    std::atomic<LONG> internalMove{0};
    LONG_PTR oldGameStyle=0, oldGameEx=0, oldPresenterStyle=0, oldPresenterEx=0;
    RECT oldGameRect{}, oldPresenterRect{};
};
State g;

bool pointer_msg(UINT m){
    switch(m){
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        return true;
    default:return false;
    }
}

void monitor_rect_from_game(){
    HMONITOR m=MonitorFromWindow(g.game,MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if(m&&GetMonitorInfoW(m,&mi)){
        RECT mr=mi.rcMonitor;
        LONG ow=LONG(g.geo.output.right-g.geo.output.left), oh=LONG(g.geo.output.bottom-g.geo.output.top);
        g.geo.output.left=mr.left; g.geo.output.top=mr.top;
        g.geo.output.right=mr.left+ow; g.geo.output.bottom=mr.top+oh;
    }
}

void apply_game_geometry(){
    if(!g.game||!g.geo.valid()) return;
    g.internalMove.fetch_add(1);
    LONG_PTR s=GetWindowLongPtrW(g.game,GWL_STYLE);
    s|=WS_POPUP;
    s&=~LONG_PTR(WS_CAPTION|WS_THICKFRAME|WS_CHILD);
    SetWindowLongPtrW(g.game,GWL_STYLE,s);
    SetWindowPos(g.game,nullptr,g.geo.output.left,g.geo.output.top,LONG(g.geo.renderW),LONG(g.geo.renderH),
        SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    g.internalMove.fetch_sub(1);
}

void apply_presenter_geometry(){
    if(!g.presenter||!g.geo.valid()) return;
    g.internalMove.fetch_add(1);
    LONG_PTR s=GetWindowLongPtrW(g.presenter,GWL_STYLE);
    LONG_PTR ex=GetWindowLongPtrW(g.presenter,GWL_EXSTYLE);
    s|=WS_POPUP|WS_VISIBLE;
    s&=~LONG_PTR(WS_CAPTION|WS_THICKFRAME|WS_CHILD);
    ex|=WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
    ex&=~LONG_PTR(WS_EX_TRANSPARENT|WS_EX_TOPMOST);
    SetWindowLongPtrW(g.presenter,GWL_STYLE,s);
    SetWindowLongPtrW(g.presenter,GWL_EXSTYLE,ex);
    SetWindowPos(g.presenter,HWND_TOP,g.geo.output.left,g.geo.output.top,
        g.geo.output.right-g.geo.output.left,g.geo.output.bottom-g.geo.output.top,
        SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    g.internalMove.fetch_sub(1);
}

void refocus_game(){
    if(!g.game) return;
    SetForegroundWindow(g.game);
    if(g.presenterTid==g.gameTid){ SetFocus(g.game); return; }
    if(g.presenterTid&&g.gameTid&&AttachThreadInput(g.presenterTid,g.gameTid,TRUE)){
        SetFocus(g.game);
        AttachThreadInput(g.presenterTid,g.gameTid,FALSE);
    }
}

LRESULT call_game(UINT m,WPARAM w,LPARAM l){
    return g.oldGame?CallWindowProcW(g.oldGame,g.game,m,w,l):DefWindowProcW(g.game,m,w,l);
}
LRESULT call_presenter(UINT m,WPARAM w,LPARAM l){
    return g.oldPresenter?CallWindowProcW(g.oldPresenter,g.presenter,m,w,l):DefWindowProcW(g.presenter,m,w,l);
}

LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(h!=g.presenter) return DefWindowProcW(h,m,w,l);
    if(m==WM_NCHITTEST) return HTCLIENT;
    if(m==WM_MOUSEACTIVATE){ refocus_game(); return MA_NOACTIVATE; }
    if(m==WM_SETFOCUS){ refocus_game(); return 0; }
    if(m==WM_SETCURSOR){
        if(g.game&&g.oldGame) return CallWindowProcW(g.oldGame,g.game,WM_SETCURSOR,reinterpret_cast<WPARAM>(g.game),l);
        return TRUE;
    }
    if(pointer_msg(m)){
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        ClientToScreen(h,&p);
        POINT q=g.geo.physicalScreenToLogicalClient(p);
        if(m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN||m==WM_MBUTTONDOWN||m==WM_XBUTTONDOWN) refocus_game();
        DWORD_PTR ignored=0;
        SendMessageTimeoutW(g.game,m,w,MAKELPARAM(q.x,q.y),SMTO_ABORTIFHUNG|SMTO_BLOCK,100,&ignored);
        return 0;
    }
    if(m==WM_MOUSELEAVE){ PostMessageW(g.game,m,w,l); return 0; }
    return call_presenter(m,w,l);
}

LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(h!=g.game) return DefWindowProcW(h,m,w,l);
    if((g.status.load()&PTAR_BCTL_ACTIVE)&&!g.internalMove.load()){
        if(m==WM_WINDOWPOSCHANGING){
            WINDOWPOS* p=reinterpret_cast<WINDOWPOS*>(l);
            if(p && !(p->flags&SWP_HIDEWINDOW) && p->x>-30000 && p->y>-30000){
                if(!(p->flags&SWP_NOMOVE)){p->x=g.geo.output.left;p->y=g.geo.output.top;}
                if(!(p->flags&SWP_NOSIZE)){p->cx=LONG(g.geo.renderW);p->cy=LONG(g.geo.renderH);}
            }
        } else if(m==WM_STYLECHANGING && w==GWL_STYLE){
            STYLESTRUCT* ss=reinterpret_cast<STYLESTRUCT*>(l);
            if(ss){ss->styleNew|=WS_POPUP;ss->styleNew&=~LONG_PTR(WS_CAPTION|WS_THICKFRAME|WS_CHILD);}
        } else if(m==WM_MOUSEWHEEL||m==WM_MOUSEHWHEEL){
            POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            POINT q=g.geo.physicalScreenToLogicalScreen(p);
            return call_game(m,w,MAKELPARAM(q.x,q.y));
        } else if(m==WM_SIZE){
            if(w==SIZE_MINIMIZED){ShowWindow(g.presenter,SW_HIDE);}
            else if(w==SIZE_RESTORED){ShowWindow(g.presenter,SW_SHOWNOACTIVATE);}
        } else if(m==WM_DISPLAYCHANGE){
            monitor_rect_from_game(); apply_game_geometry(); apply_presenter_geometry();
        }
    }
    return call_game(m,w,l);
}

bool dpi_safe(){
    // Windows 8.1 has process-level awareness. Do not change it here. For the
    // first product integration, require identical logical/physical system DPI;
    // otherwise fail-open rather than corrupt another title's coordinate space.
    HDC dc=GetDC(nullptr); if(!dc) return false;
    int dx=GetDeviceCaps(dc,LOGPIXELSX),dy=GetDeviceCaps(dc,LOGPIXELSY); ReleaseDC(nullptr,dc);
    return dx==96 && dy==96;
}

void rollback_partial(){
    if(g.presenter&&g.oldPresenter){SetWindowLongPtrW(g.presenter,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(g.oldPresenter));g.oldPresenter=nullptr;}
    if(g.game&&g.oldGame){SetWindowLongPtrW(g.game,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(g.oldGame));g.oldGame=nullptr;}
    if(g.presenter&&IsWindow(g.presenter)){
        SetWindowLongPtrW(g.presenter,GWL_STYLE,g.oldPresenterStyle);SetWindowLongPtrW(g.presenter,GWL_EXSTYLE,g.oldPresenterEx);
        SetWindowPos(g.presenter,nullptr,g.oldPresenterRect.left,g.oldPresenterRect.top,g.oldPresenterRect.right-g.oldPresenterRect.left,g.oldPresenterRect.bottom-g.oldPresenterRect.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
    if(g.game&&IsWindow(g.game)){
        SetWindowLongPtrW(g.game,GWL_STYLE,g.oldGameStyle);SetWindowLongPtrW(g.game,GWL_EXSTYLE,g.oldGameEx);
        SetWindowPos(g.game,nullptr,g.oldGameRect.left,g.oldGameRect.top,g.oldGameRect.right-g.oldGameRect.left,g.oldGameRect.bottom-g.oldGameRect.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
    g.status.store(PTAR_BCTL_FAIL_OPEN);
}
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){ if(reason==DLL_PROCESS_ATTACH){g.self=h;DisableThreadLibraryCalls(h);} return TRUE; }

int WINAPI PTAR_BorderlessAttach(HWND game,HWND presenter,uint32_t rw,uint32_t rh,uint32_t ow,uint32_t oh){
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)||!rw||!rh||!ow||!oh) return 10;
    if(g.status.load()&PTAR_BCTL_ACTIVE) return 0;
    g.game=game;g.presenter=presenter;g.gameTid=GetWindowThreadProcessId(game,nullptr);g.presenterTid=GetWindowThreadProcessId(presenter,nullptr);
    g.geo.renderW=rw;g.geo.renderH=rh;g.geo.output=RECT{0,0,LONG(ow),LONG(oh)};monitor_rect_from_game();
    if(!dpi_safe()){g.status.store(PTAR_BCTL_FAIL_OPEN);return 20;}
    g.status.store(PTAR_BCTL_DPI_SAFE);
    g.oldGameStyle=GetWindowLongPtrW(game,GWL_STYLE);g.oldGameEx=GetWindowLongPtrW(game,GWL_EXSTYLE);GetWindowRect(game,&g.oldGameRect);
    g.oldPresenterStyle=GetWindowLongPtrW(presenter,GWL_STYLE);g.oldPresenterEx=GetWindowLongPtrW(presenter,GWL_EXSTYLE);GetWindowRect(presenter,&g.oldPresenterRect);
    SetLastError(0);LONG_PTR pg=SetWindowLongPtrW(game,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&GameProc));if(!pg&&GetLastError()){rollback_partial();return 30;}g.oldGame=reinterpret_cast<WNDPROC>(pg);g.status.fetch_or(PTAR_BCTL_GAME_SUBCLASSED);
    SetLastError(0);LONG_PTR pp=SetWindowLongPtrW(presenter,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&PresenterProc));if(!pp&&GetLastError()){rollback_partial();return 31;}g.oldPresenter=reinterpret_cast<WNDPROC>(pp);g.status.fetch_or(PTAR_BCTL_PRESENTER_SUBCLASSED);
    apply_game_geometry();apply_presenter_geometry();
    RECT c{};GetClientRect(game,&c);if(c.right!=LONG(rw)||c.bottom!=LONG(rh)){rollback_partial();return 40;}
    LONG_PTR ex=GetWindowLongPtrW(presenter,GWL_EXSTYLE);LONG_PTR st=GetWindowLongPtrW(presenter,GWL_STYLE);
    if(!(st&WS_POPUP)||(st&(WS_CAPTION|WS_THICKFRAME))||(ex&WS_EX_TRANSPARENT)||(ex&WS_EX_TOPMOST)||!(ex&WS_EX_NOACTIVATE)){rollback_partial();return 41;}
    g.status.fetch_or(PTAR_BCTL_GEOMETRY_LOCKED|PTAR_BCTL_INPUT_ROUTING|PTAR_BCTL_ACTIVE);
    return 0;
}

void WINAPI PTAR_BorderlessDetach(void){ if(!(g.status.load()&PTAR_BCTL_ACTIVE)) return; rollback_partial(); g.status.store(0); }
uint32_t WINAPI PTAR_BorderlessStatus(void){ return g.status.load(); }
