#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <utility>
#include <cstring>

static std::wstring g_runtimePath;
extern "C" DWORD WINAPI FakeGetModuleFileNameW(HMODULE, LPWSTR out, DWORD cap){
    if(!out || cap==0) return 0;
    if(g_runtimePath.size()+1 > cap){ SetLastError(ERROR_INSUFFICIENT_BUFFER); return cap; }
    memcpy(out,g_runtimePath.c_str(),(g_runtimePath.size()+1)*sizeof(wchar_t));
    return (DWORD)g_runtimePath.size();
}
#pragma pack(push,1)
struct Rec { char magic[8]; uint32_t version,stage,attempt,lastError; int32_t autostartRc; uint32_t loadFailCount; uint64_t module,proc,runtime; uint32_t autostartRetryCount,flags; };
#pragma pack(pop)
static_assert(sizeof(Rec)==64,"record size");
static std::vector<unsigned char> read_all(const wchar_t* path){ std::ifstream f(path,std::ios::binary); if(!f) return {}; return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),{}); }
static bool read_rec(const std::wstring& p, Rec& r){ std::ifstream f(p,std::ios::binary); if(!f) return false; f.read(reinterpret_cast<char*>(&r),sizeof(r)); return f.gcount()==sizeof(r); }
static std::wstring dirname_of(const std::wstring& p){ size_t q=p.find_last_of(L"\\/"); return q==std::wstring::npos?L".":p.substr(0,q); }
static std::wstring join(const std::wstring& a,const wchar_t* b){ return a+L"\\"+b; }
typedef void (*LoaderFn)();
static bool install_iat(unsigned char* base){ auto put=[&](size_t off, void* p){ *reinterpret_cast<void**>(base+off)=p; }; put(0x49E70,(void*)&CloseHandle); put(0x49E88,(void*)&CreateFileW); put(0x49EA0,(void*)&CreateThread); put(0x49ED0,(void*)&GetLastError); put(0x49EE0,(void*)&FakeGetModuleFileNameW); put(0x49F00,(void*)&GetProcAddress); put(0x49F10,(void*)&LoadLibraryW); put(0x49F50,(void*)&Sleep); put(0x49F80,(void*)&WriteFile); return true; }
static bool run_scenario(const std::vector<unsigned char>& loader,const std::wstring& dir,bool delayed,bool expectRetries){
    const size_t imageSize=0x03501000; unsigned char* base=(unsigned char*)VirtualAlloc(nullptr,imageSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE); if(!base){ std::printf("FAIL VirtualAlloc %lu\n",GetLastError()); return false; }
    install_iat(base); memcpy(base+0x035006C0,loader.data(),loader.size()); g_runtimePath=join(dir,L"fake_runtime.dll");
    std::wstring state=join(dir,L"ptar_rc41_bootstrap.bin"); DeleteFileW(state.c_str()); std::wstring live=join(dir,L"ptar_rc41.dll"); std::wstring delayedDll=join(dir,L"ptar_rc41_delayed.dll");
    if(delayed){ DeleteFileW(live.c_str()); auto* paths=new std::pair<std::wstring,std::wstring>(delayedDll,live); HANDLE th=CreateThread(nullptr,0,[](LPVOID p)->DWORD{ auto* pair=reinterpret_cast<std::pair<std::wstring,std::wstring>*>(p); Sleep(800); CopyFileW(pair->first.c_str(),pair->second.c_str(),FALSE); delete pair; return 0; },paths,0,nullptr); if(!th){ delete paths; VirtualFree(base,0,MEM_RELEASE); return false; } CloseHandle(th); }
    else if(GetFileAttributesW(live.c_str())==INVALID_FILE_ATTRIBUTES && !CopyFileW(delayedDll.c_str(),live.c_str(),FALSE)){ VirtualFree(base,0,MEM_RELEASE); return false; }
    ULONGLONG t0=GetTickCount64(); reinterpret_cast<LoaderFn>(base+0x035006C0)(); ULONGLONG callMs=GetTickCount64()-t0; if(callMs>500){ VirtualFree(base,0,MEM_RELEASE); return false; }
    Rec r{}; bool ok=false; for(int i=0;i<80;i++){ if(read_rec(state,r)&&memcmp(r.magic,"RC41BST1",8)==0&&r.stage==7){ok=true;break;} Sleep(200); }
    if(!ok){ if(read_rec(state,r)) std::printf("FAIL final stage=%u attempt=%u err=%u rc=%d loadFails=%u autoRetries=%u\n",r.stage,r.attempt,r.lastError,r.autostartRc,r.loadFailCount,r.autostartRetryCount); else std::printf("FAIL no state file\n"); VirtualFree(base,0,MEM_RELEASE); return false; }
    bool good=r.version==1 && r.autostartRc==0 && r.module && r.proc && r.runtime==(uint64_t)(uintptr_t)base; if(expectRetries) good=good&&r.loadFailCount>=1&&r.autostartRetryCount>=2; else good=good&&r.loadFailCount==0;
    std::printf("SCENARIO %s stage=%u attempt=%u err=%u rc=%d loadFails=%u autoRetries=%u callMs=%llu runtimeMatch=%d\n",delayed?"DELAYED":"IMMEDIATE",r.stage,r.attempt,r.lastError,r.autostartRc,r.loadFailCount,r.autostartRetryCount,callMs,r.runtime==(uint64_t)(uintptr_t)base);
    Sleep(100); VirtualFree(base,0,MEM_RELEASE); return good;
}
int wmain(int argc,wchar_t** argv){ if(argc<2) return 2; wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH); std::wstring dir=dirname_of(exe); auto loader=read_all(argv[1]); if(loader.empty()) return 3; wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp); SetCurrentDirectoryW(temp); if(!run_scenario(loader,dir,true,true)) return 4; if(!run_scenario(loader,dir,false,false)) return 5; std::puts("RC41B_BOOTSTRAP_HOST=PASS absolute_path=PASS delayed_load_retry=PASS autostart_retry=PASS nonblocking=PASS binary_state=PASS"); return 0; }
