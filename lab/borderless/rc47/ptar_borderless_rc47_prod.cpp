#define PTAR_BorderlessAttachStable PTAR_BorderlessAttachStable_RC46_INTERNAL
#define PTAR_BorderlessQueryMode PTAR_BorderlessQueryMode_RC46_INTERNAL
#define PTAR_BorderlessAutoStart PTAR_BorderlessAutoStart_RC46_INTERNAL
#include "../rc46/ptar_borderless_rc46_prod.cpp"
#undef PTAR_BorderlessAttachStable
#undef PTAR_BorderlessQueryMode
#undef PTAR_BorderlessAutoStart

// RC47 fixes the remaining field-only Windowed failure. RC46 proves that the
// game window and presenter geometry are correct, but the P1U46 runtime keeps
// raising the game HWND. On the real game the presenter is an unowned top-level
// popup, so the valid reconstructed frames continue behind an opaque black game
// client. In the old lab host the presenter was accidentally created owned by
// the game, masking this exact z-order failure.
//
// Windowed policy: presenter is an owned top-level popup of the game HWND.
// Windows guarantees an owned popup stays above its owner. It remains
// WS_EX_NOACTIVATE so keyboard focus stays on the game; PresenterProc still
// routes pointer input to the game. Borderless policy: restore the exact
// original presenter owner before reusing the validated RC42/RC46 geometry.

static HWND g_rc47OriginalPresenterOwner=nullptr;
static volatile LONG g_rc47OwnershipWorkerStarted=0;
static volatile LONG64 g_rc47OwnerAttach=0;
static volatile LONG64 g_rc47OwnerRestore=0;
static volatile LONG64 g_rc47ZRepairs=0;

struct PTARRC47ModeState {
    UINT size;
    UINT installed;
    UINT borderlessActive;
    UINT presenterVisible;
    ULONG_PTR gameStyle;
    ULONG_PTR presenterStyle;
    unsigned long long transitionsToWindowed;
    unsigned long long transitionsToBorderless;
    LONG targetLeft;
    LONG targetTop;
    LONG targetRight;
    LONG targetBottom;
    LONG windowStylePreference;
    unsigned long long presenterFollows;
    unsigned long long gameCoercionClamps;
    unsigned long long presenterClamps;
    unsigned long long rejectedWindowSaves;
    LONG savedLeft;
    LONG savedTop;
    LONG savedRight;
    LONG savedBottom;
    ULONG_PTR presenterOwner;
    ULONG_PTR originalPresenterOwner;
    unsigned long long ownerAttach;
    unsigned long long ownerRestore;
    unsigned long long zRepairs;
};

static bool rc47_set_presenter_owner(HWND owner) noexcept {
    if(!IsWindow(g_presenter)) return false;
    const HWND current=GetWindow(g_presenter,GW_OWNER);
    if(current==owner) return true;
    SetLastError(ERROR_SUCCESS);
    SetWindowLongPtrW(g_presenter,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(owner));
    const DWORD gle=GetLastError();
    const HWND after=GetWindow(g_presenter,GW_OWNER);
    if(after!=owner){
        logfmt("FAIL RC47 presenter owner change",reinterpret_cast<LONG_PTR>(current),reinterpret_cast<LONG_PTR>(owner),reinterpret_cast<LONG_PTR>(after),gle);
        return false;
    }
    return true;
}

static void rc47_apply_windowed_ownership(bool forceRaise) noexcept {
    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)||!IsWindow(g_game)||!IsWindow(g_presenter)) return;
    const HWND before=GetWindow(g_presenter,GW_OWNER);
    if(before!=g_game){
        if(rc47_set_presenter_owner(g_game)){
            InterlockedIncrement64(&g_rc47OwnerAttach);
            logfmt("RC47_WINDOWED_OWNER_ATTACHED",reinterpret_cast<LONG_PTR>(before),reinterpret_cast<LONG_PTR>(g_game),0,0);
        } else return;
    }
    // Reassert the current client geometry after an ownership transition. This
    // also repairs z-order after a runtime HWND_TOP attempt without TOPMOST.
    rc46_force_presenter_to_target(forceRaise || before!=g_game);
    if(before==g_game && forceRaise) InterlockedIncrement64(&g_rc47ZRepairs);
}

static void rc47_restore_borderless_owner() noexcept {
    if(!IsWindow(g_presenter)) return;
    const HWND current=GetWindow(g_presenter,GW_OWNER);
    if(current==g_rc47OriginalPresenterOwner) return;
    if(rc47_set_presenter_owner(g_rc47OriginalPresenterOwner)){
        InterlockedIncrement64(&g_rc47OwnerRestore);
        logfmt("RC47_BORDERLESS_OWNER_RESTORED",reinterpret_cast<LONG_PTR>(current),reinterpret_cast<LONG_PTR>(g_rc47OriginalPresenterOwner),0,0);
    }
}

static DWORD WINAPI RC47OwnershipWorker(LPVOID) {
    InterlockedExchange(&g_rc47OwnershipWorkerStarted,1);
    LONG lastMode=-1;
    while(InterlockedCompareExchange(&g_rc45Installed,0,0) && IsWindow(g_game) && IsWindow(g_presenter)){
        const LONG borderless=InterlockedCompareExchange(&g_rc45Borderless,0,0);
        if(borderless){
            if(lastMode!=1) rc47_restore_borderless_owner();
        } else {
            // Runtime can repeatedly put the game at HWND_TOP without a size
            // change. Ownership makes the ordering invariant; refresh geometry
            // only on transition or if ownership was externally removed.
            if(lastMode!=0 || GetWindow(g_presenter,GW_OWNER)!=g_game)
                rc47_apply_windowed_ownership(true);
        }
        lastMode=borderless;
        Sleep(25);
    }
    InterlockedExchange(&g_rc47OwnershipWorkerStarted,0);
    return 0;
}

