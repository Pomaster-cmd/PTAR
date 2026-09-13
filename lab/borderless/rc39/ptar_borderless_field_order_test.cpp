#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>

struct State {UINT size,active,renderW,renderH,outputW,outputH;LONG ml,mt,mr,mb;UINT gw,gh,pw,ph;ULONG_PTR gs,gex,ps,pex;UINT hooks,physicalCaptureIsPresenter;};
typedef int (WINAPI* AttachFn)(HWND,HWND,UINT,UINT,UINT,UINT);
typedef int (WINAPI* QueryFn)(State*);

static WNDPROC g_baseProc=nullptr;
static volatile LONG g_runtimeHits=0;
static volatile LONG g_mouseX=-999,g_mouseY=-999;

static LRESULT CALLBACK GameBase(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEMOVE){g_mouseX=GET_X_LPARAM(l);g_mouseY=GET_Y_LPARAM(l);return 0x1234;}
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK RuntimeLateProc(HWND h,UINT m,WPARAM w,LPARAM l){
    InterlockedIncrement(&g_runtimeHits);
    return CallWindowProcW(g_baseProc,h,m,w,l);
}
static LRESULT CALLBACK PresenterBase(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void pump(DWORD ms){DWORD end=GetTickCount()+ms;MSG m;while((LONG)(end-GetTickCount())>0){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}}
static int fail(const char* s,int c){printf("FAIL %d %s\n",c,s);return c;}
static bool query(QueryFn q,State& s){s={};s.size=sizeof(s);return q(&s)==0;}

int main(){
    HINSTANCE hi=GetModuleHandleW(nullptr);
    WNDCLASSW gc{};gc.hInstance=hi;gc.lpfnWndProc=GameBase;gc.lpszClassName=L"RC39FieldOrderGame";
    WNDCLASSW pc{};pc.hInstance=hi;pc.lpfnWndProc=PresenterBase;pc.lpszClassName=L"RC39FieldOrderPresenter";
    if((!RegisterClassW(&gc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)||(!RegisterClassW(&pc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS))return fail("register",1);

    POINT z{0,0};HMONITOR hm=MonitorFromPoint(z,MONITOR_DEFAULTTOPRIMARY);MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi))return fail("monitor",2);
    const UINT ow=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),oh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);
    if(ow<640||oh<480)return fail("monitor small",3);
    const UINT rw=(ow*2)/3,rh=(oh*2)/3;

    HWND game=CreateWindowExW(0,gc.lpszClassName,L"game",WS_POPUP,mi.rcMonitor.left,mi.rcMonitor.top,rw,rh,nullptr,nullptr,hi,nullptr);
    HWND pres=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,pc.lpszClassName,L"presenter",WS_POPUP,mi.rcMonitor.left,mi.rcMonitor.top,ow,oh,game,nullptr,hi,nullptr);
    if(!game||!pres)return fail("windows",4);
    ShowWindow(game,SW_SHOW);ShowWindow(pres,SW_SHOWNOACTIVATE);pump(50);

    HMODULE d=LoadLibraryW(L"ptar_borderless.dll");if(!d)return fail("load sidecar",5);
    auto attach=(AttachFn)GetProcAddress(d,"PTAR_BorderlessAttach");
    auto q=(QueryFn)GetProcAddress(d,"PTAR_BorderlessQuery");
    if(!attach||!q)return fail("exports",6);

    // Real field order: AutoStart/Attach happens first, while PTAR still owns its
    // pre-transition WndProc. RC39 must remain pending here.
    if(attach(game,pres,rw,rh,ow,oh)!=0)return fail("deferred attach",7);
    pump(125);
    State s{};if(!query(q,s))return fail("query pending",8);
    if(s.active)return fail("activated before late PTAR WndProc transition",9);
    if(s.gw!=0||s.gh!=0){/* pending state may still expose configured dims only through render fields */}

    // Simulate P1U46 installing its final WndProc after AutoStart returned.
    g_baseProc=(WNDPROC)GetWindowLongPtrW(game,GWLP_WNDPROC);if(!g_baseProc)return fail("base proc",10);
    if(!SetWindowLongPtrW(game,GWLP_WNDPROC,(LONG_PTR)RuntimeLateProc))return fail("late runtime proc",11);
    pump(650);

    if(!query(q,s)||!s.active)return fail("did not activate after late transition",12);
    if(s.gw!=rw||s.gh!=rh||s.pw!=ow||s.ph!=oh)return fail("authority geometry",13);
    if(!(s.ps&WS_POPUP)||(s.pex&(WS_EX_TRANSPARENT|WS_EX_TOPMOST))||!(s.pex&WS_EX_NOACTIVATE)||!(s.pex&WS_EX_TOOLWINDOW))return fail("presenter policy",14);

    // Simulate the later P1U46 silent native-input restore observed on hardware.
    SetWindowPos(game,nullptr,mi.rcMonitor.left+17,mi.rcMonitor.top+23,(int)ow,(int)oh,SWP_NOZORDER|SWP_NOACTIVATE);
    pump(50);
    if(!query(q,s)||s.gw!=rw||s.gh!=rh)return fail("late native geometry escaped clamp",15);

    // Ensure RC39 chains through the late PTAR WndProc rather than bypassing it.
    g_mouseX=g_mouseY=-999;SendMessageW(pres,WM_MOUSEMOVE,0,MAKELPARAM((SHORT)(ow/2),(SHORT)(oh/2)));
    if(g_runtimeHits<=0)return fail("late runtime WndProc not chained",16);
    if(abs((int)g_mouseX-(int)rw/2)>1||abs((int)g_mouseY-(int)rh/2)>1)return fail("pointer map",17);

    printf("PASS RC39_FIELD_ORDER pending_before_transition=YES active_after_transition=YES render=%ux%u presenter=%ux%u runtimeHits=%ld mouse=%ld,%ld\n",s.gw,s.gh,s.pw,s.ph,g_runtimeHits,g_mouseX,g_mouseY);
    return 0;
}
