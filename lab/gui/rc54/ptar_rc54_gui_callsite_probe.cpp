#include "ptar_rc54_gui_callsite_probe.h"
#include <cstdio>
#include <cstring>

namespace ptar_rc54 {

namespace {
GuiCallsiteProbe g_probe;

static HMODULE allocation_base(const void* p) noexcept {
    if(!p) return nullptr;
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(p,&mbi,sizeof(mbi))) return nullptr;
    return static_cast<HMODULE>(mbi.AllocationBase);
}
}

GuiCallsiteProbe& global_gui_callsite_probe() noexcept { return g_probe; }

bool GuiCallsiteProbe::configure(HMODULE gameModule, HMODULE sidecarModule, const ptar_rc41::Contract& contract) noexcept {
    if(configured_ || !gameModule || !sidecarModule || !contract.valid()) return false;
    gameModule_ = gameModule;
    contract_ = contract;
    if(!build_log_path(sidecarModule)) return false;
    if(!QueryPerformanceFrequency(&qpcFrequency_) || !QueryPerformanceCounter(&qpcStart_)) return false;
    configured_ = true;
    AcquireSRWLockExclusive(&lock_);
    write_header_unlocked();
    ReleaseSRWLockExclusive(&lock_);
    return true;
}

bool GuiCallsiteProbe::build_log_path(HMODULE sidecarModule) noexcept {
    wchar_t path[MAX_PATH]{};
    const DWORD n=GetModuleFileNameW(sidecarModule,path,MAX_PATH);
    if(!n || n>=MAX_PATH) return false;
    wchar_t* slash=wcsrchr(path,L'\\');
    if(!slash) return false;
    const wchar_t name[]=L"ptar_rc54_gui_callsites.log";
    const size_t remain=MAX_PATH-size_t(slash+1-path);
    if(wcslen(name)+1>remain) return false;
    wcscpy_s(slash+1,remain,name);
    wcscpy_s(logPath_,path);
    return true;
}

const char* GuiCallsiteProbe::kind_name(QueryKind kind) noexcept {
    return kind==QueryKind::GetDesc1 ? "GetDesc1" : "GetDesc";
}

const char* GuiCallsiteProbe::domain_name(ptar_rc41::CallerDomain domain) noexcept {
    switch(domain){
        case ptar_rc41::CallerDomain::Game:return "Game";
        case ptar_rc41::CallerDomain::Runtime:return "Runtime";
        case ptar_rc41::CallerDomain::Sidecar:return "Sidecar";
        default:return "Unknown";
    }
}

bool GuiCallsiteProbe::should_sample(uint64_t hits) noexcept {
    if(hits<=8) return true;
    return (hits & (hits-1))==0; // 16,32,64,... bounds logging overhead.
}

void GuiCallsiteProbe::write_line_unlocked(const char* line) noexcept {
    if(!line || !*line || !logPath_[0]) return;
    HANDLE h=CreateFileW(logPath_,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;
    DWORD wr=0;
    const DWORD len=(DWORD)strlen(line);
    WriteFile(h,line,len,&wr,nullptr);
    CloseHandle(h);
}

void GuiCallsiteProbe::write_header_unlocked() noexcept {
    char line[512]{};
    std::snprintf(line,sizeof(line),
        "RC54_GUI_CALLSITE_PROBE=ACTIVE game=%p logical=%ux%u physical=%ux%u policy=observe_only_no_behavior_change\r\n",
        static_cast<void*>(gameModule_),contract_.logical.w,contract_.logical.h,contract_.physical.w,contract_.physical.h);
    write_line_unlocked(line);
}

void GuiCallsiteProbe::write_observation_unlocked(const Entry& entry,DWORD threadId,uint64_t elapsedUs) noexcept {
    char line[768]{};
    std::snprintf(line,sizeof(line),
        "t_us=%llu tid=%lu kind=%s domain=%s module=%p rva=0x%llX physical=%ux%u reported=%ux%u virtualized=%u hits=%llu\r\n",
        static_cast<unsigned long long>(elapsedUs),
        static_cast<unsigned long>(threadId),kind_name(entry.kind),domain_name(entry.domain),
        static_cast<void*>(entry.module),static_cast<unsigned long long>(entry.rva),
        entry.physicalW,entry.physicalH,entry.reportedW,entry.reportedH,entry.virtualized?1u:0u,
        static_cast<unsigned long long>(entry.hits));
    write_line_unlocked(line);
}

void GuiCallsiteProbe::observe(const QueryObservation& observation) noexcept {
    if(!configured_ || !observation.returnAddress) return;
    const HMODULE module=allocation_base(observation.returnAddress);
    if(!module) return;
    const uintptr_t base=reinterpret_cast<uintptr_t>(module);
    const uintptr_t addr=reinterpret_cast<uintptr_t>(observation.returnAddress);
    if(addr<base) return;
    const uintptr_t rva=addr-base;

    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    const uint64_t elapsedUs=qpcFrequency_.QuadPart>0
        ? uint64_t((now.QuadPart-qpcStart_.QuadPart)*1000000LL/qpcFrequency_.QuadPart) : 0;

    AcquireSRWLockExclusive(&lock_);
    Entry* found=nullptr;
    for(size_t i=0;i<entryCount_;++i){
        Entry& e=entries_[i];
        if(e.module==module && e.rva==rva && e.kind==observation.kind){found=&e;break;}
    }
    if(!found){
        if(entryCount_>=kMaxCallsites){
            if(!tableFullLogged_){
                write_line_unlocked("RC54_GUI_CALLSITE_TABLE_FULL=1\r\n");
                tableFullLogged_=true;
            }
            ReleaseSRWLockExclusive(&lock_);
            return;
        }
        found=&entries_[entryCount_++];
        found->module=module;
        found->rva=rva;
        found->kind=observation.kind;
    }
    found->domain=observation.domain;
    found->physicalW=observation.physicalW;
    found->physicalH=observation.physicalH;
    found->reportedW=observation.reportedW;
    found->reportedH=observation.reportedH;
    found->virtualized=observation.virtualized;
    ++found->hits;
    if(should_sample(found->hits)) write_observation_unlocked(*found,GetCurrentThreadId(),elapsedUs);
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace ptar_rc54
