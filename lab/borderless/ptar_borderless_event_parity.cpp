#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <random>
#include <algorithm>
#include "include/ptar_borderless_geometry.h"

using ptar_lab::Geometry;

struct Event {
    UINT msg{};
    WPARAM w{};
    LONG x{};
    LONG y{};
};

static HWND g_game = nullptr;
static HWND g_presenter = nullptr;
static HWND g_decoy = nullptr;
static Geometry g_geo{};
static std::vector<Event> g_events;
static unsigned long long g_focusReacquire = 0;
static unsigned long long g_setCursorRoutes = 0;
static unsigned long long g_leaveRoutes = 0;

static bool is_client_pointer(UINT m) {
    switch (m) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        return true;
    default: return false;
    }
}

static bool is_wheel(UINT m) { return m == WM_MOUSEWHEEL || m == WM_MOUSEHWHEEL; }

static LRESULT CALLBACK GameProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (is_client_pointer(m)) {
        g_events.push_back(Event{m,w,GET_X_LPARAM(l),GET_Y_LPARAM(l)});
        return 0;
    }
    if (is_wheel(m)) {
        g_events.push_back(Event{m,w,GET_X_LPARAM(l),GET_Y_LPARAM(l)});
        return 0;
    }
    if (m == WM_MOUSELEAVE) {
        g_events.push_back(Event{m,w,0,0});
        return 0;
    }
    if (m == WM_SETCURSOR) {
        ++g_setCursorRoutes;
        return TRUE;
    }
    return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK PresenterProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEACTIVATE) {
        SetForegroundWindow(g_game);
        SetActiveWindow(g_game);
        SetFocus(g_game);
        ++g_focusReacquire;
        return MA_NOACTIVATE;
    }
    if (is_client_pointer(m)) {
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        ClientToScreen(h,&p);
        p = g_geo.physical_to_logical(p);
        SendMessageW(g_game,m,w,MAKELPARAM(p.x,p.y));
        return 0;
    }
    if (is_wheel(m)) {
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        POINT q = g_geo.physical_to_logical(p);
        q.x += g_geo.output.left;
        q.y += g_geo.output.top;
        SendMessageW(g_game,m,w,MAKELPARAM(q.x,q.y));
        return 0;
    }
    if (m == WM_MOUSELEAVE) {
        ++g_leaveRoutes;
        SendMessageW(g_game,m,w,l);
        return 0;
    }
    if (m == WM_SETCURSOR) {
        ++g_setCursorRoutes;
        return SendMessageW(g_game,WM_SETCURSOR,reinterpret_cast<WPARAM>(g_game),l);
    }
    return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK DecoyProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h,m,w,l); }

