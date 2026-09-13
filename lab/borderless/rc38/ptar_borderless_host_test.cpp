#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>

struct State {UINT size,active,renderW,renderH,outputW,outputH;LONG ml,mt,mr,mb;UINT gw,gh,pw,ph;ULONG_PTR gs,gex,ps,pex;UINT hooks,physicalCaptureIsPresenter;};
typedef int (WINAPI* AttachFn)(HWND,HWND,UINT,UINT,UINT,UINT);
typedef int (WINAPI* QueryFn)(State*);
static volatile LONG gx=-999,gy=-999,wx=-999,wy=-999;static WNDPROC g_prev=nullptr;
static LRESULT CALLBACK Game0(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEMOVE){gx=GET_X_LPARAM(l);gy=GET_Y_LPARAM(l);return 123;}if(m==WM_MOUSEWHEEL){wx=GET_X_LPARAM(l);wy=GET_Y_LPARAM(l);return 124;}return DefWindowProcW(h,m,w,l);}
static LRESULT CALLBACK RuntimeProc(HWND h,UINT m,WPARAM w,LPARAM l){return CallWindowProcW(g_prev,h,m,w,l);}
static LRESULT CALLBACK Presenter0(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void pump(unsigned ms){DWORD end=GetTickCount()+ms;MSG m;while((LONG)(end-GetTickCount())>0){while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}}
static int fail(const char*s,int c){printf("FAIL %d %s\n",c,s);return c;}
int main(){
 HINSTANCE hi=GetModuleHandleW(nullptr);WNDCLASSW a{};a.hInstance=hi;a.lpfnWndProc=Game0;a.lpszClassName=L"RC38Game";WNDCLASSW b{};b.hInstance=hi;b.lpfnWndProc=Presenter0;b.lpszClassName=L"RC38Presenter";RegisterClassW(&a);RegisterClassW(&b);
 POINT z{0,0};HMONITOR hm=MonitorFromPoint(z,MONITOR_DEFAULTTOPRIMARY);MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!hm||!GetMonitorInfoW(hm,&mi))return fail("monitor",1);UINT ow=mi.rcMonitor.right-mi.rcMonitor.left,oh=mi.rcMonitor.bottom-mi.rcMonitor.top;if(ow<640||oh<480)return fail("monitor small",2);UINT rw=(ow*2)/3,rh=(oh*2)/3;
 HWND game=CreateWindowExW(0,a.lpszClassName,L"g",WS_OVERLAPPEDWINDOW,mi.rcMonitor.left,mi.rcMonitor.top,ow,oh,nullptr,nullptr,hi,nullptr);HWND pres=CreateWindowExW(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,b.lpszClassName,L"p",WS_POPUP,mi.rcMonitor.left,mi.rcMonitor.top,ow,oh,game,nullptr,hi,nullptr);if(!game||!pres)return fail("windows",3);ShowWindow(game,SW_SHOW);ShowWindow(pres,SW_SHOWNOACTIVATE);pump(50);
 HMODULE d=LoadLibraryW(L"ptar_borderless.dll");if(!d)return fail("load dll",4);auto attach=(AttachFn)GetProcAddress(d,"PTAR_BorderlessAttach");auto query=(QueryFn)GetProcAddress(d,"PTAR_BorderlessQuery");if(!attach||!query)return fail("exports",5);if(attach(game,pres,rw,rh,ow,oh)!=0)return fail("attach",6);
 Sleep(100);g_prev=(WNDPROC)GetWindowLongPtrW(game,GWLP_WNDPROC);SetWindowLongPtrW(game,GWLP_WNDPROC,(LONG_PTR)RuntimeProc);pump(600);
 State s{};s.size=sizeof(s);if(query(&s)||!s.active)return fail("not active",7);if(s.gw!=rw||s.gh!=rh||s.pw!=ow||s.ph!=oh)return fail("geometry initial",8);if(!(s.ps&WS_POPUP)||(s.ps&(WS_CAPTION|WS_THICKFRAME)))return fail("presenter style",9);if(!(s.pex&WS_EX_NOACTIVATE)||!(s.pex&WS_EX_TOOLWINDOW)||(s.pex&(WS_EX_TRANSPARENT|WS_EX_TOPMOST)))return fail("presenter exstyle",10);
 SetWindowPos(game,nullptr,mi.rcMonitor.left+77,mi.rcMonitor.top+55,ow,oh,SWP_NOZORDER|SWP_NOACTIVATE);pump(50);s={};s.size=sizeof(s);query(&s);if(s.gw!=rw||s.gh!=rh)return fail("hostile resize escaped",11);
 gx=gy=-999;SendMessageW(pres,WM_MOUSEMOVE,0,MAKELPARAM((SHORT)(ow/2),(SHORT)(oh/2)));if(abs((int)gx-(int)rw/2)>1||abs((int)gy-(int)rh/2)>1)return fail("mouse map",12);
 wx=wy=-999;SendMessageW(pres,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM((SHORT)(mi.rcMonitor.left+ow/2),(SHORT)(mi.rcMonitor.top+oh/2)));if(abs((int)wx-(mi.rcMonitor.left+(int)rw/2))>1||abs((int)wy-(mi.rcMonitor.top+(int)rh/2))>1)return fail("wheel map",13);
 if(s.hooks>0){HWND old=SetCapture(game);(void)old;if(GetCapture()!=game)return fail("logical capture",14);State q{};q.size=sizeof(q);query(&q);if(!q.physicalCaptureIsPresenter)return fail("physical capture",15);ReleaseCapture();}
 ShowWindow(game,SW_MINIMIZE);pump(80);if(IsWindowVisible(pres))return fail("presenter minimize",16);ShowWindow(game,SW_RESTORE);pump(80);if(!IsWindowVisible(pres))return fail("presenter restore",17);
 s={};s.size=sizeof(s);query(&s);printf("PASS active=%u render=%ux%u presenter=%ux%u hooks=%u mouse=%ld,%ld wheel=%ld,%ld\n",s.active,s.gw,s.gh,s.pw,s.ph,s.hooks,gx,gy,wx,wy);return 0;
}
