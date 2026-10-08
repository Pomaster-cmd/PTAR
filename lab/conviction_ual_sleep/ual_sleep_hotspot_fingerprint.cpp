#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <map>
#include <vector>
#include <algorithm>

struct Range { uintptr_t base=0; DWORD size=0; const char* name="OTHER"; };
static std::atomic<bool> g_stop{false};
static DWORD g_mainTid=0;
static Range g_version{}, g_ntdll{}, g_kernel32{};
static std::map<uint32_t,uint64_t> g_verExact, g_ntExact, g_k32Exact, g_otherExact;
static uint64_t g_samples=0, g_suspendFail=0, g_contextFail=0;

static Range ModRange(const wchar_t* name,const char* label)
{
    Range r{}; r.name=label;
    HMODULE h=GetModuleHandleW(name);
    if(!h) return r;
    auto base=(unsigned char*)h;
    auto dos=(IMAGE_DOS_HEADER*)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return r;
    auto nt=(IMAGE_NT_HEADERS32*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE) return r;
    r.base=(uintptr_t)base;
    r.size=nt->OptionalHeader.SizeOfImage;
    return r;
}

static bool In(const Range& r,uintptr_t p){ return r.base && p>=r.base && p<r.base+r.size; }

static void* FindImportSlot(const char* wanted)
{
    auto base=(unsigned char*)GetModuleHandleW(nullptr);
    if(!base) return nullptr;
    auto dos=(IMAGE_DOS_HEADER*)base;
    auto nt=(IMAGE_NT_HEADERS32*)(base+dos->e_lfanew);
    auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress) return nullptr;
    auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);
    for(;imp->Name;++imp)
    {
        auto oft=imp->OriginalFirstThunk ? (IMAGE_THUNK_DATA32*)(base+imp->OriginalFirstThunk) : nullptr;
        auto ft=(IMAGE_THUNK_DATA32*)(base+imp->FirstThunk);
        if(!oft) continue;
        for(size_t i=0;oft[i].u1.AddressOfData;++i)
        {
            if(IMAGE_SNAP_BY_ORDINAL32(oft[i].u1.Ordinal)) continue;
            auto ibn=(IMAGE_IMPORT_BY_NAME*)(base+oft[i].u1.AddressOfData);
            if(std::strcmp((const char*)ibn->Name,wanted)==0) return &ft[i].u1.Function;
        }
    }
    return nullptr;
}

static DWORD WINAPI Sampler(void*)
{
    HANDLE th=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,g_mainTid);
    if(!th) return 10;
    while(!g_stop.load(std::memory_order_relaxed))
    {
        if(SuspendThread(th)==(DWORD)-1){++g_suspendFail; SwitchToThread(); continue;}
        CONTEXT c{}; c.ContextFlags=CONTEXT_CONTROL;
        if(GetThreadContext(th,&c))
        {
            uintptr_t eip=(uintptr_t)c.Eip;
            if(In(g_version,eip)) ++g_verExact[(uint32_t)(eip-g_version.base)];
            else if(In(g_ntdll,eip)) ++g_ntExact[(uint32_t)(eip-g_ntdll.base)];
            else if(In(g_kernel32,eip)) ++g_k32Exact[(uint32_t)(eip-g_kernel32.base)];
            else ++g_otherExact[(uint32_t)eip];
            ++g_samples;
        }
        else ++g_contextFail;
        ResumeThread(th);
        // Similar order of sampling frequency to the field sampler while
        // avoiding a second hot spin that would starve the worker.
        ::Sleep(1);
    }
    CloseHandle(th);
    return 0;
}

static void DumpTop(const char* label,const std::map<uint32_t,uint64_t>& m,unsigned maxn)
{
    std::vector<std::pair<uint32_t,uint64_t>> v(m.begin(),m.end());
    std::sort(v.begin(),v.end(),[](auto&a,auto&b){return a.second>b.second;});
    uint64_t total=0; for(auto&x:v) total+=x.second;
    std::printf("%s_TOTAL=%llu\n",label,(unsigned long long)total);
    for(unsigned i=0;i<v.size() && i<maxn;++i)
        std::printf("%s_%02u_RVA=0x%08X COUNT=%llu PCT=%.3f\n",label,i+1,v[i].first,(unsigned long long)v[i].second,total?100.0*(double)v[i].second/(double)total:0.0);
}

int main()
{
    std::printf("UAL_SLEEP_HOTSPOT_FINGERPRINT=1\n");
    g_mainTid=GetCurrentThreadId();

    // Force local VERSION proxy initialization before recording.
    DWORD dummy=0;
    DWORD ver=GetFileVersionInfoSizeW(L"kernel32.dll",&dummy);
    std::printf("VERSION_API_SIZE=%lu GLE=%lu\n",(unsigned long)ver,(unsigned long)GetLastError());

    g_version=ModRange(L"version.dll","VERSION");
    g_ntdll=ModRange(L"ntdll.dll","NTDLL");
    g_kernel32=ModRange(L"kernel32.dll","KERNEL32");
    std::printf("VERSION_BASE=0x%08lX SIZE=0x%08lX\n",(unsigned long)g_version.base,(unsigned long)g_version.size);
    std::printf("NTDLL_BASE=0x%08lX SIZE=0x%08lX\n",(unsigned long)g_ntdll.base,(unsigned long)g_ntdll.size);

    auto slot=(uintptr_t*)FindImportSlot("Sleep");
    if(!slot){std::printf("SLEEP_IAT_SLOT=NOT_FOUND\n"); return 20;}
    std::printf("SLEEP_IAT_SLOT=%p VALUE=%p",(void*)slot,(void*)*slot);
    if(In(g_version,*slot)) std::printf(" VERSION_RVA=0x%08lX",(unsigned long)(*slot-g_version.base));
    std::printf("\n");

    HANDLE s=CreateThread(nullptr,0,Sampler,nullptr,0,nullptr);
    if(!s){std::printf("SAMPLER_CREATE_FAIL=%lu\n",GetLastError()); return 21;}

    LARGE_INTEGER f{},a{},n{}; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    uint64_t calls=0;
    do {
        ::Sleep(0);
        ++calls;
        QueryPerformanceCounter(&n);
    } while((double)(n.QuadPart-a.QuadPart)/(double)f.QuadPart < 5.0);

    g_stop.store(true,std::memory_order_relaxed);
    WaitForSingleObject(s,5000); CloseHandle(s);
    std::printf("WORKER_SLEEP0_CALLS=%llu\n",(unsigned long long)calls);
    std::printf("SAMPLES=%llu SUSPEND_FAIL=%llu CONTEXT_FAIL=%llu\n",(unsigned long long)g_samples,(unsigned long long)g_suspendFail,(unsigned long long)g_contextFail);
    std::printf("SLEEP_IAT_FINAL=%p",(void*)*slot);
    if(In(g_version,*slot)) std::printf(" VERSION_RVA=0x%08lX",(unsigned long)(*slot-g_version.base));
    std::printf("\n");
    DumpTop("VERSION",g_verExact,40);
    DumpTop("NTDLL",g_ntExact,40);
    DumpTop("KERNEL32",g_k32Exact,20);
    DumpTop("OTHER",g_otherExact,20);
    std::printf("RESULT=PASS\n");
    return 0;
}
