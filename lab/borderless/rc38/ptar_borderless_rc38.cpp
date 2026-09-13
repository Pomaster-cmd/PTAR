#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <wchar.h>
#include <stdint.h>
#include <string.h>

static HMODULE g_self=nullptr;
static HWND g_game=nullptr,g_presenter=nullptr;
static UINT g_renderW=0,g_renderH=0,g_outputW=0,g_outputH=0;
static RECT g_monitor{};
static WNDPROC g_initialGameProc=nullptr,g_gameNext=nullptr,g_presenterNext=nullptr;
static LONG_PTR g_presenterStyle0=0,g_presenterExStyle0=0;
static volatile LONG g_active=0,g_internal=0,g_logicalCapture=0;
static DWORD g_tls=TLS_OUT_OF_INDEXES;
static UINT g_iatHooks=0;

struct PTARBorderlessState {
    UINT size;
    UINT active;
    UINT renderW,renderH,outputW,outputH;
    LONG monitorLeft,monitorTop,monitorRight,monitorBottom;
    UINT gameClientW,gameClientH,presenterClientW,presenterClientH;
    ULONG_PTR gameStyle,gameExStyle,presenterStyle,presenterExStyle;
    UINT iatHooks;
    UINT physicalCaptureIsPresenter;
};

static void logline(const char* s){
    wchar_t path[MAX_PATH]{};
    if(!g_self || !GetModuleFileNameW(g_self,path,MAX_PATH)) return;
    wchar_t* slash=wcsrchr(path,L'\\'); if(!slash) return;
    wcscpy_s(slash+1, MAX_PATH-(slash+1-path), L"ptar_borderless_rc38.log");
    HANDLE h=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{}; GetLocalTime(&st);
    char line[1024]{};
    int n=wsprintfA(line,"[%02u:%02u:%02u.%03u] %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,s);
    DWORD wr=0; WriteFile(h,line,(DWORD)n,&wr,nullptr); CloseHandle(h);
}
static void logfmt(const char* tag,LONG_PTR a,LONG_PTR b=0,LONG_PTR c=0,LONG_PTR d=0){
    char x[512]{}; wsprintfA(x,"%s %lld %lld %lld %lld",tag,(long long)a,(long long)b,(long long)c,(long long)d); logline(x);
}

static bool client_size(HWND h,UINT& w,UINT& hh){RECT r{};if(!GetClientRect(h,&r))return false;w=(UINT)(r.right-r.left);hh=(UINT)(r.bottom-r.top);return true;}
static LONG clampL(LONG v,LONG lo,LONG hi){return v<lo?lo:(v>hi?hi:v);}
struct MapRect { LONG l,t,r,b; };
static MapRect image_rect(){
    const LONG mw=g_monitor.right-g_monitor.left,mh=g_monitor.bottom-g_monitor.top;
    int64_t w=mw,h=(int64_t)mw*g_renderH/g_renderW;
    if(h>mh){h=mh;w=(int64_t)mh*g_renderW/g_renderH;}
    LONG iw=(LONG)w,ih=(LONG)h;
    LONG l=g_monitor.left+(mw-iw)/2,t=g_monitor.top+(mh-ih)/2;
    return {l,t,l+iw,t+ih};
}
static POINT physical_to_logical_screen(POINT p){
    MapRect m=image_rect(); LONG iw=m.r-m.l,ih=m.b-m.t;
    LONG px=clampL(p.x,m.l,m.r-1),py=clampL(p.y,m.t,m.b-1);
    POINT q{};
    q.x=g_monitor.left+(LONG)((int64_t)(px-m.l)*g_renderW/(iw?iw:1));
    q.y=g_monitor.top +(LONG)((int64_t)(py-m.t)*g_renderH/(ih?ih:1));
    return q;
}
static POINT logical_to_physical_screen(POINT p){
    MapRect m=image_rect(); LONG iw=m.r-m.l,ih=m.b-m.t;
    LONG lx=clampL(p.x-g_monitor.left,0,(LONG)g_renderW-1),ly=clampL(p.y-g_monitor.top,0,(LONG)g_renderH-1);
    POINT q{};
    q.x=m.l+(LONG)((int64_t)lx*iw/g_renderW);
    q.y=m.t+(LONG)((int64_t)ly*ih/g_renderH);
    return q;
}
static LPARAM logical_client_lparam_from_presenter(HWND h,LPARAM lp){
    POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ClientToScreen(h,&p); POINT q=physical_to_logical_screen(p);
    return MAKELPARAM((SHORT)(q.x-g_monitor.left),(SHORT)(q.y-g_monitor.top));
}
static LPARAM logical_screen_lparam(LPARAM lp){
    POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; POINT q=physical_to_logical_screen(p);
    return MAKELPARAM((SHORT)q.x,(SHORT)q.y);
}
static void tls_set_pos(LPARAM lp){ if(g_tls!=TLS_OUT_OF_INDEXES) TlsSetValue(g_tls,(LPVOID)(ULONG_PTR)((DWORD)lp|1ull<<32)); }
static void tls_clear(){ if(g_tls!=TLS_OUT_OF_INDEXES) TlsSetValue(g_tls,nullptr); }
static bool tls_get_pos(DWORD& p){ if(g_tls==TLS_OUT_OF_INDEXES)return false; ULONG_PTR v=(ULONG_PTR)TlsGetValue(g_tls); if(!v)return false; p=(DWORD)v; return true; }

static LONG_PTR get_wndproc(HWND h){return IsWindowUnicode(h)?GetWindowLongPtrW(h,GWLP_WNDPROC):GetWindowLongPtrA(h,GWLP_WNDPROC);}
static LONG_PTR set_wndproc(HWND h,WNDPROC p){return IsWindowUnicode(h)?SetWindowLongPtrW(h,GWLP_WNDPROC,(LONG_PTR)p):SetWindowLongPtrA(h,GWLP_WNDPROC,(LONG_PTR)p);}
static LRESULT call_next(WNDPROC p,HWND h,UINT m,WPARAM w,LPARAM l){
    if(!p) return IsWindowUnicode(h)?DefWindowProcW(h,m,w,l):DefWindowProcA(h,m,w,l);
    return IsWindowUnicode(h)?CallWindowProcW(p,h,m,w,l):CallWindowProcA(p,h,m,w,l);
}
static void enforce_geometry(){
    if(!g_game||!g_presenter)return;
    InterlockedExchange(&g_internal,1);
    LONG_PTR s=GetWindowLongPtrW(g_game,GWL_STYLE);
    LONG_PTR desired=(s & (WS_VISIBLE|WS_DISABLED|WS_CLIPSIBLINGS|WS_CLIPCHILDREN)) | WS_POPUP;
    if(s!=desired) SetWindowLongPtrW(g_game,GWL_STYLE,desired);
    SetWindowPos(g_game,HWND_TOP,g_monitor.left,g_monitor.top,(int)g_renderW,(int)g_renderH,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    LONG_PTR ps=GetWindowLongPtrW(g_presenter,GWL_STYLE);
    LONG_PTR pes=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);
    LONG_PTR pds=(ps & (WS_VISIBLE|WS_DISABLED|WS_CLIPSIBLINGS|WS_CLIPCHILDREN)) | WS_POPUP;
    LONG_PTR pde=(pes | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~(LONG_PTR)(WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_APPWINDOW);
    if(ps!=pds) SetWindowLongPtrW(g_presenter,GWL_STYLE,pds);
    if(pes!=pde) SetWindowLongPtrW(g_presenter,GWL_EXSTYLE,pde);
    SetWindowPos(g_presenter,HWND_TOP,g_monitor.left,g_monitor.top,(int)g_outputW,(int)g_outputH,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    InterlockedExchange(&g_internal,0);
}

static LRESULT CALLBACK GameProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(InterlockedCompareExchange(&g_active,0,0) && !InterlockedCompareExchange(&g_internal,0,0)){
        if(m==WM_WINDOWPOSCHANGING && l){
            WINDOWPOS* p=(WINDOWPOS*)l;
            p->x=g_monitor.left;p->y=g_monitor.top;p->cx=(int)g_renderW;p->cy=(int)g_renderH;
            p->flags&=~(SWP_NOMOVE|SWP_NOSIZE);
        } else if(m==WM_STYLECHANGING && l && (w==GWL_STYLE)){
            STYLESTRUCT* ss=(STYLESTRUCT*)l;
            ss->styleNew=(ss->styleNew & (WS_VISIBLE|WS_DISABLED|WS_CLIPSIBLINGS|WS_CLIPCHILDREN))|WS_POPUP;
        } else if(m==WM_SIZE){
            if(w==SIZE_MINIMIZED) ShowWindow(g_presenter,SW_HIDE);
            else {ShowWindow(g_presenter,SW_SHOWNOACTIVATE);SetWindowPos(g_presenter,HWND_TOP,g_monitor.left,g_monitor.top,(int)g_outputW,(int)g_outputH,SWP_NOACTIVATE|SWP_SHOWWINDOW);}
        } else if((m==WM_MOUSEWHEEL||m==WM_MOUSEHWHEEL)){
            DWORD routed=0;
            if(!tls_get_pos(routed)) l=logical_screen_lparam(l);
        }
    }
    return call_next(g_gameNext,h,m,w,l);
}
static bool is_button_down(UINT m){return m==WM_LBUTTONDOWN||m==WM_RBUTTONDOWN||m==WM_MBUTTONDOWN||m==WM_XBUTTONDOWN;}
static bool is_button_up(UINT m){return m==WM_LBUTTONUP||m==WM_RBUTTONUP||m==WM_MBUTTONUP||m==WM_XBUTTONUP;}
static bool is_pointer_client(UINT m){
    switch(m){case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_LBUTTONDBLCLK:case WM_RBUTTONDOWN:case WM_RBUTTONUP:case WM_RBUTTONDBLCLK:case WM_MBUTTONDOWN:case WM_MBUTTONUP:case WM_MBUTTONDBLCLK:case WM_XBUTTONDOWN:case WM_XBUTTONUP:case WM_XBUTTONDBLCLK:return true;default:return false;}
}
static LRESULT CALLBACK PresenterProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(InterlockedCompareExchange(&g_active,0,0)){
        if(m==WM_MOUSEACTIVATE){SetForegroundWindow(g_game);SetFocus(g_game);return MA_NOACTIVATE;}
        if(m==WM_NCHITTEST) return HTCLIENT;
        if(is_pointer_client(m)){
            if(is_button_down(m)){SetForegroundWindow(g_game);SetFocus(g_game);if(GetCapture()!=h)SetCapture(h);}
            LPARAM q=logical_client_lparam_from_presenter(h,l); tls_set_pos(q); LRESULT r=SendMessage(g_game,m,w,q); tls_clear();
            if(is_button_up(m) && !(w&(MK_LBUTTON|MK_RBUTTON|MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2)) && !InterlockedCompareExchange(&g_logicalCapture,0,0)) ReleaseCapture();
            return r;
        }
        if(m==WM_MOUSEWHEEL||m==WM_MOUSEHWHEEL){LPARAM q=logical_screen_lparam(l);tls_set_pos(q);LRESULT r=SendMessage(g_game,m,w,q);tls_clear();return r;}
        if(m==WM_MOUSELEAVE){return SendMessage(g_game,m,w,l);}
        if(m==WM_SETCURSOR){LRESULT r=SendMessage(g_game,m,(WPARAM)g_game,MAKELPARAM(HTCLIENT,HIWORD(l)));if(r)return r;}
    }
    return call_next(g_presenterNext,h,m,w,l);
}

static BOOL WINAPI HGetCursorPos(LPPOINT p){ if(!GetCursorPos(p))return FALSE; if(g_active&&p)*p=physical_to_logical_screen(*p); return TRUE; }
static BOOL WINAPI HSetCursorPos(int x,int y){POINT p{x,y};if(g_active)p=logical_to_physical_screen(p);return SetCursorPos(p.x,p.y);}
static BOOL WINAPI HClipCursor(const RECT* r){if(!g_active||!r)return ClipCursor(r);POINT a{r->left,r->top},b{r->right-1,r->bottom-1};a=logical_to_physical_screen(a);b=logical_to_physical_screen(b);RECT q{a.x,a.y,b.x+1,b.y+1};return ClipCursor(&q);}
static BOOL WINAPI HGetClipCursor(LPRECT r){if(!GetClipCursor(r))return FALSE;if(g_active&&r){POINT a{r->left,r->top},b{r->right-1,r->bottom-1};a=physical_to_logical_screen(a);b=physical_to_logical_screen(b);r->left=a.x;r->top=a.y;r->right=b.x+1;r->bottom=b.y+1;}return TRUE;}
static HWND WINAPI HSetCapture(HWND h){if(g_active&&h==g_game){HWND prev=GetCapture();SetCapture(g_presenter);InterlockedExchange(&g_logicalCapture,1);return prev==g_presenter?g_game:prev;}return SetCapture(h);}
static HWND WINAPI HGetCapture(){HWND h=GetCapture();if(g_active&&h==g_presenter&&InterlockedCompareExchange(&g_logicalCapture,0,0))return g_game;return h;}
static BOOL WINAPI HReleaseCapture(){InterlockedExchange(&g_logicalCapture,0);return ReleaseCapture();}
static BOOL WINAPI HTrackMouseEvent(LPTRACKMOUSEEVENT t){if(g_active&&t&&t->hwndTrack==g_game){TRACKMOUSEEVENT q=*t;q.hwndTrack=g_presenter;return TrackMouseEvent(&q);}return TrackMouseEvent(t);}
static DWORD WINAPI HGetMessagePos(){DWORD v=0;if(g_active&&tls_get_pos(v))return v;DWORD p=GetMessagePos();if(!g_active)return p;POINT q{GET_X_LPARAM(p),GET_Y_LPARAM(p)};q=physical_to_logical_screen(q);return (DWORD)MAKELPARAM((SHORT)q.x,(SHORT)q.y);}

struct Hook {const char* n; void* p;};
static bool patch_iat(HMODULE mod){
    if(!mod)return false;BYTE* b=(BYTE*)mod;auto* dos=(IMAGE_DOS_HEADER*)b;if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;auto* nt=(IMAGE_NT_HEADERS64*)(b+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return false;
    Hook hs[]={{"GetCursorPos",(void*)HGetCursorPos},{"SetCursorPos",(void*)HSetCursorPos},{"ClipCursor",(void*)HClipCursor},{"GetClipCursor",(void*)HGetClipCursor},{"SetCapture",(void*)HSetCapture},{"GetCapture",(void*)HGetCapture},{"ReleaseCapture",(void*)HReleaseCapture},{"TrackMouseEvent",(void*)HTrackMouseEvent},{"GetMessagePos",(void*)HGetMessagePos}};
    UINT count=0;auto* d=(IMAGE_IMPORT_DESCRIPTOR*)(b+dir.VirtualAddress);
    for(;d->Name;++d){auto* ft=(IMAGE_THUNK_DATA64*)(b+d->FirstThunk);auto* ot=d->OriginalFirstThunk?(IMAGE_THUNK_DATA64*)(b+d->OriginalFirstThunk):ft;for(;ot->u1.AddressOfData;++ot,++ft){if(IMAGE_SNAP_BY_ORDINAL64(ot->u1.Ordinal))continue;auto* ibn=(IMAGE_IMPORT_BY_NAME*)(b+ot->u1.AddressOfData);for(auto& h:hs){if(strcmp((char*)ibn->Name,h.n)==0){DWORD old=0;if(VirtualProtect(&ft->u1.Function,sizeof(ULONGLONG),PAGE_READWRITE,&old)){ft->u1.Function=(ULONGLONG)h.p;DWORD x=0;VirtualProtect(&ft->u1.Function,sizeof(ULONGLONG),old,&x);FlushInstructionCache(GetCurrentProcess(),&ft->u1.Function,sizeof(ULONGLONG));++count;}break;}}}}
    g_iatHooks=count;logfmt("IAT_HOOKS",count);return count>0;
}
static void restore_presenter_partial(){
    if(g_presenterNext && get_wndproc(g_presenter)==(LONG_PTR)PresenterProc)set_wndproc(g_presenter,g_presenterNext);
    if(g_presenter){SetWindowLongPtrW(g_presenter,GWL_STYLE,g_presenterStyle0);SetWindowLongPtrW(g_presenter,GWL_EXSTYLE,g_presenterExStyle0);}
}
static DWORD WINAPI Worker(LPVOID){
    LONG_PTR cur=0;bool transitioned=false;
    for(unsigned i=0;i<200;++i){if(!IsWindow(g_game)||!IsWindow(g_presenter))return 10;cur=get_wndproc(g_game);if(cur && cur!=(LONG_PTR)g_initialGameProc){transitioned=true;break;}Sleep(25);}
    if(!transitioned){logline("FAIL runtime game-WndProc transition not observed; fail-open");restore_presenter_partial();return 11;}
    g_gameNext=(WNDPROC)cur;
    if(!set_wndproc(g_game,GameProc)){logline("FAIL game subclass install");return 12;}
    g_presenterNext=(WNDPROC)get_wndproc(g_presenter);
    if(!set_wndproc(g_presenter,PresenterProc)){set_wndproc(g_game,g_gameNext);logline("FAIL presenter subclass install");return 13;}
    patch_iat(GetModuleHandleW(nullptr));
    InterlockedExchange(&g_active,1);
    enforce_geometry();
    UINT gw=0,gh=0,pw=0,ph=0;client_size(g_game,gw,gh);client_size(g_presenter,pw,ph);
    logfmt("ACTIVE game/presenter",(LONG_PTR)((gw<<16)^gh),(LONG_PTR)((pw<<16)^ph),g_iatHooks);
    return 0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttach(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    if(InterlockedCompareExchange(&g_active,0,0))return 1;
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)||!renderW||!renderH||!outputW||!outputH||renderW>outputW||renderH>outputH){logline("FAIL invalid attach args");return -1;}
    HMONITOR hm=MonitorFromWindow(presenter,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);if(!hm||!GetMonitorInfoW(hm,&mi)){logline("FAIL monitor resolve");return -2;}
    UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);
    if(mw!=outputW||mh!=outputH){logfmt("FAIL output/monitor mismatch",mw,mh,outputW,outputH);return -3;}
    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=mi.rcMonitor;
    g_initialGameProc=(WNDPROC)get_wndproc(game);g_presenterStyle0=GetWindowLongPtrW(presenter,GWL_STYLE);g_presenterExStyle0=GetWindowLongPtrW(presenter,GWL_EXSTYLE);
    if(!g_initialGameProc){logline("FAIL initial game proc");return -4;}
    HANDLE th=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(!th){logline("FAIL worker create");return -5;}CloseHandle(th);
    logfmt("ATTACH_PENDING",renderW,renderH,outputW,outputH);return 0;
}
extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessQuery(PTARBorderlessState* s){
    if(!s||s->size<sizeof(PTARBorderlessState))return -1;ZeroMemory(((BYTE*)s)+sizeof(UINT),sizeof(PTARBorderlessState)-sizeof(UINT));
    s->active=(UINT)InterlockedCompareExchange(&g_active,0,0);s->renderW=g_renderW;s->renderH=g_renderH;s->outputW=g_outputW;s->outputH=g_outputH;s->monitorLeft=g_monitor.left;s->monitorTop=g_monitor.top;s->monitorRight=g_monitor.right;s->monitorBottom=g_monitor.bottom;s->gameStyle=(ULONG_PTR)GetWindowLongPtrW(g_game,GWL_STYLE);s->gameExStyle=(ULONG_PTR)GetWindowLongPtrW(g_game,GWL_EXSTYLE);s->presenterStyle=(ULONG_PTR)GetWindowLongPtrW(g_presenter,GWL_STYLE);s->presenterExStyle=(ULONG_PTR)GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);client_size(g_game,s->gameClientW,s->gameClientH);client_size(g_presenter,s->presenterClientW,s->presenterClientH);s->iatHooks=g_iatHooks;s->physicalCaptureIsPresenter=(GetCapture()==g_presenter);return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){g_self=h;DisableThreadLibraryCalls(h);g_tls=TlsAlloc();}else if(reason==DLL_PROCESS_DETACH){if(g_tls!=TLS_OUT_OF_INDEXES)TlsFree(g_tls);}return TRUE;}
