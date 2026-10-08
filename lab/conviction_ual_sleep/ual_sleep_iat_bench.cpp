#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

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

int main()
{
    std::printf("UAL_SLEEP_IAT_BENCH=1\n");
    std::printf("PID=%lu\n",(unsigned long)GetCurrentProcessId());

    // Force a genuine VERSION.dll import/use. The local UAL proxy must forward it.
    DWORD dummy=0;
    DWORD ver=GetFileVersionInfoSizeW(L"kernel32.dll",&dummy);
    std::printf("VERSION_API_SIZE=%lu GLE=%lu\n",(unsigned long)ver,(unsigned long)GetLastError());

    auto slot=(uintptr_t*)FindImportSlot("Sleep");
    if(!slot)
    {
        std::printf("SLEEP_IAT_SLOT=NOT_FOUND\n");
        return 10;
    }
    std::printf("SLEEP_IAT_SLOT=%p\n",(void*)slot);
    SlotProtect("INITIAL",slot);
    PtrInfo("SLEEP_PTR_INITIAL",(void*)*slot);

    Sleep(0);
    SlotProtect("AFTER1",slot);
    PtrInfo("SLEEP_PTR_AFTER1",(void*)*slot);

    Sleep(0);
    PtrInfo("SLEEP_PTR_AFTER2",(void*)*slot);

    const unsigned n1=1000;
    const unsigned n2=10000;
    double t1=BenchSleep(n1);
    double t2=BenchSleep(n2);
    std::printf("BENCH_SLEEP0_N=%u TOTAL_MS=%.3f US_PER_CALL=%.3f\n",n1,t1,t1*1000.0/n1);
    std::printf("BENCH_SLEEP0_N=%u TOTAL_MS=%.3f US_PER_CALL=%.3f\n",n2,t2,t2*1000.0/n2);

    std::printf("RESULT=PASS\n");
    return 0;
}
