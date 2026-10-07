#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <algorithm>

struct ModuleInfoLite {
    uintptr_t base;
    DWORD size;
    std::wstring name;
    std::wstring path;
};

struct ThreadCpuSample {
    DWORD tid;
    unsigned long long delta100ns;
};

struct BucketKey {
    std::wstring module;
    DWORD offsetBucket;
    bool operator<(const BucketKey& o) const {
        if (module != o.module) return module < o.module;
        return offsetBucket < o.offsetBucket;
    }
};

static unsigned long long FileTimeU64(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

static bool GetThreadCpu100ns(DWORD tid, unsigned long long& out) {
    HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!h) return false;
    FILETIME ct{}, et{}, kt{}, ut{};
    const BOOL ok = GetThreadTimes(h, &ct, &et, &kt, &ut);
    CloseHandle(h);
    if (!ok) return false;
    out = FileTimeU64(kt) + FileTimeU64(ut);
    return true;
}

static std::vector<DWORD> EnumerateThreads(DWORD pid) {
    std::vector<DWORD> tids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return tids;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) tids.push_back(te.th32ThreadID);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return tids;
}

static bool QueryCreationTime(DWORD pid, unsigned long long& creation) {
    HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!p) return false;
    FILETIME ct{}, et{}, kt{}, ut{};
    const BOOL ok = GetProcessTimes(p, &ct, &et, &kt, &ut);
    CloseHandle(p);
    if (!ok) return false;
    creation = FileTimeU64(ct);
    return true;
}

