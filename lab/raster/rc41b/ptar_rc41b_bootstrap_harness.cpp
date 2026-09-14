#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "ptar_rc41b_bootstrap.h"

namespace {
int g_loadCalls=0;
int g_autoCalls=0;
int g_queryCalls=0;
int g_sleepCalls=0;
int g_failLoads=0;
bool g_missingAuto=false;
bool g_missingQuery=false;
bool g_active=false;
DWORD g_lastError=0;

HMODULE WINAPI fake_get_module(LPCWSTR){return reinterpret_cast<HMODULE>(0x11110000ull);}
HMODULE WINAPI fake_load(LPCWSTR path){
    ++g_loadCalls;
    if(!path||!wcsstr(path,L"ptar_rc41.dll")){g_lastError=ERROR_FILE_NOT_FOUND;return nullptr;}
    if(g_loadCalls<=g_failLoads){g_lastError=ERROR_MOD_NOT_FOUND;return nullptr;}
    g_lastError=ERROR_SUCCESS;return reinterpret_cast<HMODULE>(0x22220000ull);
}
int WINAPI fake_auto(HMODULE runtime){
    ++g_autoCalls;
    if(!runtime)return -20;
    g_active=true;return 0;
}
int WINAPI fake_query(RC41BState* out){
    ++g_queryCalls;
    if(!out||out->size<sizeof(RC41BState))return -1;
    RC41BState s{};s.size=sizeof(s);s.active=g_active?1u:0u;
    if(g_active){
        s.logicalW=1920;s.logicalH=1080;s.physicalW=1280;s.physicalH=720;
        s.resourceBridgeInstalled=1;s.contextInstalled=1;s.swapchainInstalled=1;
    }
    *out=s;return 0;
}
FARPROC WINAPI fake_proc(HMODULE,const char* name){
    if(!name){g_lastError=ERROR_PROC_NOT_FOUND;return nullptr;}
    if(std::strcmp(name,"PTAR_RC41_AutoStart")==0){
        if(g_missingAuto){g_lastError=ERROR_PROC_NOT_FOUND;return nullptr;}
        g_lastError=ERROR_SUCCESS;return reinterpret_cast<FARPROC>(fake_auto);
    }
    if(std::strcmp(name,"PTAR_RC41_Query")==0){
        if(g_missingQuery){g_lastError=ERROR_PROC_NOT_FOUND;return nullptr;}
        g_lastError=ERROR_SUCCESS;return reinterpret_cast<FARPROC>(fake_query);
    }
    g_lastError=ERROR_PROC_NOT_FOUND;return nullptr;
}
DWORD WINAPI fake_error(){return g_lastError;}
void WINAPI fake_sleep(DWORD){++g_sleepCalls;}
DWORD WINAPI fake_attr(LPCWSTR path){return (path&&wcsstr(path,L"ptar_rc41.dll"))?FILE_ATTRIBUTE_NORMAL:INVALID_FILE_ATTRIBUTES;}

void reset(){g_loadCalls=g_autoCalls=g_queryCalls=g_sleepCalls=0;g_failLoads=0;g_missingAuto=false;g_missingQuery=false;g_active=false;g_lastError=0;}
void install_hooks(){RC41BTestHooks h={fake_get_module,fake_load,fake_proc,fake_error,fake_sleep,fake_attr};RC41B_SetTestHooks(&h);}
bool expect(bool v,const char* what){if(!v)std::printf("FAIL %s\n",what);return v;}
}

int wmain(){
    install_hooks();
    wchar_t path[MAX_PATH]{};
    if(!expect(RC41B_BuildSiblingPath(L"C:\\Program Files (x86)\\Warhammer Martyr\\ptar_borderless.dll",L"ptar_rc41.dll",path,MAX_PATH),"path build"))return 10;
    if(!expect(std::wcscmp(path,L"C:\\Program Files (x86)\\Warhammer Martyr\\ptar_rc41.dll")==0,"path with spaces"))return 11;

    reset();g_failLoads=1;
    int rc=RC41B_RunLoaderWithPathsForTest(reinterpret_cast<HMODULE>(0x33330000ull),path,L"RC41B_HARNESS_RETRY.log",4,1);
    if(!expect(rc==0,"first-fail-then-success result"))return 20;
    if(!expect(g_loadCalls==2,"two load attempts"))return 21;
    if(!expect(g_autoCalls==1,"AutoStart once after load"))return 22;
    if(!expect(g_queryCalls==1,"Query once after activation"))return 23;
    if(!expect(g_sleepCalls==1,"single bounded retry sleep"))return 24;

    reset();g_failLoads=99;
    rc=RC41B_RunLoaderWithPathsForTest(reinterpret_cast<HMODULE>(0x33330000ull),path,L"RC41B_HARNESS_EXPIRE.log",3,1);
    if(!expect(rc==-104,"retry budget expiry"))return 30;
    if(!expect(g_loadCalls==3&&g_autoCalls==0&&g_sleepCalls==2,"expiry counters"))return 31;

    reset();g_missingAuto=true;
    rc=RC41B_RunLoaderWithPathsForTest(reinterpret_cast<HMODULE>(0x33330000ull),path,L"RC41B_HARNESS_MISSING_AUTO.log",3,1);
    if(!expect(rc==-102,"missing AutoStart export"))return 40;
    if(!expect(g_loadCalls==1&&g_autoCalls==0&&g_sleepCalls==0,"missing AutoStart fail fast"))return 41;

    reset();g_missingQuery=true;
    rc=RC41B_RunLoaderWithPathsForTest(reinterpret_cast<HMODULE>(0x33330000ull),path,L"RC41B_HARNESS_MISSING_QUERY.log",3,1);
    if(!expect(rc==-103,"missing Query export"))return 50;
    if(!expect(g_loadCalls==1&&g_autoCalls==0&&g_sleepCalls==0,"missing Query fail fast before AutoStart"))return 51;

    RC41B_ResetTestHooks();
    std::printf("RC41B_BOOTSTRAP_HARNESS=PASS\n");
    std::printf("FIRST_FAIL_THEN_SUCCESS=PASS\n");
    std::printf("ABSOLUTE_PATH_WITH_SPACES=PASS\n");
    std::printf("RETRY_BUDGET_FAIL_OPEN=PASS\n");
    std::printf("MISSING_EXPORT_FAIL_OPEN=PASS\n");
    return 0;
}
