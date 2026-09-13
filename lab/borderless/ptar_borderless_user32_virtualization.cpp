#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include "include/ptar_borderless_input_virtualizer.h"

using ptar_lab::Geometry;
using ptar_lab::InputVirtualizer;

static HWND g_game=nullptr,g_presenter=nullptr;
static InputVirtualizer g_input;
static LONG g_forwarded=0;
static LONG g_lastClientX=-1,g_lastClientY=-1;
static LONG g_lastMsgPosX=-1,g_lastMsgPosY=-1;
static LONG g_leave=0;

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEMOVE||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP){
        ++g_forwarded;
        g_lastClientX=GET_X_LPARAM(l);g_lastClientY=GET_Y_LPARAM(l);
        POINT p=InputVirtualizer::unpack_message_pos(InputVirtualizer::get_message_pos());
        g_lastMsgPosX=p.x;g_lastMsgPosY=p.y;
        return 0;
    }
    if(m==WM_MOUSELEAVE){++g_leave;return 0;}
    return DefWindowProcW(h,m,w,l);
}

static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_MOUSEMOVE||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP){
        POINT physical{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        ClientToScreen(h,&physical);
        POINT logicalClient=g_input.physical_screen_to_logical_client(physical);
        POINT logicalScreen{logicalClient.x+g_input.geometry().output.left,logicalClient.y+g_input.geometry().output.top};
        InputVirtualizer::RoutedMessageScope scope(logicalScreen);
        SendMessageW(g_game,m,w,MAKELPARAM(logicalClient.x,logicalClient.y));
        return 0;
    }
    if(m==WM_MOUSELEAVE){SendMessageW(g_game,WM_MOUSELEAVE,w,l);return 0;}
    return DefWindowProcW(h,m,w,l);
}

static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static unsigned diff(LONG a,LONG b){return unsigned(a>b?a-b:b-a);}

