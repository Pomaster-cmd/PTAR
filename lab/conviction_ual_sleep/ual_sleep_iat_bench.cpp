#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

static void* FindImportSlot(const char* wanted)
{
    auto base=(unsigned char*)GetModuleHandleW(nullptr);
    if(!base) return nullptr;
    auto dos=(IMAGE_DOS_HEADER*)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt=(IMAGE_NT_HEADERS32*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE) return nullptr;
    auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress) return nullptr;
    auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);
    for(;imp->Name;++imp)
    {
        auto oft=imp->OriginalFirstThunk ?
            (IMAGE_THUNK_DATA32*)(base+imp->OriginalFirstThunk) : nullptr;
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

static void PtrInfo(const char* label, void* p)
{
    MEMORY_BASIC_INFORMATION mbi{};
    wchar_t path[MAX_PATH]={0};
    uintptr_t off=0;
    if(p && VirtualQuery(p,&mbi,sizeof(mbi))==sizeof(mbi) && mbi.AllocationBase)
    {
        GetModuleFileNameW((HMODULE)mbi.AllocationBase,path,MAX_PATH);
        off=(uintptr_t)p-(uintptr_t)mbi.AllocationBase;
    }
    ::wprintf(L"%S=%p module=%ls offset=0x%08lX\n",label,p,path[0]?path:L"<none>",(unsigned long)off);
}

static void SlotProtect(const char* label, void* slot)
{
    MEMORY_BASIC_INFORMATION mbi{};
    if(slot && VirtualQuery(slot,&mbi,sizeof(mbi))==sizeof(mbi))
        std::printf("%s_SLOT=%p STATE=0x%08lX PROTECT=0x%08lX TYPE=0x%08lX\n",label,slot,(unsigned long)mbi.State,(unsigned long)mbi.Protect,(unsigned long)mbi.Type);
}

static double BenchSleep(unsigned n)
{
    LARGE_INTEGER f{},a{},b{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    for(unsigned i=0;i<n;++i) Sleep(0);
    QueryPerformanceCounter(&b);
    return (double)(b.QuadPart-a.QuadPart)*1000.0/(double)f.QuadPart;
}

struct LimiterStats
{
    double minMs=0.0;
    double medianMs=0.0;
    double p95Ms=0.0;
    double maxMs=0.0;
    double meanMs=0.0;
    double meanOvershootMs=0.0;
    double meanSleepCalls=0.0;
    double cpuMs=0.0;
};

static unsigned long long FileTimeToU64(const FILETIME& ft)
{
    ULARGE_INTEGER u{};
    u.LowPart=ft.dwLowDateTime;
    u.HighPart=ft.dwHighDateTime;
    return u.QuadPart;
}

static LimiterStats BenchConvictionStyleLimiter(double targetMs,unsigned frames)
{
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    if(freq.QuadPart<=0) freq.QuadPart=1;

    FILETIME c0{},e0{},k0{},u0{},c1{},e1{},k1{},u1{};
    GetThreadTimes(GetCurrentThread(),&c0,&e0,&k0,&u0);

    std::vector<double> elapsed;
    elapsed.reserve(frames);
    unsigned long long totalSleeps=0;
    double sum=0.0;

    for(unsigned frame=0;frame<frames;++frame)
    {
        LARGE_INTEGER start{},now{};
        QueryPerformanceCounter(&start);
        unsigned long sleeps=0;
        for(;;)
        {
            QueryPerformanceCounter(&now);
            const double ms=(double)(now.QuadPart-start.QuadPart)*1000.0/(double)freq.QuadPart;
            if(ms>=targetMs)
            {
                elapsed.push_back(ms);
                sum+=ms;
                break;
            }
            Sleep(0);
            ++sleeps;
        }
        totalSleeps+=sleeps;
    }

    GetThreadTimes(GetCurrentThread(),&c1,&e1,&k1,&u1);
    std::sort(elapsed.begin(),elapsed.end());

    LimiterStats s{};
    if(!elapsed.empty())
    {
        s.minMs=elapsed.front();
        s.maxMs=elapsed.back();
        s.medianMs=elapsed[elapsed.size()/2];
        size_t p95=(elapsed.size()*95u)/100u;
        if(p95>=elapsed.size()) p95=elapsed.size()-1u;
        s.p95Ms=elapsed[p95];
        s.meanMs=sum/(double)elapsed.size();
        s.meanOvershootMs=s.meanMs-targetMs;
        s.meanSleepCalls=(double)totalSleeps/(double)elapsed.size();
    }
    const unsigned long long cpu100ns=
        (FileTimeToU64(k1)+FileTimeToU64(u1))-
        (FileTimeToU64(k0)+FileTimeToU64(u0));
    s.cpuMs=(double)cpu100ns/10000.0;
    return s;
}

int main()
{
    std::printf("UAL_SLEEP_IAT_BENCH=2\n");
    std::printf("PID=%lu\n",(unsigned long)GetCurrentProcessId());

    auto slot=(uintptr_t*)FindImportSlot("Sleep");
    if(!slot)
    {
        std::printf("SLEEP_IAT_SLOT=NOT_FOUND\n");
        return 10;
    }
    std::printf("SLEEP_IAT_SLOT=%p\n",(void*)slot);
    SlotProtect("PRE_VERSION_API",slot);
    PtrInfo("SLEEP_PTR_PRE_VERSION_API",(void*)*slot);

    // A real VERSION.dll call exercises UAL's forwarding path and, on fixed
    // releases, gives the loader a chance to restore the executable IAT.
    DWORD dummy=0;
    DWORD ver=GetFileVersionInfoSizeW(L"kernel32.dll",&dummy);
    std::printf("VERSION_API_SIZE=%lu GLE=%lu\n",(unsigned long)ver,(unsigned long)GetLastError());

    SlotProtect("POST_VERSION_API",slot);
    PtrInfo("SLEEP_PTR_POST_VERSION_API",(void*)*slot);

    Sleep(0);
    PtrInfo("SLEEP_PTR_AFTER_SLEEP1",(void*)*slot);
    Sleep(0);
    PtrInfo("SLEEP_PTR_AFTER_SLEEP2",(void*)*slot);

    const unsigned n1=1000;
    const unsigned n2=10000;
    double t1=BenchSleep(n1);
    double t2=BenchSleep(n2);
    std::printf("BENCH_SLEEP0_N=%u TOTAL_MS=%.3f US_PER_CALL=%.3f\n",n1,t1,t1*1000.0/n1);
    std::printf("BENCH_SLEEP0_N=%u TOTAL_MS=%.3f US_PER_CALL=%.3f\n",n2,t2,t2*1000.0/n2);

    const double target=1000.0/120.0;
    const unsigned frames=240;
    LimiterStats ls=BenchConvictionStyleLimiter(target,frames);
    std::printf(
        "LIMITER_TARGET_MS=%.6f FRAMES=%u MIN_MS=%.6f MEDIAN_MS=%.6f P95_MS=%.6f MAX_MS=%.6f MEAN_MS=%.6f MEAN_OVERSHOOT_MS=%.6f MEAN_SLEEP0_CALLS=%.3f CPU_MS=%.3f\n",
        target,frames,ls.minMs,ls.medianMs,ls.p95Ms,ls.maxMs,ls.meanMs,
        ls.meanOvershootMs,ls.meanSleepCalls,ls.cpuMs);

    std::printf("RESULT=PASS\n");
    return 0;
}
