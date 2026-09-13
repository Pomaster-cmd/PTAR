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
    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,0,0,1280,720,nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,0,0,1920,1080,g_game,nullptr,hi,nullptr);
    if(!g_game||!g_presenter)return 11;
    ShowWindow(g_game,SW_SHOW);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump();

    const std::vector<POINT> renders={{320,180},{640,360},{800,450},{960,540},{1024,576},{1280,720},{1024,768},{1280,800},{1600,900}};
    const std::vector<RECT> outputs={{0,0,1024,768},{0,0,1366,768},{0,0,1920,1080},{0,0,2560,1440},{-1920,0,0,1080},{1920,-200,4480,1240},{-1280,-1024,0,0}};
    std::mt19937 rng(0x55333256u);
    unsigned failures=0,maxClientErr=0,maxMsgPosErr=0;
    unsigned long long routedCases=0,captureCases=0,trackCases=0,queryCases=0,nestedScopes=0;

    for(unsigned i=0;i<12000;++i){
        POINT rs=renders[rng()%renders.size()];RECT out=outputs[rng()%outputs.size()];
        Geometry geo{out,UINT(rs.x),UINT(rs.y)};g_input.configure(g_game,g_presenter,geo);
        SetWindowPos(g_game,nullptr,out.left,out.top,rs.x,rs.y,SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(g_presenter,nullptr,out.left,out.top,out.right-out.left,out.bottom-out.top,SWP_NOZORDER|SWP_NOACTIVATE);
        pump();

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
        LONG ex=out.left+logical.x,ey=out.top+logical.y;
        unsigned me=(std::max)(diff(g_lastMsgPosX,ex),diff(g_lastMsgPosY,ey));
        maxMsgPosErr=(std::max)(maxMsgPosErr,me);if(me>1)++failures;
        ++routedCases;

        if((i%2)==0){
            HWND old=g_input.set_capture(g_game);
            (void)old;
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
            POINT outer{out.left+10,out.top+20};
            POINT inner{out.left+30,out.top+40};
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
    if(failures){std::printf("FAIL failures=%u routed=%llu capture=%llu track=%llu query=%llu max_client_err=%u max_msgpos_err=%u\n",failures,routedCases,captureCases,trackCases,queryCases,maxClientErr,maxMsgPosErr);return 20;}
    std::printf("PASS PTAR_BORDERLESS_USER32_VIRTUALIZATION\n");
    std::printf("routed_message_cases=%llu capture_cases=%llu track_cases=%llu query_cases=%llu nested_messagepos_scopes=%llu\n",routedCases,captureCases,trackCases,queryCases,nestedScopes);
    std::printf("max_client_error=%u max_GetMessagePos_error=%u mouseleave_forward=PASS\n",maxClientErr,maxMsgPosErr);
    std::printf("contract=TrackMouseEvent_game_to_presenter; SetCapture_logical_game_to_physical_presenter; GetCapture_hidden; scoped_GetMessagePos_logical; WM_INPUT_untouched\n");
    return 0;
}
