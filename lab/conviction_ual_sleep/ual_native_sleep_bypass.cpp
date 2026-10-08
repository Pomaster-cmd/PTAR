#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <atomic>

struct Range { uintptr_t base=0; DWORD size=0; };
static std::atomic<bool> g_stop{false};
static DWORD g_mainTid=0;
static Range g_version{};
static std::atomic<unsigned long long> g_samples{0};
static std::atomic<unsigned long long> g_versionSamples{0};

typedef VOID (WINAPI *PFN_NativeSleep)(DWORD);

static Range ModRange(const wchar_t* name)
{
    Range r{};
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

static bool In(const Range& r,uintptr_t p)
{
    return r.base && p>=r.base && p<r.base+r.size;
}

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
            if(std::strcmp((const char*)ibn->Name,wanted)==0)
                return &ft[i].u1.Function;
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
        if(SuspendThread(th)!=(DWORD)-1)
        {
            CONTEXT c{}; c.ContextFlags=CONTEXT_CONTROL;
            if(GetThreadContext(th,&c))
            {
                ++g_samples;
                if(In(g_version,(uintptr_t)c.Eip)) ++g_versionSamples;
            }
            ResumeThread(th);
        }
        ::Sleep(1);
    }
    CloseHandle(th);
    return 0;
}

static unsigned long long RunImportedSleep(double seconds)
{
    LARGE_INTEGER f{},a{},n{};
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    unsigned long long calls=0;
    do {
        ::Sleep(0);
        ++calls;
        QueryPerformanceCounter(&n);
    } while((double)(n.QuadPart-a.QuadPart)/(double)f.QuadPart < seconds);
    return calls;
}

static unsigned long long RunNativeSleep(PFN_NativeSleep fn,double seconds)
{
    LARGE_INTEGER f{},a{},n{};
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    unsigned long long calls=0;
    do {
        fn(0);
        ++calls;
        QueryPerformanceCounter(&n);
    } while((double)(n.QuadPart-a.QuadPart)/(double)f.QuadPart < seconds);
    return calls;
}

static bool SamplePhase(bool native,PFN_NativeSleep fn,const char* label)
{
    g_samples.store(0,std::memory_order_relaxed);
    g_versionSamples.store(0,std::memory_order_relaxed);
    g_stop.store(false,std::memory_order_relaxed);
    HANDLE s=CreateThread(nullptr,0,Sampler,nullptr,0,nullptr);
    if(!s) return false;
    unsigned long long calls=native ? RunNativeSleep(fn,3.0) : RunImportedSleep(3.0);
    g_stop.store(true,std::memory_order_relaxed);
    WaitForSingleObject(s,5000); CloseHandle(s);
    unsigned long long samples=g_samples.load();
    unsigned long long version=g_versionSamples.load();
    std::printf("%s_CALLS=%llu\n",label,calls);
    std::printf("%s_SAMPLES=%llu\n",label,samples);
    std::printf("%s_VERSION_SAMPLES=%llu\n",label,version);
    std::printf("%s_VERSION_PCT=%.3f\n",label,samples?100.0*(double)version/(double)samples:0.0);
    return true;
}

int main()
{
    std::printf("UAL_NATIVE_SLEEP_BYPASS=1\n");
    g_mainTid=GetCurrentThreadId();

    // Force the local VERSION proxy to initialise and trigger UAL's IAT logic.
    DWORD dummy=0;
    DWORD ver=GetFileVersionInfoSizeW(L"kernel32.dll",&dummy);
    std::printf("VERSION_API_SIZE=%lu GLE=%lu\n",(unsigned long)ver,(unsigned long)GetLastError());

    g_version=ModRange(L"version.dll");
    std::printf("VERSION_BASE=%p SIZE=0x%08lX\n",(void*)g_version.base,(unsigned long)g_version.size);

    auto slot=(uintptr_t*)FindImportSlot("Sleep");
    if(!slot){std::printf("RESULT=FAIL_NO_SLEEP_IAT\n"); return 20;}
    uintptr_t imported=*slot;
    std::printf("IMPORTED_SLEEP=%p",(void*)imported);
    if(In(g_version,imported))
        std::printf(" VERSION_RVA=0x%08lX",(unsigned long)(imported-g_version.base));
    std::printf("\n");

    HMODULE k32=GetModuleHandleW(L"kernel32.dll");
    auto native=(PFN_NativeSleep)(k32?GetProcAddress(k32,"Sleep"):nullptr);
    if(!native){std::printf("RESULT=FAIL_NATIVE_RESOLVE\n"); return 21;}
    std::printf("NATIVE_SLEEP=%p IN_VERSION=%d\n",(void*)native,In(g_version,(uintptr_t)native)?1:0);

    if(!SamplePhase(false,native,"IMPORTED")){std::printf("RESULT=FAIL_IMPORTED_SAMPLE\n"); return 22;}
    if(!SamplePhase(true,native,"NATIVE")){std::printf("RESULT=FAIL_NATIVE_SAMPLE\n"); return 23;}

    std::printf("SLEEP_IAT_FINAL=%p",(void*)*slot);
    if(In(g_version,*slot))
        std::printf(" VERSION_RVA=0x%08lX",(unsigned long)(*slot-g_version.base));
    std::printf("\n");

    if(!In(g_version,imported)){std::printf("RESULT=FAIL_IMPORTED_NOT_HOOKED\n"); return 30;}
    if((unsigned long)(imported-g_version.base)!=0x00038FA0UL){std::printf("RESULT=FAIL_WRONG_UAL_RVA\n"); return 31;}
    if(In(g_version,(uintptr_t)native)){std::printf("RESULT=FAIL_NATIVE_STILL_VERSION\n"); return 32;}
    if(g_versionSamples.load()!=0){std::printf("RESULT=FAIL_NATIVE_ENTERED_VERSION\n"); return 33;}

    std::printf("RESULT=PASS\n");
    return 0;
}
