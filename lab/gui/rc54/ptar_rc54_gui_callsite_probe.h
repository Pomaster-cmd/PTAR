#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include "../../raster/rc41/ptar_rc41_contract.h"
#include "../../raster/rc41/ptar_rc41_swapchain_policy.h"

namespace ptar_rc54 {

enum class QueryKind : uint32_t {
    GetDesc = 1,
    GetDesc1 = 2
};

struct QueryObservation {
    QueryKind kind = QueryKind::GetDesc;
    ptar_rc41::CallerDomain domain = ptar_rc41::CallerDomain::Unknown;
    const void* returnAddress = nullptr;
    UINT physicalW = 0;
    UINT physicalH = 0;
    UINT reportedW = 0;
    UINT reportedH = 0;
    bool virtualized = false;
};

class GuiCallsiteProbe {
public:
    GuiCallsiteProbe() noexcept = default;
    GuiCallsiteProbe(const GuiCallsiteProbe&) = delete;
    GuiCallsiteProbe& operator=(const GuiCallsiteProbe&) = delete;

    bool configure(HMODULE gameModule, HMODULE sidecarModule, const ptar_rc41::Contract& contract) noexcept;
    void observe(const QueryObservation& observation, bool primaryBound) noexcept;

private:
    static constexpr size_t kMaxCallsites = 256;

    struct Entry {
        HMODULE module = nullptr;
        uintptr_t rva = 0;
        QueryKind kind = QueryKind::GetDesc;
        ptar_rc41::CallerDomain domain = ptar_rc41::CallerDomain::Unknown;
        bool primaryBound = false;
        UINT physicalW = 0;
        UINT physicalH = 0;
        UINT reportedW = 0;
        UINT reportedH = 0;
        bool virtualized = false;
        uint64_t hits = 0;
    };

    bool build_log_path(HMODULE sidecarModule) noexcept;
    void write_header_unlocked() noexcept;
    void write_observation_unlocked(const Entry& entry, DWORD threadId, uint64_t elapsedUs) noexcept;
    void write_line_unlocked(const char* line) noexcept;
    static bool should_sample(uint64_t hits) noexcept;
    static const char* kind_name(QueryKind kind) noexcept;
    static const char* domain_name(ptar_rc41::CallerDomain domain) noexcept;

    SRWLOCK lock_ = SRWLOCK_INIT;
    HMODULE gameModule_ = nullptr;
    ptar_rc41::Contract contract_{};
    wchar_t logPath_[MAX_PATH]{};
    LARGE_INTEGER qpcFrequency_{};
    LARGE_INTEGER qpcStart_{};
    Entry entries_[kMaxCallsites]{};
    size_t entryCount_ = 0;
    bool configured_ = false;
    bool tableFullLogged_ = false;
};

} // namespace ptar_rc54