int main() {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW gc{}; gc.hInstance=hi; gc.lpfnWndProc=GameProc; gc.lpszClassName=L"PTAREventGame";
    WNDCLASSW pc{}; pc.hInstance=hi; pc.lpfnWndProc=PresenterProc; pc.lpszClassName=L"PTAREventPresenter"; pc.style=CS_DBLCLKS;
    WNDCLASSW dc{}; dc.hInstance=hi; dc.lpfnWndProc=DecoyProc; dc.lpszClassName=L"PTAREventDecoy";
    if ((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) ||
        (!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) ||
        (!RegisterClassW(&dc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)) return 10;

    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,0,0,1280,720,nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,0,0,1920,1080,g_game,nullptr,hi,nullptr);
    g_decoy=CreateWindowExW(0,dc.lpszClassName,L"decoy",WS_OVERLAPPEDWINDOW,50,50,400,300,nullptr,nullptr,hi,nullptr);
    if(!g_game||!g_presenter||!g_decoy) return 11;
    ShowWindow(g_game,SW_SHOW); ShowWindow(g_presenter,SW_SHOWNOACTIVATE); ShowWindow(g_decoy,SW_SHOWNA);

    const std::vector<UINT> clientMsgs={
        WM_MOUSEMOVE,
        WM_LBUTTONDOWN,WM_LBUTTONUP,WM_LBUTTONDBLCLK,
        WM_RBUTTONDOWN,WM_RBUTTONUP,WM_RBUTTONDBLCLK,
        WM_MBUTTONDOWN,WM_MBUTTONUP,WM_MBUTTONDBLCLK,
        WM_XBUTTONDOWN,WM_XBUTTONUP,WM_XBUTTONDBLCLK
    };
    const std::vector<POINT> renders={{320,180},{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800},{1600,900}};
    const std::vector<RECT> outputs={{0,0,1024,768},{0,0,1366,768},{0,0,1920,1080},{0,0,2560,1440},{-1920,0,0,1080},{1920,-200,4480,1240},{-1280,-1024,0,0}};
    std::mt19937 rng(0x45564E54u);
    unsigned failures=0;
    unsigned long long pointerCases=0,wheelCases=0,focusCases=0,setCursorCases=0,leaveCases=0;

    for(unsigned i=0;i<20000;++i) {
        POINT rs=renders[rng()%renders.size()]; RECT out=outputs[rng()%outputs.size()];
        g_geo=Geometry{out,UINT(rs.x),UINT(rs.y)};
        SetWindowPos(g_game,nullptr,out.left,out.top,rs.x,rs.y,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g_presenter,nullptr,out.left,out.top,out.right-out.left,out.bottom-out.top,SWP_NOZORDER|SWP_NOACTIVATE);

        UINT m=clientMsgs[rng()%clientMsgs.size()];
        POINT logical{LONG(rng()%g_geo.render_w),LONG(rng()%g_geo.render_h)};
        POINT physical=g_geo.logical_to_physical(logical);
        POINT presenterClient=physical; ScreenToClient(g_presenter,&presenterClient);
        WPARAM w=0;
        if(m==WM_XBUTTONDOWN||m==WM_XBUTTONUP||m==WM_XBUTTONDBLCLK) w=MAKEWPARAM(0,(rng()&1)?XBUTTON1:XBUTTON2);
        else w=WPARAM(rng()&0x001Fu);

        g_events.clear();
        SendMessageW(g_presenter,m,w,MAKELPARAM(presenterClient.x,presenterClient.y));
        if(g_events.size()!=1) {++failures;}
        else {
            const Event &got=g_events[0];
            if(got.msg!=m||got.w!=w||std::abs(got.x-logical.x)>1||std::abs(got.y-logical.y)>1) ++failures;
        }
        ++pointerCases;

        if((i%3)==0) {
            const UINT wm=(rng()&1)?WM_MOUSEWHEEL:WM_MOUSEHWHEEL;
            POINT lp{LONG(rng()%g_geo.render_w),LONG(rng()%g_geo.render_h)};
            POINT pp=g_geo.logical_to_physical(lp);
            WPARAM ww=MAKEWPARAM(0,SHORT((rng()&1)?WHEEL_DELTA:-WHEEL_DELTA));
            g_events.clear();
            SendMessageW(g_presenter,wm,ww,MAKELPARAM(pp.x,pp.y));
            if(g_events.size()!=1) ++failures;
            else {
                const Event &got=g_events[0];
                if(got.msg!=wm||got.w!=ww||std::abs(got.x-(out.left+lp.x))>1||std::abs(got.y-(out.top+lp.y))>1) ++failures;
            }
            ++wheelCases;
        }

        if((i%197)==0) {
            SetActiveWindow(g_decoy); SetFocus(g_decoy);
            LRESULT ma=SendMessageW(g_presenter,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(g_decoy),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
            if(ma!=MA_NOACTIVATE||GetActiveWindow()!=g_game||GetFocus()!=g_game) ++failures;
            ++focusCases;
        }
        if((i%131)==0) {
            const unsigned long long before=g_setCursorRoutes;
            LRESULT sr=SendMessageW(g_presenter,WM_SETCURSOR,reinterpret_cast<WPARAM>(g_presenter),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
            if(!sr||g_setCursorRoutes<before+2) ++failures;
            ++setCursorCases;
        }
        if((i%173)==0) {
            const size_t before=g_events.size();
            SendMessageW(g_presenter,WM_MOUSELEAVE,0,0);
            if(g_events.size()!=before+1||g_events.back().msg!=WM_MOUSELEAVE) ++failures;
            ++leaveCases;
        }
    }

    DWORD ex=DWORD(GetWindowLongPtrW(g_presenter,GWL_EXSTYLE));
    DWORD st=DWORD(GetWindowLongPtrW(g_presenter,GWL_STYLE));
    if(!(st&WS_POPUP)||(st&(WS_CAPTION|WS_THICKFRAME))||!(ex&WS_EX_NOACTIVATE)||!(ex&WS_EX_TOOLWINDOW)||(ex&WS_EX_TRANSPARENT)) ++failures;
    if(GetWindow(g_presenter,GW_OWNER)!=g_game) ++failures;

    if(failures) {
        std::printf("FAIL failures=%u pointer=%llu wheel=%llu focus=%llu setcursor=%llu leave=%llu\n",failures,pointerCases,wheelCases,focusCases,setCursorCases,leaveCases);
        return 20;
    }
    std::printf("PASS PTAR_BORDERLESS_EVENT_PARITY\n");
    std::printf("pointer_cases=%llu wheel_cases=%llu focus_reacquire_cases=%llu setcursor_cases=%llu leave_cases=%llu\n",pointerCases,wheelCases,focusCases,setCursorCases,leaveCases);
    std::printf("focus_reacquire_calls=%llu cursor_routes=%llu leave_routes=%llu\n",g_focusReacquire,g_setCursorRoutes,g_leaveRoutes);
    std::printf("contract=presenter_routes_absolute_pointer_to_logical_game; wheel_screen_coords_virtualized; click_reactivates_game_not_presenter; doubleclick_enabled\n");
    return 0;
}