static DWORD WINAPI RC47StartWorker(LPVOID) {
    for(unsigned i=0;i<400;++i){
        if(InterlockedCompareExchange(&g_rc45Installed,0,0)) break;
        if(!IsWindow(g_game)||!IsWindow(g_presenter)) return 20;
        Sleep(10);
    }
    if(!InterlockedCompareExchange(&g_rc45Installed,0,0)){logline("FAIL RC47 RC46 install timeout; fail-open");return 21;}
    if(InterlockedCompareExchange(&g_rc45Borderless,0,0)) rc47_restore_borderless_owner();
    else rc47_apply_windowed_ownership(true);
    HANDLE th=CreateThread(nullptr,0,RC47OwnershipWorker,nullptr,0,nullptr);
    if(th) CloseHandle(th); else logline("WARN RC47 ownership worker unavailable");
    logfmt("ACTIVE_RC47_WINDOWED_OWNER_GUARD",reinterpret_cast<LONG_PTR>(GetWindow(g_presenter,GW_OWNER)),reinterpret_cast<LONG_PTR>(g_game),InterlockedCompareExchange(&g_rc45Borderless,0,0),0);
    return 0;
}

static int rc47_attach(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    g_rc47OriginalPresenterOwner=presenter?GetWindow(presenter,GW_OWNER):nullptr;
    const int rc=PTAR_BorderlessAttachStable_RC46_INTERNAL(game,presenter,renderW,renderH,outputW,outputH);
    if(rc<0) return rc;
    HANDLE th=CreateThread(nullptr,0,RC47StartWorker,nullptr,0,nullptr);
    if(!th){logline("FAIL RC47 start worker create; RC46 remains active");return rc;}
    CloseHandle(th);
    logfmt("ATTACH_RC47_WINDOWED_OWNER_GUARD_PENDING",renderW,renderH,outputW,outputH);
    return rc;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    return rc47_attach(game,presenter,renderW,renderH,outputW,outputH);
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessQueryMode(PTARRC47ModeState* s){
    if(!s||s->size<offsetof(PTARRC47ModeState,targetLeft)) return -1;
    const UINT caller=s->size;
    PTARRC46ModeState base{};base.size=sizeof(base);
    const int rc=PTAR_BorderlessQueryMode_RC46_INTERNAL(&base);if(rc) return rc;
    ZeroMemory(reinterpret_cast<BYTE*>(s)+sizeof(UINT),caller-sizeof(UINT));
    s->installed=base.installed;s->borderlessActive=base.borderlessActive;s->presenterVisible=base.presenterVisible;
    s->gameStyle=base.gameStyle;s->presenterStyle=base.presenterStyle;s->transitionsToWindowed=base.transitionsToWindowed;s->transitionsToBorderless=base.transitionsToBorderless;
    if(caller>=offsetof(PTARRC47ModeState,presenterOwner)){
        s->targetLeft=base.targetLeft;s->targetTop=base.targetTop;s->targetRight=base.targetRight;s->targetBottom=base.targetBottom;s->windowStylePreference=base.windowStylePreference;
        s->presenterFollows=base.presenterFollows;s->gameCoercionClamps=base.gameCoercionClamps;s->presenterClamps=base.presenterClamps;s->rejectedWindowSaves=base.rejectedWindowSaves;
        s->savedLeft=base.savedLeft;s->savedTop=base.savedTop;s->savedRight=base.savedRight;s->savedBottom=base.savedBottom;
    }
    if(caller>=sizeof(PTARRC47ModeState)){
        s->presenterOwner=reinterpret_cast<ULONG_PTR>(IsWindow(g_presenter)?GetWindow(g_presenter,GW_OWNER):nullptr);
        s->originalPresenterOwner=reinterpret_cast<ULONG_PTR>(g_rc47OriginalPresenterOwner);
        s->ownerAttach=(unsigned long long)InterlockedCompareExchange64(&g_rc47OwnerAttach,0,0);
        s->ownerRestore=(unsigned long long)InterlockedCompareExchange64(&g_rc47OwnerRestore,0,0);
        s->zRepairs=(unsigned long long)InterlockedCompareExchange64(&g_rc47ZRepairs,0,0);
    }
    return 0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(!runtime) return -20;
    BYTE* base=nullptr;IMAGE_NT_HEADERS64* nt=nullptr;
    if(!rc45_runtime_layout_ok(runtime,base,nt)){logline("FAIL RC47 runtime layout guard; fail-open");return -20;}
    HWND game=*(HWND*)(base+0x02C3FAC0u);HWND presenter=*(HWND*)(base+0x02C7DFE0u);
    UINT renderW=*(UINT*)(base+0x02C3FB78u),renderH=*(UINT*)(base+0x02C3FB7Cu),outputW=*(UINT*)(base+0x0004B040u),outputH=*(UINT*)(base+0x0004B044u);
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)){logline("FAIL RC47 windows unavailable; fail-open");return -21;}
    if(!renderW||!renderH||!outputW||!outputH){logfmt("FAIL RC47 runtime dimensions",renderW,renderH,outputW,outputH);return -22;}
    const LONG_PTR proc=get_wndproc(game);if(proc!=(LONG_PTR)(base+0x0000DC50u)){logfmt("FAIL RC47 expected P1U46 WndProc not active",proc,(LONG_PTR)(base+0x0000DC50u));return -23;}
    g_rc45Runtime=runtime;
    logfmt("AUTO_START_RC47_WINDOWED_OWNER_GUARD geometry",renderW,renderH,outputW,outputH);
    return rc47_attach(game,presenter,renderW,renderH,outputW,outputH);
}
