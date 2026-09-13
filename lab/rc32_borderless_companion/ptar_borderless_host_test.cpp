#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <random>
#include <vector>
#include <stdint.h>
#include "ptar_borderless_api.h"

static HWND g_game=nullptr,g_presenter=nullptr;
static LONG g_lastX=-1,g_lastY=-1;
static LONG g_mouse=0,g_wheel=0;
static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEMOVE||m==WM_LBUTTONDOWN||m==WM_LBUTTONUP){++g_mouse;g_lastX=GET_X_LPARAM(l);g_lastY=GET_Y_LPARAM(l);return 0;}
    if(m==WM_MOUSEWHEEL||m==WM_MOUSEHWHEEL){++g_wheel;g_lastX=GET_X_LPARAM(l);g_lastY=GET_Y_LPARAM(l);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_NCHITTEST)return HTTRANSPARENT; // deliberately emulate old RC32 presenter
    return DefWindowProcW(h,m,w,l);
}
static void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static bool game_client(UINT w,UINT h){RECT r{};return GetClientRect(g_game,&r)&&UINT(r.right-r.left)==w&&UINT(r.bottom-r.top)==h;}
int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameProc;gc.lpszClassName=L"RC32CompanionGame";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterProc;pc.lpszClassName=L"RC32CompanionPresenter";pc.style=CS_DBLCLKS;
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return 10;
    g_game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,hi,nullptr);
    g_presenter=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,pc.lpszClassName,L"presenter",WS_POPUP|WS_VISIBLE,0,0,1920,1080,g_game,nullptr,hi,nullptr);
    if(!g_game||!g_presenter)return 11;ShowWindow(g_game,SW_SHOW);ShowWindow(g_presenter,SW_SHOWNOACTIVATE);pump();
    int rc=PTAR_BorderlessAttach(g_game,g_presenter,1280,720,1920,1080);if(rc){std::printf("ATTACH_FAIL %d status=0x%08x\n",rc,PTAR_BorderlessStatus());return 20;}
    uint32_t st=PTAR_BorderlessStatus();uint32_t want=PTAR_BCTL_ACTIVE|PTAR_BCTL_GAME_SUBCLASSED|PTAR_BCTL_PRESENTER_SUBCLASSED|PTAR_BCTL_GEOMETRY_LOCKED|PTAR_BCTL_INPUT_ROUTING|PTAR_BCTL_DPI_SAFE;if((st&want)!=want)return 21;
    LONG_PTR ex=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE),ws=GetWindowLongPtrW(g_presenter,GWL_STYLE);if((ex&(WS_EX_TRANSPARENT|WS_EX_TOPMOST))||!(ex&WS_EX_NOACTIVATE)||!(ws&WS_POPUP))return 22;
    if(SendMessageW(g_presenter,WM_NCHITTEST,0,MAKELPARAM(100,100))!=HTCLIENT)return 23;
    if(!game_client(1280,720))return 24;
    std::mt19937 rng(0x52433332u);unsigned failures=0;unsigned long long pointer=0,hostile=0,wheels=0,minrest=0;
    for(unsigned i=0;i<20000;++i){
        LONG px=LONG(rng()%1920),py=LONG(rng()%1080);g_lastX=g_lastY=-1;LONG before=g_mouse;SendMessageW(g_presenter,WM_MOUSEMOVE,0,MAKELPARAM(px,py));if(g_mouse!=before+1)++failures;LONG exx=LONG((int64_t(2*px+1)*1280)/(2*1920)),eyy=LONG((int64_t(2*py+1)*720)/(2*1080));if(abs(g_lastX-exx)>1||abs(g_lastY-eyy)>1)++failures;++pointer;
        LONG hx=LONG(rng()%2500)-300,hy=LONG(rng()%1600)-200,hw=LONG(300+rng()%2200),hh=LONG(200+rng()%1400);SetWindowPos(g_game,nullptr,hx,hy,hw,hh,SWP_NOZORDER|SWP_NOACTIVATE);pump();if(!game_client(1280,720))++failures;++hostile;
        if((i%7)==0){LONG sx=LONG(rng()%1920),sy=LONG(rng()%1080);g_lastX=g_lastY=-1;LONG wb=g_wheel;SendMessageW(g_game,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(sx,sy));if(g_wheel!=wb+1)++failures;LONG lx=LONG((int64_t(2*sx+1)*1280)/(2*1920)),ly=LONG((int64_t(2*sy+1)*720)/(2*1080));if(abs(g_lastX-lx)>1||abs(g_lastY-ly)>1)++failures;++wheels;}
        if((i%997)==0){ShowWindow(g_game,SW_MINIMIZE);pump();if(IsWindowVisible(g_presenter))++failures;ShowWindow(g_game,SW_RESTORE);pump();if(!IsWindowVisible(g_presenter)||!game_client(1280,720))++failures;++minrest;}
    }
    PTAR_BorderlessDetach();
    if(failures){std::printf("FAIL failures=%u pointer=%llu hostile=%llu wheel=%llu minrest=%llu\n",failures,pointer,hostile,wheels,minrest);return 30;}
    std::printf("PASS PTAR_RC32_BORDERLESS_COMPANION_HOST\nstatus_before_detach=0x%08x pointer=%llu hostile_geometry=%llu wheel=%llu minimize_restore=%llu\n",st,pointer,hostile,wheels,minrest);
    return 0;
}
