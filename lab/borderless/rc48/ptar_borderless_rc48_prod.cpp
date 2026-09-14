#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// RC48 deliberately stops stacking behavioural wrappers on top of RC43-RC47.
// It reuses the validated RC46 dual-mode controller directly, but virtualizes
// only the presenter Win32 mutations made by that controller. In Windowed mode
// the native PTAR presenter becomes a real WS_CHILD of the game HWND. This makes
// the reconstructed surface part of the game client hierarchy instead of a
// separate popup that can remain black/occluded under the P1U46 window policy.
// In Borderless mode every intercepted operation is passed through as the
// validated RC46/RC42 top-level presenter path.

static LONG_PTR WINAPI RC48_SetWindowLongPtrW(HWND,int,LONG_PTR);
static BOOL WINAPI RC48_SetWindowPos(HWND,HWND,int,int,int,int,UINT);
static BOOL WINAPI RC48_ShowWindow(HWND,int);

#define DllMain DllMain_RC46_INTERNAL
#define SetWindowLongPtrW RC48_SetWindowLongPtrW
#define SetWindowPos RC48_SetWindowPos
#define ShowWindow RC48_ShowWindow
#include "../rc46/ptar_borderless_rc46_prod.cpp"
#undef ShowWindow
#undef SetWindowPos
#undef SetWindowLongPtrW
#undef DllMain

static volatile LONG g_rc48Stop=0;
static volatile LONG g_rc48HaveOriginal=0;
static volatile LONG g_rc48IsChild=0;
static HWND g_rc48OriginalOwner=nullptr;
static LONG_PTR g_rc48OriginalStyle=0;
static LONG_PTR g_rc48OriginalExStyle=0;
static volatile LONG64 g_rc48ChildAttach=0;
static volatile LONG64 g_rc48TopRestore=0;
static volatile LONG64 g_rc48Repairs=0;
static volatile LONG64 g_rc48WindowedSetPos=0;

static void rc48_capture_original() noexcept {
    if(InterlockedCompareExchange(&g_rc48HaveOriginal,0,0) || !IsWindow(g_presenter)) return;
    g_rc48OriginalOwner=GetWindow(g_presenter,GW_OWNER);
    g_rc48OriginalStyle=GetWindowLongPtrW(g_presenter,GWL_STYLE);
    g_rc48OriginalExStyle=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);
    InterlockedExchange(&g_rc48HaveOriginal,1);
    logfmt("RC48_ORIGINAL_PRESENTER owner/style/ex",reinterpret_cast<LONG_PTR>(g_rc48OriginalOwner),g_rc48OriginalStyle,g_rc48OriginalExStyle,0);
}

static bool rc48_windowed_mode() noexcept {
    return InterlockedCompareExchange(&g_rc45Borderless,0,0)==0;
}