int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"PTARUser32Game";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"PTARUser32Presenter";pc.style=CS_DBLCLKS;
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;

    POINT origin{0,0};
    HMONITOR monitor=MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!monitor||!GetMonitorInfoW(monitor,&mi))return 11;
    RECT output=mi.rcMonitor;
    const LONG outW=output.right-output.left;
    const LONG outH=output.bottom-output.top;
    if(outW<320||outH<180)return 12;

    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,output.left,output.top,640,360,nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,output.left,output.top,outW,outH,g_game,nullptr,hi,nullptr);
    if(!g_game||!g_presenter)return 13;
    ShowWindow(g_game,SW_SHOW);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump();

    std::vector<POINT> renders;
    auto add_render=[&](LONG w,LONG h){
        w=(std::max)(LONG(1),(std::min)(w,outW));
        h=(std::max)(LONG(1),(std::min)(h,outH));
        for(const POINT&p:renders)if(p.x==w&&p.y==h)return;
        renders.push_back(POINT{w,h});
    };
    add_render(320,180);add_render(640,360);add_render(800,450);add_render(960,540);
    add_render(1024,576);add_render(1280,720);add_render(outW,outH);
    add_render(outW/2,outH/2);add_render((outW*3)/4,(outH*3)/4);

    // Preserve broad synthetic geometry coverage without asking USER32 to place
    // windows on monitors that do not exist on the CI machine. The real USER32
    // phase below is restricted to the actual primary monitor returned by the OS.
    const std::vector<RECT> syntheticOutputs={{0,0,1024,768},{0,0,1366,768},{0,0,1920,1080},{0,0,2560,1440},{-1920,0,0,1080},{1920,-200,4480,1240},{-1280,-1024,0,0}};
    std::mt19937 rng(0x55333256u);
    unsigned failures=0,maxClientErr=0,maxMsgPosErr=0,maxSyntheticErr=0;
    unsigned long long routedCases=0,captureCases=0,trackCases=0,queryCases=0,nestedScopes=0,syntheticMathCases=0;

    for(unsigned i=0;i<50000;++i){
        RECT so=syntheticOutputs[rng()%syntheticOutputs.size()];
        POINT rs{LONG(320+rng()%1281),LONG(180+rng()%721)};
        Geometry sg{so,UINT(rs.x),UINT(rs.y)};
        POINT logical{LONG(rng()%sg.render_w),LONG(rng()%sg.render_h)};
        POINT physical=sg.logical_to_physical(logical);
        POINT back=sg.physical_to_logical(physical);
        unsigned e=(std::max)(diff(back.x,logical.x),diff(back.y,logical.y));
        maxSyntheticErr=(std::max)(maxSyntheticErr,e);
        if(e>1)++failures;
        ++syntheticMathCases;
    }

    for(unsigned i=0;i<12000;++i){
        POINT rs=renders[rng()%renders.size()];
        Geometry geo{output,UINT(rs.x),UINT(rs.y)};g_input.configure(g_game,g_presenter,geo);
        SetWindowPos(g_game,nullptr,output.left,output.top,rs.x,rs.y,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g_presenter,nullptr,output.left,output.top,outW,outH,SWP_NOZORDER|SWP_NOACTIVATE);
        pump();

        RECT actualPresenter{};
        if(!GetWindowRect(g_presenter,&actualPresenter)||actualPresenter.left!=output.left||actualPresenter.top!=output.top||actualPresenter.right!=output.right||actualPresenter.bottom!=output.bottom){++failures;continue;}
        RECT actualGameClient{};
        if(!GetClientRect(g_game,&actualGameClient)||actualGameClient.right!=rs.x||actualGameClient.bottom!=rs.y){++failures;continue;}

        POINT logical{LONG(rng()%geo.render_w),LONG(rng()%geo.render_h)};
        POINT physical=geo.logical_to_physical(logical);
        POINT pcpt=physical;ScreenToClient(g_presenter,&pcpt);
        g_lastClientX=g_lastClientY=g_lastMsgPosX=g_lastMsgPosY=-1;
        LONG before=g_forwarded;
        PostMessageW(g_presenter,WM_MOUSEMOVE,0,MAKELPARAM(pcpt.x,pcpt.y));
        pump();
        if(g_forwarded!=before+1)++failures;
        unsigned ce=(std::max)(diff(g_lastClientX,logical.x),diff(g_lastClientY,logical.y));
        maxClientErr=(std::max)(maxClientErr,ce);if(ce>1)++failures;
        LONG ex=output.left+logical.x,ey=output.top+logical.y;
        unsigned me=(std::max)(diff(g_lastMsgPosX,ex),diff(g_lastMsgPosY,ey));
        maxMsgPosErr=(std::max)(maxMsgPosErr,me);if(me>1)++failures;
        ++routedCases;

        if((i%2)==0){
            SetActiveWindow(g_game);SetFocus(g_game);
            HWND old=g_input.set_capture(g_game);(void)old;
            if(::GetCapture()!=g_presenter||g_input.get_capture()!=g_game||!g_input.logical_capture_active())++failures;
            if(!g_input.release_capture()||::GetCapture()!=nullptr||g_input.get_capture()!=nullptr||g_input.logical_capture_active())++failures;
            ++captureCases;
        }

        if((i%3)==0){
            TRACKMOUSEEVENT logicalTrack{};logicalTrack.cbSize=sizeof(logicalTrack);logicalTrack.dwFlags=TME_LEAVE|TME_HOVER;logicalTrack.hwndTrack=g_game;logicalTrack.dwHoverTime=DWORD(1+rng()%1000);
            TRACKMOUSEEVENT physicalTrack{};
            if(!g_input.rewrite_track_request(logicalTrack,physicalTrack))++failures;
            if(physicalTrack.hwndTrack!=g_presenter||physicalTrack.dwFlags!=logicalTrack.dwFlags||physicalTrack.dwHoverTime!=logicalTrack.dwHoverTime)++failures;
            if(g_input.tracked_flags()!=logicalTrack.dwFlags||g_input.hover_time()!=logicalTrack.dwHoverTime)++failures;
            ++trackCases;

            TRACKMOUSEEVENT queried=physicalTrack;queried.dwFlags=TME_QUERY;queried.hwndTrack=g_presenter;
            g_input.logicalize_track_result(queried);
            if(queried.hwndTrack!=g_game)++failures;
            ++queryCases;
        }

        if((i%101)==0){
            POINT outer{output.left+10,output.top+20};
            POINT inner{output.left+30,output.top+40};
            {
                InputVirtualizer::RoutedMessageScope a(outer);
                POINT pa=InputVirtualizer::unpack_message_pos(InputVirtualizer::get_message_pos());
                if(pa.x!=outer.x||pa.y!=outer.y)++failures;
                {
                    InputVirtualizer::RoutedMessageScope b(inner);
                    POINT pb=InputVirtualizer::unpack_message_pos(InputVirtualizer::get_message_pos());
                    if(pb.x!=inner.x||pb.y!=inner.y)++failures;
                }
                POINT pa2=InputVirtualizer::unpack_message_pos(InputVirtualizer::get_message_pos());
                if(pa2.x!=outer.x||pa2.y!=outer.y)++failures;
            }
            ++nestedScopes;
        }
    }

    LONG leaveBefore=g_leave;SendMessageW(g_presenter,WM_MOUSELEAVE,0,0);if(g_leave!=leaveBefore+1)++failures;
    if(failures){std::printf("FAIL failures=%u routed=%llu capture=%llu track=%llu query=%llu synthetic=%llu max_client_err=%u max_msgpos_err=%u max_synthetic_err=%u output=%ldx%ld\n",failures,routedCases,captureCases,trackCases,queryCases,syntheticMathCases,maxClientErr,maxMsgPosErr,maxSyntheticErr,outW,outH);return 20;}
    std::printf("PASS PTAR_BORDERLESS_USER32_VIRTUALIZATION\n");
    std::printf("routed_message_cases=%llu capture_cases=%llu track_cases=%llu query_cases=%llu nested_messagepos_scopes=%llu synthetic_math_cases=%llu\n",routedCases,captureCases,trackCases,queryCases,nestedScopes,syntheticMathCases);
    std::printf("max_client_error=%u max_GetMessagePos_error=%u max_synthetic_error=%u mouseleave_forward=PASS actual_output=%ldx%ld\n",maxClientErr,maxMsgPosErr,maxSyntheticErr,outW,outH);
    std::printf("contract=real_USER32_only_on_real_monitor; synthetic_multimonitor_math_separate; TrackMouseEvent_game_to_presenter; SetCapture_logical_game_to_physical_presenter; GetCapture_hidden; scoped_GetMessagePos_logical; WM_INPUT_untouched\n");
    return 0;
}