static DWORD FindNewestProcessByName(const wchar_t* exeName) {
    DWORD bestPid = 0;
    unsigned long long bestCreation = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                unsigned long long ct = 0;
                if (QueryCreationTime(pe.th32ProcessID, ct) && ct >= bestCreation) {
                    bestCreation = ct;
                    bestPid = pe.th32ProcessID;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return bestPid;
}

static std::vector<ModuleInfoLite> EnumerateModules(DWORD pid) {
    std::vector<ModuleInfoLite> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return out;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            ModuleInfoLite m{};
            m.base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
            m.size = me.modBaseSize;
            m.name = me.szModule;
            m.path = me.szExePath;
            out.push_back(m);
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    std::sort(out.begin(), out.end(), [](const ModuleInfoLite& a, const ModuleInfoLite& b) {
        return a.base < b.base;
    });
    return out;
}

static const ModuleInfoLite* FindModule(const std::vector<ModuleInfoLite>& mods, uintptr_t ip) {
    for (const auto& m : mods) {
        const uintptr_t end = m.base + static_cast<uintptr_t>(m.size);
        if (ip >= m.base && ip < end) return &m;
    }
    return nullptr;
}

static bool SelectHottestThread(DWORD pid, DWORD windowMs, DWORD& selected, std::vector<ThreadCpuSample>& deltas) {
    const auto tids = EnumerateThreads(pid);
    if (tids.empty()) return false;
    std::map<DWORD, unsigned long long> before;
    for (DWORD candidate : tids) {
        unsigned long long v = 0;
        if (GetThreadCpu100ns(candidate, v)) before[candidate] = v;
    }
    Sleep(windowMs);
    selected = 0;
    unsigned long long best = 0;
    for (const auto& it : before) {
        unsigned long long after = 0;
        if (!GetThreadCpu100ns(it.first, after) || after < it.second) continue;
        const unsigned long long d = after - it.second;
        deltas.push_back({it.first, d});
        if (d > best) {
            best = d;
            selected = it.first;
        }
    }
    std::sort(deltas.begin(), deltas.end(), [](const ThreadCpuSample& a, const ThreadCpuSample& b) {
        return a.delta100ns > b.delta100ns;
    });
    return selected != 0;
}

static std::wstring TimestampFolder() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[96]{};
    swprintf_s(buf, L"PTAR_CONVICTION_HOTSPOT_%04u%02u%02u_%02u%02u%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static void PrintUsage() {
    std::wprintf(L"PTAR Conviction Hotspot Sampler\n"
                 L"Usage: PTAR_Conviction_Hotspot_Sampler.exe [--pid N] [--tid N] [--seconds N] [--interval-ms N]\n"
                 L"Defaults: newest Conviction_game.exe, hottest thread auto-detected, 12 seconds, 4 ms.\n");
}

static bool ParseUInt(const wchar_t* s, unsigned long& v) {
    if (!s || !*s) return false;
    wchar_t* end = nullptr;
    const unsigned long x = wcstoul(s, &end, 10);
    if (!end || *end != L'\0') return false;
    v = x;
    return true;
}

static int SelfTest() {
    std::vector<ModuleInfoLite> mods;
    mods.push_back({0x00400000u, 0x00100000u, L"Conviction_game.exe", L"X"});
    mods.push_back({0x10000000u, 0x00010000u, L"Lead3DEngine.dll", L"Y"});
    const auto* a = FindModule(mods, 0x0042D16Cu);
    const auto* b = FindModule(mods, 0x10001234u);
    const auto* c = FindModule(mods, 0x20000000u);
    if (!a || a->name != L"Conviction_game.exe") return 10;
    if (!b || b->name != L"Lead3DEngine.dll") return 11;
    if (c) return 12;
    const DWORD bucket = static_cast<DWORD>((0x0042D16Cu - 0x00400000u) & ~static_cast<uintptr_t>(0xFFu));
    if (bucket != 0x0002D100u) return 13;
    std::puts("SELFTEST=PASS");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    DWORD pid = 0;
    DWORD tid = 0;
    unsigned long seconds = 12;
    unsigned long intervalMs = 4;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"-h") == 0) {
            PrintUsage();
            return 0;
        }
        if (_wcsicmp(argv[i], L"--selftest") == 0) {
            selftest = true;
            continue;
        }
        if (i + 1 >= argc) {
            PrintUsage();
            return 2;
        }
        unsigned long v = 0;
        if (!ParseUInt(argv[i + 1], v)) {
            std::fwprintf(stderr, L"Invalid numeric value: %ls\n", argv[i + 1]);
            return 2;
        }
        if (_wcsicmp(argv[i], L"--pid") == 0) pid = static_cast<DWORD>(v);
        else if (_wcsicmp(argv[i], L"--tid") == 0) tid = static_cast<DWORD>(v);
        else if (_wcsicmp(argv[i], L"--seconds") == 0) seconds = v;
        else if (_wcsicmp(argv[i], L"--interval-ms") == 0) intervalMs = v;
        else {
            std::fwprintf(stderr, L"Unknown option: %ls\n", argv[i]);
            return 2;
        }
        ++i;
    }

    if (selftest) return SelfTest();
    if (seconds < 2 || seconds > 120 || intervalMs < 2 || intervalMs > 100) {
        std::fwprintf(stderr, L"Safety limits: seconds 2..120, interval-ms 2..100.\n");
        return 2;
    }

    if (!pid) pid = FindNewestProcessByName(L"Conviction_game.exe");
    if (!pid) {
        std::fwprintf(stderr, L"Conviction_game.exe not found. Start the game first.\n");
        return 3;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        std::fwprintf(stderr, L"OpenProcess failed pid=%lu gle=%lu\n", pid, GetLastError());
        return 4;
    }

    const auto modules = EnumerateModules(pid);
    if (modules.empty()) {
        std::fwprintf(stderr, L"Module enumeration failed pid=%lu gle=%lu\n", pid, GetLastError());
        CloseHandle(process);
        return 5;
    }

    std::vector<ThreadCpuSample> cpuDeltas;
    if (!tid) {
        if (!SelectHottestThread(pid, 750, tid, cpuDeltas)) {
            std::fwprintf(stderr, L"Unable to auto-select hottest thread.\n");
            CloseHandle(process);
            return 6;
        }
    }

    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!thread) {
        std::fwprintf(stderr, L"OpenThread failed tid=%lu gle=%lu\n", tid, GetLastError());
        CloseHandle(process);
        return 7;
    }

    const std::wstring folder = TimestampFolder();
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        std::fwprintf(stderr, L"CreateDirectory failed gle=%lu\n", GetLastError());
        CloseHandle(thread);
        CloseHandle(process);
        return 8;
    }

    const std::wstring rawPath = folder + L"\\samples.csv";
    const std::wstring summaryPath = folder + L"\\SUMMARY.txt";
    const std::wstring modulesPath = folder + L"\\modules.tsv";
    const std::wstring cpuPath = folder + L"\\thread_cpu_probe.tsv";

    FILE* raw = nullptr;
    FILE* summary = nullptr;
    FILE* mf = nullptr;
    FILE* cf = nullptr;
    _wfopen_s(&raw, rawPath.c_str(), L"wb");
    _wfopen_s(&summary, summaryPath.c_str(), L"wb");
    _wfopen_s(&mf, modulesPath.c_str(), L"wb");
    _wfopen_s(&cf, cpuPath.c_str(), L"wb");
    if (!raw || !summary || !mf || !cf) {
        std::fwprintf(stderr, L"Unable to create output files.\n");
        if (raw) fclose(raw);
        if (summary) fclose(summary);
        if (mf) fclose(mf);
        if (cf) fclose(cf);
        CloseHandle(thread);
        CloseHandle(process);
        return 9;
    }

    std::fwprintf(mf, L"module\tbase\tsize\tpath\n");
    for (const auto& m : modules)
        std::fwprintf(mf, L"%ls\t0x%08lX\t%lu\t%ls\n", m.name.c_str(), static_cast<unsigned long>(m.base), m.size, m.path.c_str());
    fclose(mf);

    std::fwprintf(cf, L"tid\tcpu_delta_ms_over_750ms\n");
    for (const auto& t : cpuDeltas)
        std::fwprintf(cf, L"%lu\t%.3f\n", t.tid, static_cast<double>(t.delta100ns) / 10000.0);
    fclose(cf);

    std::fprintf(raw, "sample,qpc_us,tid,eip,module,module_offset,bucket_0x100\r\n");

    LARGE_INTEGER freq{}, start{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    if (freq.QuadPart <= 0) freq.QuadPart = 1;

    std::map<BucketKey, unsigned long> buckets;
    std::map<std::wstring, unsigned long> moduleCounts;
    unsigned long sampleCount = 0;
    unsigned long suspendFailures = 0;
    unsigned long contextFailures = 0;

    std::wprintf(L"Sampling pid=%lu tid=%lu for %lu s at %lu ms -> %ls\n", pid, tid, seconds, intervalMs, folder.c_str());

    while (true) {
        Sleep(static_cast<DWORD>(intervalMs));
        QueryPerformanceCounter(&now);
        const double elapsed = static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
        if (elapsed >= static_cast<double>(seconds)) break;

        const DWORD suspendResult = SuspendThread(thread);
        if (suspendResult == 0xFFFFFFFFu) {
            ++suspendFailures;
            continue;
        }

        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL;
        const BOOL got = GetThreadContext(thread, &ctx);
        ResumeThread(thread);
        if (!got) {
            ++contextFailures;
            continue;
        }

        const uintptr_t eip = static_cast<uintptr_t>(ctx.Eip);
        const ModuleInfoLite* mod = FindModule(modules, eip);
        const std::wstring modName = mod ? mod->name : L"<unknown>";
        DWORD offset = 0;
        if (mod) offset = static_cast<DWORD>(eip - mod->base);
        const DWORD bucket = offset & ~0xFFu;
        ++buckets[{modName, bucket}];
        ++moduleCounts[modName];
        ++sampleCount;

        const long long qpcUs = static_cast<long long>((now.QuadPart - start.QuadPart) * 1000000LL / freq.QuadPart);
        std::fprintf(raw, "%lu,%lld,%lu,0x%08lX,\"%ls\",0x%08lX,0x%08lX\r\n",
            sampleCount, qpcUs, tid, static_cast<unsigned long>(eip), modName.c_str(), offset, bucket);
    }
    fclose(raw);

    std::vector<std::pair<BucketKey, unsigned long>> sortedBuckets(buckets.begin(), buckets.end());
    std::sort(sortedBuckets.begin(), sortedBuckets.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::pair<std::wstring, unsigned long>> sortedModules(moduleCounts.begin(), moduleCounts.end());
    std::sort(sortedModules.begin(), sortedModules.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    std::fprintf(summary, "PTAR_CONVICTION_HOTSPOT_SAMPLER=1\r\nPID=%lu\r\nTID=%lu\r\nSECONDS=%lu\r\nINTERVAL_MS=%lu\r\nSAMPLES=%lu\r\nSUSPEND_FAILURES=%lu\r\nCONTEXT_FAILURES=%lu\r\n",
        pid, tid, seconds, intervalMs, sampleCount, suspendFailures, contextFailures);
    std::fprintf(summary, "\r\nMODULE_DISTRIBUTION\r\n");
    for (const auto& it : sortedModules) {
        const double pct = sampleCount ? (100.0 * static_cast<double>(it.second) / static_cast<double>(sampleCount)) : 0.0;
        std::fprintf(summary, "%ls\t%lu\t%.2f%%\r\n", it.first.c_str(), it.second, pct);
    }
    std::fprintf(summary, "\r\nTOP_0x100_BUCKETS\r\n");
    const size_t topN = (std::min)(static_cast<size_t>(80), sortedBuckets.size());
    for (size_t i = 0; i < topN; ++i) {
        const auto& it = sortedBuckets[i];
        const double pct = sampleCount ? (100.0 * static_cast<double>(it.second) / static_cast<double>(sampleCount)) : 0.0;
        std::fprintf(summary, "%ls+0x%08lX\t%lu\t%.2f%%\r\n",
            it.first.module.c_str(), it.first.offsetBucket, it.second, pct);
    }
    fclose(summary);

    CloseHandle(thread);
    CloseHandle(process);

    std::wprintf(L"DONE samples=%lu suspend_fail=%lu context_fail=%lu\n", sampleCount, suspendFailures, contextFailures);
    std::wprintf(L"Send the entire folder %ls.\n", folder.c_str());
    return sampleCount ? 0 : 20;
}
