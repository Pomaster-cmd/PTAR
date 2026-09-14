#define DllMain DllMain_RC46_INTERNAL
#include "../rc46/ptar_borderless_rc46_prod.cpp"
#undef DllMain

// RC47 fixes the remaining field-only Windowed failure without changing RC46's
// validated geometry/raster path. RC46 field logs prove that the real game HWND
// is held at the correct windowed rect while P1U46 repeatedly raises that HWND.
// The real presenter is an unowned top-level popup, so those HWND_TOP operations
// can leave the valid reconstructed presenter behind the opaque black game client.
//
// In Windowed mode RC47 makes the presenter an owned popup of the game HWND.
// Windows then keeps the owned popup above its owner without TOPMOST. In
// Borderless mode RC47 restores the exact original presenter owner before RC46
// continues using the already validated borderless path.

static HWND g_rc47OriginalPresenterOwner=nullptr;
static volatile LONG g_rc47HaveOriginalOwner=0;
static volatile LONG g_rc47Stop=0;
static volatile LONG64 g_rc47OwnerAttach=0;
static volatile LONG64 g_rc47OwnerRestore=0;

static bool rc47_set_owner(HWND owner) noexcept {
    if(!IsWindow(g_presenter)) return false;
    const HWND before=GetWindow(g_presenter,GW_OWNER);
    if(before==owner) return true;
    SetLastError(ERROR_SUCCESS);
    SetWindowLongPtrW(g_presenter,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(owner));
    const DWORD gle=GetLastError();
    const HWND after=GetWindow(g_presenter,GW_OWNER);
    if(after!=owner){
        logfmt("FAIL RC47 presenter owner change",reinterpret_cast<LONG_PTR>(before),reinterpret_cast<LONG_PTR>(owner),reinterpret_cast<LONG_PTR>(after),gle);
        return false;
    }
    return true;
}

static void rc47_apply_mode_owner(LONG borderless) noexcept {
    if(!IsWindow(g_game)||!IsWindow(g_presenter)) return;
    if(!InterlockedCompareExchange(&g_rc47HaveOriginalOwner,0,0)){
        g_rc47OriginalPresenterOwner=GetWindow(g_presenter,GW_OWNER);
        InterlockedExchange(&g_rc47HaveOriginalOwner,1);
        logfmt("RC47_ORIGINAL_PRESENTER_OWNER",reinterpret_cast<LONG_PTR>(g_rc47OriginalPresenterOwner),reinterpret_cast<LONG_PTR>(g_game),0,0);
    }
    if(borderless){
        if(GetWindow(g_presenter,GW_OWNER)!=g_rc47OriginalPresenterOwner && rc47_set_owner(g_rc47OriginalPresenterOwner)){
            InterlockedIncrement64(&g_rc47OwnerRestore);
            logfmt("RC47_BORDERLESS_OWNER_RESTORED",reinterpret_cast<LONG_PTR>(g_rc47OriginalPresenterOwner),0,0,0);
        }
        return;
    }
    if(GetWindow(g_presenter,GW_OWNER)!=g_game && rc47_set_owner(g_game)){
        InterlockedIncrement64(&g_rc47OwnerAttach);
        logfmt("RC47_WINDOWED_OWNER_ATTACHED",reinterpret_cast<LONG_PTR>(g_game),0,0,0);
    }
    // RC46 already owns geometry and input routing. Reassert its target after an
    // ownership change so the popup is visible immediately at the client rect.
    rc46_force_presenter_to_target(true);
}

static DWORD WINAPI RC47OwnerWorker(LPVOID){
    for(unsigned i=0;i<800 && !InterlockedCompareExchange(&g_rc47Stop,0,0);++i){
        if(InterlockedCompareExchange(&g_rc45Installed,0,0) && IsWindow(g_game) && IsWindow(g_presenter)) break;
        Sleep(10);
    }
    if(!InterlockedCompareExchange(&g_rc45Installed,0,0) || !IsWindow(g_game) || !IsWindow(g_presenter)){
        logline("FAIL RC47 owner worker install timeout; RC46 remains fail-open");
        return 20;
    }
    LONG last=-1;
    while(!InterlockedCompareExchange(&g_rc47Stop,0,0) && InterlockedCompareExchange(&g_rc45Installed,0,0) && IsWindow(g_game) && IsWindow(g_presenter)){
        const LONG mode=InterlockedCompareExchange(&g_rc45Borderless,0,0);
        const HWND want=mode?(InterlockedCompareExchange(&g_rc47HaveOriginalOwner,0,0)?g_rc47OriginalPresenterOwner:GetWindow(g_presenter,GW_OWNER)):g_game;
        if(mode!=last || GetWindow(g_presenter,GW_OWNER)!=want) rc47_apply_mode_owner(mode);
        last=mode;
        Sleep(25);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID reserved){
    if(reason==DLL_PROCESS_ATTACH){
        if(!DllMain_RC46_INTERNAL(h,reason,reserved)) return FALSE;
        InterlockedExchange(&g_rc47Stop,0);
        HANDLE th=CreateThread(nullptr,0,RC47OwnerWorker,nullptr,0,nullptr);
        if(th) CloseHandle(th); else logline("WARN RC47 owner worker unavailable; RC46 remains active");
        return TRUE;
    }
    if(reason==DLL_PROCESS_DETACH){
        InterlockedExchange(&g_rc47Stop,1);
        return DllMain_RC46_INTERNAL(h,reason,reserved);
    }
    return DllMain_RC46_INTERNAL(h,reason,reserved);
}
