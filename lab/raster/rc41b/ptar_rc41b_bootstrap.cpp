#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wchar.h>
#include <stdint.h>
#include "ptar_rc41b_bootstrap.h"

namespace {
volatile LONG g_workerStarted=0;
HMODULE g_owner=nullptr;
HMODULE g_runtime=nullptr;

RC41BTestHooks g_hooks={
    GetModuleHandleW,
    LoadLibraryW,
    GetProcAddress,
    GetLastError,
    Sleep,
    GetFileAttributesW
};

using AutoStartFn=int (WINAPI*)(HMODULE);
using QueryFn=int (WINAPI*)(RC41BState*);

static void write_log(const wchar_t* logPath,const char* text) noexcept {
    if(!logPath||!text)return;
    HANDLE h=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME st{};GetLocalTime(&st);
    char line[1024]{};
    int n=wsprintfA(line,"[%02u:%02u:%02u.%03u] %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,text);
    DWORD wr=0;WriteFile(h,line,(DWORD)n,&wr,nullptr);CloseHandle(h);
}
static void log_u32(const wchar_t* logPath,const char* tag,DWORD a,DWORD b=0,DWORD c=0,DWORD d=0) noexcept {
    char x[768]{};wsprintfA(x,"%s %lu %lu %lu %lu",tag,(unsigned long)a,(unsigned long)b,(unsigned long)c,(unsigned long)d);write_log(logPath,x);
}
static void log_ptr(const wchar_t* logPath,const char* tag,const void* p,DWORD err=0) noexcept {
    char x[768]{};wsprintfA(x,"%s ptr=0x%p gle=%lu",tag,p,(unsigned long)err);write_log(logPath,x);
}
static void log_wide(const wchar_t* logPath,const char* tag,const wchar_t* value) noexcept {
    char x[900]{};
    char narrow[700]{};
    if(value)WideCharToMultiByte(CP_ACP,0,value,-1,narrow,(int)sizeof(narrow),nullptr,nullptr);
    wsprintfA(x,"%s %s",tag,narrow);write_log(logPath,x);
}

static bool query_and_log(const wchar_t* logPath,QueryFn query,RC41BState& state) noexcept {
    state={};state.size=sizeof(state);
    if(!query){write_log(logPath,"QUERY missing");return false;}
    SetLastError(ERROR_SUCCESS);
    const int rc=query(&state);
    const DWORD gle=g_hooks.getLastError?g_hooks.getLastError():GetLastError();
    char x[1024]{};
    wsprintfA(x,
        "QUERY rc=%d gle=%lu active=%u logical=%ux%u physical=%ux%u bridge=%u context=%u swapchain=%u vp=%I64u sc=%I64u desc=%I64u resize=%I64u refresh=%I64u tex=%I64u fallback=%I64u rtv=%I64u dsv=%I64u",
        rc,(unsigned long)gle,state.active,state.logicalW,state.logicalH,state.physicalW,state.physicalH,
        state.resourceBridgeInstalled,state.contextInstalled,state.swapchainInstalled,
        (unsigned long long)state.viewportMapped,(unsigned long long)state.scissorMapped,
        (unsigned long long)state.getDescVirtualized,(unsigned long long)state.resizeRemapped,
        (unsigned long long)state.primaryRefreshes,(unsigned long long)state.textureRemapped,
        (unsigned long long)state.textureFallbacks,(unsigned long long)state.rtvTagged,(unsigned long long)state.dsvTagged);
    write_log(logPath,x);
    return rc==0 && state.active==1 && state.logicalW==1920 && state.logicalH==1080 && state.physicalW==1280 && state.physicalH==720 &&
           state.resourceBridgeInstalled==1 && state.contextInstalled==1 && state.swapchainInstalled==1;
}

static DWORD WINAPI bootstrap_worker(LPVOID) noexcept {
    wchar_t ownerPath[MAX_PATH]{};
    wchar_t dllPath[MAX_PATH]{};
    wchar_t logPath[MAX_PATH]{};
    if(!g_owner||!GetModuleFileNameW(g_owner,ownerPath,MAX_PATH))return 100;
    if(!RC41B_BuildSiblingPath(ownerPath,L"ptar_rc41.dll",dllPath,MAX_PATH))return 101;
    if(!RC41B_BuildSiblingPath(ownerPath,L"ptar_rc41_bootstrap.log",logPath,MAX_PATH))return 102;
    return (DWORD)RC41B_RunLoaderWithPathsForTest(g_runtime,dllPath,logPath,20,250);
}
}

bool RC41B_BuildSiblingPath(const wchar_t* modulePath,const wchar_t* leafName,wchar_t* out,size_t outCount) noexcept {
    if(!modulePath||!leafName||!out||outCount<4)return false;
    const size_t n=wcslen(modulePath),leaf=wcslen(leafName);
    if(n+1>outCount)return false;
    const wchar_t* slash=wcsrchr(modulePath,L'\\');
    const wchar_t* slash2=wcsrchr(modulePath,L'/');
    if(!slash || (slash2&&slash2>slash))slash=slash2;
    if(!slash)return false;
    const size_t prefix=(size_t)(slash-modulePath)+1;
    if(prefix+leaf+1>outCount)return false;
    wmemcpy(out,modulePath,prefix);wmemcpy(out+prefix,leafName,leaf+1);return true;
}

void RC41B_SetTestHooks(const RC41BTestHooks* hooks) noexcept {
    if(hooks)g_hooks=*hooks;
}
void RC41B_ResetTestHooks() noexcept {
    RC41BTestHooks d={GetModuleHandleW,LoadLibraryW,GetProcAddress,GetLastError,Sleep,GetFileAttributesW};g_hooks=d;
}

int RC41B_RunLoaderWithPathsForTest(HMODULE runtimeModule,const wchar_t* dllPath,const wchar_t* logPath,unsigned maxAttempts,DWORD retryDelayMs) noexcept {
    if(!runtimeModule||!dllPath||!logPath||maxAttempts==0)return -100;
    write_log(logPath,"BOOTSTRAP_RC41B_ENTERED");
    log_ptr(logPath,"RUNTIME_MODULE",runtimeModule,0);
    log_wide(logPath,"RC41_DLL_ABSOLUTE",dllPath);
    const DWORD attr=g_hooks.getFileAttributesW?g_hooks.getFileAttributesW(dllPath):INVALID_FILE_ATTRIBUTES;
    log_u32(logPath,"RC41_DLL_INITIAL_ATTR",attr,attr==INVALID_FILE_ATTRIBUTES?1u:0u);

    HMODULE sidecar=nullptr;
    AutoStartFn autoStart=nullptr;
    QueryFn query=nullptr;
    for(unsigned attempt=1;attempt<=maxAttempts;++attempt){
        log_u32(logPath,"ATTEMPT",attempt,maxAttempts,retryDelayMs,0);
        if(!sidecar){
            SetLastError(ERROR_SUCCESS);
            sidecar=g_hooks.loadLibraryW?g_hooks.loadLibraryW(dllPath):nullptr;
            const DWORD gle=g_hooks.getLastError?g_hooks.getLastError():GetLastError();
            log_ptr(logPath,"LoadLibraryW",sidecar,gle);
            if(!sidecar){if(attempt<maxAttempts && g_hooks.sleep)g_hooks.sleep(retryDelayMs);continue;}
        }
        if(!autoStart){
            SetLastError(ERROR_SUCCESS);
            FARPROC p=g_hooks.getProcAddress?g_hooks.getProcAddress(sidecar,"PTAR_RC41_AutoStart"):nullptr;
            const DWORD gle=g_hooks.getLastError?g_hooks.getLastError():GetLastError();
            log_ptr(logPath,"GetProcAddress PTAR_RC41_AutoStart",p,gle);
            autoStart=reinterpret_cast<AutoStartFn>(p);
            if(!autoStart){write_log(logPath,"PERMANENT_FAILURE missing AutoStart export");return -102;}
        }
        if(!query){
            SetLastError(ERROR_SUCCESS);
            FARPROC p=g_hooks.getProcAddress?g_hooks.getProcAddress(sidecar,"PTAR_RC41_Query"):nullptr;
            const DWORD gle=g_hooks.getLastError?g_hooks.getLastError():GetLastError();
            log_ptr(logPath,"GetProcAddress PTAR_RC41_Query",p,gle);
            query=reinterpret_cast<QueryFn>(p);
            if(!query){write_log(logPath,"PERMANENT_FAILURE missing Query export");return -103;}
        }

        SetLastError(ERROR_SUCCESS);
        const int autoRc=autoStart(runtimeModule);
        const DWORD autoGle=g_hooks.getLastError?g_hooks.getLastError():GetLastError();
        log_u32(logPath,"PTAR_RC41_AutoStart rc/gle",(DWORD)autoRc,autoGle,0,0);
        RC41BState state{};
        if(query_and_log(logPath,query,state)){
            write_log(logPath,"RC41B_ACTIVATION_PROVEN logical=1920x1080 physical=1280x720");
            return 0;
        }
        if(attempt<maxAttempts && g_hooks.sleep)g_hooks.sleep(retryDelayMs);
    }
    write_log(logPath,"RC41B_RETRY_BUDGET_EXPIRED_FAIL_OPEN");
    return -104;
}

void RC41B_StartBootstrap(HMODULE ownerModule,HMODULE runtimeModule) noexcept {
    if(!ownerModule||!runtimeModule)return;
    if(InterlockedCompareExchange(&g_workerStarted,1,0)!=0)return;
    g_owner=ownerModule;g_runtime=runtimeModule;
    HANDLE th=CreateThread(nullptr,0,bootstrap_worker,nullptr,0,nullptr);
    if(!th){InterlockedExchange(&g_workerStarted,0);return;}
    CloseHandle(th);
}