static bool rc48_make_child(bool countRepair) noexcept {
    if(!IsWindow(g_game)||!IsWindow(g_presenter)||!rc48_windowed_mode()) return false;
    rc48_capture_original();
    const LONG_PTR current=GetWindowLongPtrW(g_presenter,GWL_STYLE);
    const HWND parent=GetParent(g_presenter);
    bool changed=false;
    if(!(current&WS_CHILD) || (current&WS_POPUP)){
        const LONG_PTR desired=(current & (WS_VISIBLE|WS_DISABLED|WS_CLIPSIBLINGS|WS_CLIPCHILDREN)) |
                               WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        SetWindowLongPtrW(g_presenter,GWL_STYLE,desired);
        changed=true;
    }
    if(!rc48_windowed_mode()) return false;
    if(GetParent(g_presenter)!=g_game){
        SetLastError(ERROR_SUCCESS);
        SetParent(g_presenter,g_game);
        if(GetParent(g_presenter)!=g_game){
            logfmt("FAIL RC48 SetParent child",reinterpret_cast<LONG_PTR>(parent),reinterpret_cast<LONG_PTR>(g_game),GetLastError(),0);
            return false;
        }
        changed=true;
    }
    if(!rc48_windowed_mode()) return false;
    LONG_PTR ex=GetWindowLongPtrW(g_presenter,GWL_EXSTYLE);
    const LONG_PTR desiredEx=(ex|WS_EX_NOACTIVATE) & ~(LONG_PTR)(WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_APPWINDOW|WS_EX_TOOLWINDOW);
    if(ex!=desiredEx){SetWindowLongPtrW(g_presenter,GWL_EXSTYLE,desiredEx);changed=true;}
    RECT c{};if(GetClientRect(g_game,&c) && rc48_windowed_mode()){
        SetWindowPos(g_presenter,HWND_TOP,0,0,c.right-c.left,c.bottom-c.top,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    }
    if(!rc48_windowed_mode()) return false;
    InterlockedExchange(&g_rc48IsChild,1);
    if(changed){
        InterlockedIncrement64(&g_rc48ChildAttach);
        if(countRepair) InterlockedIncrement64(&g_rc48Repairs);
        logfmt("RC48_WINDOWED_CHILD_ATTACHED",reinterpret_cast<LONG_PTR>(g_game),GetWindowLongPtrW(g_presenter,GWL_STYLE),GetWindowLongPtrW(g_presenter,GWL_EXSTYLE),0);
    }
    return true;
}

static bool rc48_restore_top_level(bool countRepair,bool force=false) noexcept {
    if(!IsWindow(g_presenter)) return false;
    if(!force && rc48_windowed_mode()) return true;
    rc48_capture_original();
    const LONG_PTR cur=GetWindowLongPtrW(g_presenter,GWL_STYLE);
    bool changed=false;
    // GetParent() returns the owner for a top-level WS_POPUP. Therefore owner==game
    // is NOT evidence that the presenter is still a child. WS_CHILD is the only
    // discriminator used here; otherwise the borderless worker would repeatedly
    // detach a perfectly valid owned popup and collapse its geometry.
    if(cur&WS_CHILD){
        SetLastError(ERROR_SUCCESS);
        SetParent(g_presenter,nullptr);
        if((GetWindowLongPtrW(g_presenter,GWL_STYLE)&WS_CHILD) && GetParent(g_presenter)==g_game){
            logfmt("FAIL RC48 SetParent desktop",reinterpret_cast<LONG_PTR>(g_game),GetLastError(),0,0);
            return false;
        }
        changed=true;
    }
    if(!force && rc48_windowed_mode()) return false;
    LONG_PTR desired=g_rc48OriginalStyle;
    desired=(desired & ~(LONG_PTR)WS_CHILD) | WS_POPUP;
    if(GetWindowLongPtrW(g_presenter,GWL_STYLE)!=desired){SetWindowLongPtrW(g_presenter,GWL_STYLE,desired);changed=true;}
    if(InterlockedCompareExchange(&g_rc48HaveOriginal,0,0)){
        SetWindowLongPtrW(g_presenter,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(g_rc48OriginalOwner));
    }
    InterlockedExchange(&g_rc48IsChild,0);
    if(changed){
        InterlockedIncrement64(&g_rc48TopRestore);
        if(countRepair) InterlockedIncrement64(&g_rc48Repairs);
        logfmt("RC48_BORDERLESS_TOPLEVEL_RESTORED",reinterpret_cast<LONG_PTR>(g_rc48OriginalOwner),GetWindowLongPtrW(g_presenter,GWL_STYLE),0,0);
    }
    return true;
}

static LONG_PTR WINAPI RC48_SetWindowLongPtrW(HWND h,int index,LONG_PTR value){
    if(h!=g_presenter || !IsWindow(h)) return SetWindowLongPtrW(h,index,value);
    rc48_capture_original();
    if(index==GWL_STYLE){
        if(rc48_windowed_mode()){
            LONG_PTR desired=(value & (WS_VISIBLE|WS_DISABLED|WS_CLIPSIBLINGS|WS_CLIPCHILDREN)) |
                             WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
            const LONG_PTR prior=SetWindowLongPtrW(h,index,desired);
            if(rc48_windowed_mode() && GetParent(h)!=g_game && IsWindow(g_game)) SetParent(h,g_game);
            if(rc48_windowed_mode()) InterlockedExchange(&g_rc48IsChild,1);
            return prior;
        }
        if(GetWindowLongPtrW(h,GWL_STYLE)&WS_CHILD) rc48_restore_top_level(false);
        value=(value & ~(LONG_PTR)WS_CHILD)|WS_POPUP;
        return SetWindowLongPtrW(h,index,value);
    }
    if(index==GWL_EXSTYLE && rc48_windowed_mode()){
        value=(value|WS_EX_NOACTIVATE) & ~(LONG_PTR)(WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_APPWINDOW|WS_EX_TOOLWINDOW);
    }
    return SetWindowLongPtrW(h,index,value);
}

static BOOL WINAPI RC48_SetWindowPos(HWND h,HWND after,int x,int y,int cx,int cy,UINT flags){
    if(h!=g_presenter || !IsWindow(h)) return SetWindowPos(h,after,x,y,cx,cy,flags);
    if(rc48_windowed_mode() && IsWindow(g_game)){
        rc48_make_child(false);
        RECT c{};if(rc48_windowed_mode() && GetClientRect(g_game,&c)){
            InterlockedIncrement64(&g_rc48WindowedSetPos);
            return SetWindowPos(h,HWND_TOP,0,0,c.right-c.left,c.bottom-c.top,
                                (flags|SWP_NOACTIVATE|SWP_SHOWWINDOW)&~(SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER));
        }
    } else if(!rc48_windowed_mode()) {
        rc48_restore_top_level(false);
    }
    return SetWindowPos(h,after,x,y,cx,cy,flags);
}

static BOOL WINAPI RC48_ShowWindow(HWND h,int cmd){
    if(h==g_presenter && IsWindow(h) && rc48_windowed_mode() && cmd!=SW_HIDE) rc48_make_child(false);
    return ShowWindow(h,cmd);
}

static DWORD WINAPI RC48InvariantWorker(LPVOID){
    LONG last=-1;
    while(!InterlockedCompareExchange(&g_rc48Stop,0,0)){
        if(!InterlockedCompareExchange(&g_rc45Installed,0,0) || !IsWindow(g_game) || !IsWindow(g_presenter)){Sleep(10);continue;}
        const LONG mode=InterlockedCompareExchange(&g_rc45Borderless,0,0);
        const LONG_PTR s=GetWindowLongPtrW(g_presenter,GWL_STYLE);
        if(mode==0){
            if(last!=0 || !(s&WS_CHILD) || (s&WS_POPUP) || GetParent(g_presenter)!=g_game) rc48_make_child(last==0);
        } else {
            if(last!=1 || (s&WS_CHILD)) rc48_restore_top_level(last==1);
        }
        last=InterlockedCompareExchange(&g_rc45Borderless,0,0);
        Sleep(25);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID reserved){
    if(reason==DLL_PROCESS_ATTACH){
        if(!DllMain_RC46_INTERNAL(h,reason,reserved)) return FALSE;
        InterlockedExchange(&g_rc48Stop,0);
        HANDLE th=CreateThread(nullptr,0,RC48InvariantWorker,nullptr,0,nullptr);
        if(th) CloseHandle(th); else logline("WARN RC48 invariant worker unavailable; RC46 remains active");
        logline("RC48_CHILD_PRESENTER_MODULE_LOADED");
        return TRUE;
    }
    if(reason==DLL_PROCESS_DETACH){
        InterlockedExchange(&g_rc48Stop,1);
        if(IsWindow(g_presenter)) rc48_restore_top_level(false,true);
        return DllMain_RC46_INTERNAL(h,reason,reserved);
    }
    return DllMain_RC46_INTERNAL(h,reason,reserved);
}
