#include "../rc38/ptar_borderless_rc38.cpp"
#include "../../raster/rc41b/ptar_rc41b_bootstrap.h"

static volatile LONG g_prodStarting=0;
static volatile LONG g_prodStableMode=0;
static HMODULE g_prodRuntime=nullptr;

static void prod_starting_clear(){InterlockedExchange(&g_prodStarting,0);}

static DWORD WINAPI StableWorker(LPVOID){
    if(!IsWindow(g_game)||!IsWindow(g_presenter)){prod_starting_clear();return 20;}
    LONG_PTR cur=get_wndproc(g_game);
    if(!cur){logline("FAIL RC40 stable game proc unavailable; fail-open");prod_starting_clear();return 21;}
    g_gameNext=(WNDPROC)cur;
    if(!set_wndproc(g_game,GameProc)){logline("FAIL RC40 stable game subclass install");prod_starting_clear();return 22;}
    g_presenterNext=(WNDPROC)get_wndproc(g_presenter);
    if(!g_presenterNext||!set_wndproc(g_presenter,PresenterProc)){
        set_wndproc(g_game,g_gameNext);logline("FAIL RC40 stable presenter subclass install");prod_starting_clear();return 23;
    }
    patch_iat(GetModuleHandleW(nullptr));
    InterlockedExchange(&g_active,1);
    InterlockedExchange(&g_prodStableMode,1);
    enforce_geometry();
    prod_starting_clear();
    UINT gw=0,gh=0,pw=0,ph=0;client_size(g_game,gw,gh);client_size(g_presenter,pw,ph);
    logfmt("ACTIVE_RC40_POSTWNDPROC game/presenter",(LONG_PTR)((gw<<16)^gh),(LONG_PTR)((pw<<16)^ph),g_iatHooks);
    RC41B_StartBootstrap(g_self,g_prodRuntime);
    return 0;
}

static int attach_stable(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    if(InterlockedCompareExchange(&g_active,0,0))return 1;
    if(InterlockedCompareExchange(&g_prodStarting,1,0))return 2;
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)||!renderW||!renderH||!outputW||!outputH||renderW>outputW||renderH>outputH){
        logline("FAIL RC40 invalid stable attach args");prod_starting_clear();return -10;
    }
    HMONITOR hm=MonitorFromWindow(presenter,MONITOR_DEFAULTTONEAREST);MONITORINFO mi{};mi.cbSize=sizeof(mi);
    if(!hm||!GetMonitorInfoW(hm,&mi)){logline("FAIL RC40 stable monitor resolve");prod_starting_clear();return -11;}
    UINT mw=(UINT)(mi.rcMonitor.right-mi.rcMonitor.left),mh=(UINT)(mi.rcMonitor.bottom-mi.rcMonitor.top);
    if(mw!=outputW||mh!=outputH){logfmt("FAIL RC40 output/monitor mismatch",mw,mh,outputW,outputH);prod_starting_clear();return -12;}
    g_game=game;g_presenter=presenter;g_renderW=renderW;g_renderH=renderH;g_outputW=outputW;g_outputH=outputH;g_monitor=mi.rcMonitor;
    g_initialGameProc=(WNDPROC)get_wndproc(game);g_presenterStyle0=GetWindowLongPtrW(presenter,GWL_STYLE);g_presenterExStyle0=GetWindowLongPtrW(presenter,GWL_EXSTYLE);
    if(!g_initialGameProc){logline("FAIL RC40 stable initial game proc");prod_starting_clear();return -13;}
    HANDLE th=CreateThread(nullptr,0,StableWorker,nullptr,0,nullptr);
    if(!th){logline("FAIL RC40 stable worker create");prod_starting_clear();return -14;}
    CloseHandle(th);logfmt("ATTACH_RC40_POSTWNDPROC_PENDING",renderW,renderH,outputW,outputH);return 0;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAttachStable(HWND game,HWND presenter,UINT renderW,UINT renderH,UINT outputW,UINT outputH){
    return attach_stable(game,presenter,renderW,renderH,outputW,outputH);
}

static bool runtime_layout_ok(HMODULE runtime,BYTE*& base,IMAGE_NT_HEADERS64*& nt){
    if(!runtime)return false;base=(BYTE*)runtime;
    auto* dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0)return false;
    nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
    if(nt->OptionalHeader.SizeOfImage<0x02C7E010u)return false;
    FARPROC exported=GetProcAddress(runtime,"D3D11CreateDeviceAndSwapChain");
    if(exported!=(FARPROC)(base+0x000021C0u))return false;
    static const BYTE gameAnchor[]={0x49,0x8B,0x49,0x30,0x48,0x89,0x0D,0xBA,0xC2,0xC3,0x02};
    if(memcmp(base+0x000037FBu,gameAnchor,sizeof(gameAnchor))!=0)return false;
    static const BYTE restoredPresenterLog[]={0xE8,0xAD,0x70,0xFF,0xFF};
    if(memcmp(base+0x0000C2FEu,restoredPresenterLog,sizeof(restoredPresenterLog))!=0)return false;
    BYTE* call=base+0x00003806u;if(call[0]!=0xE8)return false;
    INT32 disp=0;memcpy(&disp,call+1,sizeof(disp));BYTE* target=call+5+disp;
    if(target!=base+0x03500500u)return false;
    static const BYTE loaderCallsP1U46[]={0x49,0x8D,0x83,0x60,0xC4,0x00,0x00};
    if(memcmp(base+0x03500517u,loaderCallsP1U46,sizeof(loaderCallsP1U46))!=0)return false;
    auto* sh=IMAGE_FIRST_SECTION(nt);bool rc38=false;
    for(WORD i=0;i<nt->FileHeader.NumberOfSections;++i){if(memcmp(sh[i].Name,".rc38",5)==0&&sh[i].VirtualAddress==0x03500000u){rc38=true;break;}}
    return rc38;
}

extern "C" __declspec(dllexport) int WINAPI PTAR_BorderlessAutoStart(HMODULE runtime){
    if(InterlockedCompareExchange(&g_active,0,0))return 1;
    BYTE* base=nullptr;IMAGE_NT_HEADERS64* nt=nullptr;
    if(!runtime_layout_ok(runtime,base,nt)){logline("FAIL RC40 runtime post-WndProc layout guard; fail-open");return -20;}
    HWND game=*(HWND*)(base+0x02C3FAC0u);HWND presenter=*(HWND*)(base+0x02C7DFE0u);
    UINT renderW=*(UINT*)(base+0x02C3FB78u),renderH=*(UINT*)(base+0x02C3FB7Cu);
    UINT outputW=*(UINT*)(base+0x0004B040u),outputH=*(UINT*)(base+0x0004B044u);
    if(!game||!presenter||!IsWindow(game)||!IsWindow(presenter)){logline("FAIL RC40 post-WndProc windows unavailable; fail-open");return -21;}
    if(!renderW||!renderH||!outputW||!outputH){logfmt("FAIL RC40 runtime dimensions",renderW,renderH,outputW,outputH);return -22;}
    LONG_PTR proc=get_wndproc(game);
    if(proc!=(LONG_PTR)(base+0x0000DC50u)){logfmt("FAIL RC40 expected P1U46 WndProc not active",proc,(LONG_PTR)(base+0x0000DC50u));return -23;}
    g_prodRuntime=runtime;
    logfmt("AUTO_START_RC40_POSTWNDPROC geometry",renderW,renderH,outputW,outputH);
    return attach_stable(game,presenter,renderW,renderH,outputW,outputH);
}
