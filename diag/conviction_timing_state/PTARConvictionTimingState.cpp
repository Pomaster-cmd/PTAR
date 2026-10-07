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
#include <cmath>
#include <cstring>

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

static const ModuleInfoLite* FindModuleByName(const std::vector<ModuleInfoLite>& mods, const wchar_t* name) {
    for (const auto& m : mods) {
        if (_wcsicmp(m.name.c_str(), name) == 0) return &m;
    }
    return nullptr;
}

static const ModuleInfoLite* FindModuleByAddress(const std::vector<ModuleInfoLite>& mods, uintptr_t p) {
    for (const auto& m : mods) {
        const uintptr_t end = m.base + static_cast<uintptr_t>(m.size);
        if (p >= m.base && p < end) return &m;
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

template <typename T>
static bool ReadRemote(HANDLE process, uintptr_t address, T& out) {
    SIZE_T got = 0;
    const BOOL ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), &out, sizeof(T), &got);
    return ok && got == sizeof(T);
}

static bool ReadRemoteBytes(HANDLE process, uintptr_t address, void* out, SIZE_T bytes) {
    SIZE_T got = 0;
    const BOOL ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), out, bytes, &got);
    return ok && got == bytes;
}

static std::wstring TimestampName() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[128]{};
    swprintf_s(buf, L"PTAR_CONVICTION_TIMING_STATE_%04u%02u%02u_%02u%02u%02u.txt",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static bool WaitForCtrlT() {
    bool oldT = false;
    std::wprintf(L"[PTAR] Arme. Dans la scene lente, appuyez sur CTRL+T sans quitter le jeu.\n");
    for (;;) {
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool t = (GetAsyncKeyState('T') & 0x8000) != 0;
        if (ctrl && t && !oldT) return true;
        oldT = t;
        Sleep(10);
    }
}

#pragma pack(push, 1)
struct UnicodeString32 {
    USHORT Length;
    USHORT MaximumLength;
    uint32_t Buffer;
};
#pragma pack(pop)

typedef LONG NTSTATUS;
typedef NTSTATUS (NTAPI *PFN_NtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct ProcessBasicInformationCompat {
    PVOID Reserved1;
    PVOID PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
};

static bool ReadRemoteCommandLine(HANDLE process, std::wstring& out) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    PFN_NtQueryInformationProcess ntq = reinterpret_cast<PFN_NtQueryInformationProcess>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (!ntq) return false;

    ProcessBasicInformationCompat pbi{};
    ULONG retLen = 0;
    const NTSTATUS st = ntq(process, 0, &pbi, sizeof(pbi), &retLen);
    if (st < 0 || !pbi.PebBaseAddress) return false;

    uint32_t processParameters = 0;
    const uintptr_t peb = reinterpret_cast<uintptr_t>(pbi.PebBaseAddress);
    if (!ReadRemote(process, peb + 0x10u, processParameters) || !processParameters) return false;

    UnicodeString32 us{};
    if (!ReadRemote(process, static_cast<uintptr_t>(processParameters) + 0x40u, us)) return false;
    if (!us.Buffer || us.Length == 0 || us.Length > 32766 || (us.Length & 1u)) return false;

    std::vector<wchar_t> buf(static_cast<size_t>(us.Length / 2u) + 1u, L'\0');
    SIZE_T got = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(us.Buffer)),
                           buf.data(), us.Length, &got) || got != us.Length) return false;
    out.assign(buf.data(), us.Length / 2u);
    return true;
}

static void PrintMappedPointer(FILE* f, const wchar_t* label, uintptr_t p, const std::vector<ModuleInfoLite>& mods) {
    const ModuleInfoLite* m = FindModuleByAddress(mods, p);
    if (m) {
        std::fwprintf(f, L"%ls=0x%08lX module=%ls offset=0x%08lX path=%ls\n",
            label,
            static_cast<unsigned long>(p),
            m->name.c_str(),
            static_cast<unsigned long>(p - m->base),
            m->path.c_str());
    } else {
        std::fwprintf(f, L"%ls=0x%08lX module=<unmapped>\n", label, static_cast<unsigned long>(p));
    }
}

static int SelfTest() {
    if (sizeof(void*) != 4u) return 10;
    const uintptr_t base = 0x00400000u;
    if (base + 0x00E66030u != 0x01266030u) return 11;
    if (base + 0x0102DFD8u != 0x0142DFD8u) return 12;
    if (base + 0x00B29300u != 0x00F29300u) return 13;
    if (base + 0x0002D0A4u != 0x0042D0A4u) return 14;
    std::puts("SELFTEST=PASS");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && (_wcsicmp(argv[1], L"--selftest") == 0)) return SelfTest();

    std::wprintf(L"PTAR Conviction Timing/State Probe V4 (x86 Win8.1)\n");
    std::wprintf(L"Lecture seule: aucun patch, aucune injection, aucune ecriture jeu/PTAR.\n");

    DWORD pid = 0;
    while (!pid) {
        pid = FindNewestProcessByName(L"Conviction_game.exe");
        if (!pid) {
            std::wprintf(L"[PTAR] En attente de Conviction_game.exe...\r");
            Sleep(500);
        }
    }
    std::wprintf(L"\n[PTAR] Conviction detecte pid=%lu.\n", pid);
    WaitForCtrlT();

    pid = FindNewestProcessByName(L"Conviction_game.exe");
    if (!pid) {
        std::fwprintf(stderr, L"Conviction a disparu avant la capture.\n");
        return 3;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        std::fwprintf(stderr, L"OpenProcess failed pid=%lu gle=%lu\n", pid, GetLastError());
        return 4;
    }

    const auto mods = EnumerateModules(pid);
    const ModuleInfoLite* game = FindModuleByName(mods, L"Conviction_game.exe");
    if (!game) {
        std::fwprintf(stderr, L"Conviction_game.exe module not found.\n");
        CloseHandle(process);
        return 5;
    }
    const uintptr_t base = game->base;

    const BYTE expectedLimiter[5] = {0xE8,0x54,0x9F,0x01,0x00};
    const BYTE expectedTimer[5] = {0x55,0x8B,0xEC,0x51,0x51};
    BYTE gotLimiter[5]{};
    BYTE gotTimer[5]{};
    if (!ReadRemoteBytes(process, base + 0x0002D0A4u, gotLimiter, sizeof(gotLimiter)) ||
        !ReadRemoteBytes(process, base + 0x00046FFDu, gotTimer, sizeof(gotTimer)) ||
        memcmp(gotLimiter, expectedLimiter, sizeof(expectedLimiter)) != 0 ||
        memcmp(gotTimer, expectedTimer, sizeof(expectedTimer)) != 0) {
        std::fwprintf(stderr, L"Executable signature mismatch; refusing fixed-RVA probe.\n");
        CloseHandle(process);
        return 6;
    }

    const std::wstring outName = TimestampName();
    FILE* f = nullptr;
    _wfopen_s(&f, outName.c_str(), L"wb");
    if (!f) {
        std::fwprintf(stderr, L"Unable to create output file.\n");
        CloseHandle(process);
        return 7;
    }

    std::fwprintf(f, L"PTAR_CONVICTION_TIMING_STATE=4\nPID=%lu\nGAME_BASE=0x%08lX\nGAME_SIZE=%lu\n",
        pid, static_cast<unsigned long>(base), game->size);
    std::fwprintf(f, L"POLICY=READ_ONLY_EXTERNAL_SAMPLING_NO_GAME_OR_PTAR_FILE_CHANGE_NO_D3D11_CHANGE\n");
    std::fwprintf(f, L"SIGNATURE_GUARD=PASS\n");

    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);
    const double expectedScale = qpf.QuadPart > 0 ? 1.0 / static_cast<double>(qpf.QuadPart) : 0.0;
    double timerScale = 0.0;
    float throttle = 0.0f;
    DWORD flagC4 = 0, flagC8 = 0, flag178 = 0;
    float frameElapsed = 0.0f, dt0 = 0.0f, dt1 = 0.0f;

    const bool okScale = ReadRemote(process, base + 0x00E66030u, timerScale);
    const bool okThrottle = ReadRemote(process, base + 0x0102DFD8u, throttle);
    ReadRemote(process, base + 0x00FA21C4u, flagC4);
    ReadRemote(process, base + 0x00FA21C8u, flagC8);
    ReadRemote(process, base + 0x00FA2178u, flag178);
    ReadRemote(process, base + 0x00FA21CCu, frameElapsed);
    ReadRemote(process, base + 0x00FA21D0u, dt0);
    ReadRemote(process, base + 0x00FA21D4u, dt1);

    std::fwprintf(f, L"LOCAL_QPF=%lld\nEXPECTED_TIMER_SCALE=%.17g\n", qpf.QuadPart, expectedScale);
    if (okScale) {
        const double ratio = expectedScale != 0.0 ? timerScale / expectedScale : 0.0;
        std::fwprintf(f, L"REMOTE_TIMER_SCALE=%.17g\nTIMER_SCALE_RATIO_TO_LOCAL_EXPECTED=%.9f\n", timerScale, ratio);
        std::fwprintf(f, L"PREDICTED_REAL_MS_FOR_120FPS_TARGET=%.6f\n",
            (ratio > 0.0) ? ((1000.0 / 120.0) / ratio) : -1.0);
    } else {
        std::fwprintf(f, L"REMOTE_TIMER_SCALE=READ_FAIL\n");
    }
    std::fwprintf(f, L"THROTTLE_0x142DFD8=%g read_ok=%d\n", static_cast<double>(throttle), okThrottle ? 1 : 0);
    std::fwprintf(f, L"FLAG_0x13A21C4=%lu\nFLAG_0x13A21C8=%lu\nFLAG_0x13A2178=%lu\n", flagC4, flagC8, flag178);
    std::fwprintf(f, L"GLOBAL_FRAME_ELAPSED_0x13A21CC=%g\nGLOBAL_DT0_0x13A21D0=%g\nGLOBAL_DT1_0x13A21D4=%g\n",
        static_cast<double>(frameElapsed), static_cast<double>(dt0), static_cast<double>(dt1));

    uint32_t pQpf = 0, pQpc = 0, pSleep = 0;
    ReadRemote(process, base + 0x00B29248u, pQpf);
    ReadRemote(process, base + 0x00B29300u, pQpc);
    ReadRemote(process, base + 0x00B2932Cu, pSleep);
    PrintMappedPointer(f, L"IAT_QueryPerformanceFrequency", static_cast<uintptr_t>(pQpf), mods);
    PrintMappedPointer(f, L"IAT_QueryPerformanceCounter", static_cast<uintptr_t>(pQpc), mods);
    PrintMappedPointer(f, L"IAT_Sleep", static_cast<uintptr_t>(pSleep), mods);

    std::wstring cmd;
    if (ReadRemoteCommandLine(process, cmd)) {
        std::fwprintf(f, L"COMMAND_LINE=%ls\n", cmd.c_str());
        const bool cpuSpeed = (cmd.find(L"CPUSPEED=") != std::wstring::npos || cmd.find(L"cpuspeed=") != std::wstring::npos);
        std::fwprintf(f, L"COMMAND_LINE_CPUSPEED_OVERRIDE=%s\n", cpuSpeed ? L"YES" : L"NO");
    } else {
        std::fwprintf(f, L"COMMAND_LINE=READ_FAIL\n");
    }

    std::vector<ThreadCpuSample> cpu;
    DWORD mainTid = 0;
    const bool hotOk = SelectHottestThread(pid, 600, mainTid, cpu);
    std::fwprintf(f, L"HOTTEST_THREAD_PROBE_MS=600\nHOTTEST_TID=%lu\nHOTTEST_SELECT_OK=%d\n", mainTid, hotOk ? 1 : 0);
    for (size_t i = 0; i < cpu.size() && i < 8u; ++i) {
        std::fwprintf(f, L"CPU_RANK_%u_TID=%lu CPU_MS=%.3f\n", static_cast<unsigned>(i + 1u), cpu[i].tid,
            static_cast<double>(cpu[i].delta100ns) / 10000.0);
    }

    if (!hotOk) {
        fclose(f);
        CloseHandle(process);
        std::wprintf(L"[PTAR] Etat statique capture, mais thread principal non resolu. Fichier: %ls\n", outName.c_str());
        return 0;
    }

    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, mainTid);
    if (!thread) {
        std::fwprintf(f, L"MAIN_THREAD_OPEN=FAIL gle=%lu\n", GetLastError());
        fclose(f);
        CloseHandle(process);
        std::wprintf(L"[PTAR] Capture partielle terminee: %ls\n", outName.c_str());
        return 0;
    }

    LARGE_INTEGER start{}, now{};
    QueryPerformanceCounter(&start);
    unsigned long samples = 0, suspendFail = 0, contextFail = 0, mainLoopHits = 0, limiterHits = 0, localReadHits = 0;
    std::vector<float> limiterTargets;
    std::vector<float> maxFpsPeriods;
    std::fwprintf(f, L"\nMAIN_THREAD_SAMPLES_BEGIN\nqpc_ms\teip\tebp\ttarget_interval\tmaxfps_interval\tglobal_elapsed\tdt0\tdt1\n");

    while (true) {
        Sleep(5);
        QueryPerformanceCounter(&now);
        const double elapsedSec = static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(qpf.QuadPart ? qpf.QuadPart : 1);
        if (elapsedSec >= 5.0) break;
        const DWORD sr = SuspendThread(thread);
        if (sr == 0xFFFFFFFFu) { ++suspendFail; continue; }
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL;
        const BOOL gotCtx = GetThreadContext(thread, &ctx);
        if (!gotCtx) {
            ++contextFail;
            ResumeThread(thread);
            continue;
        }

        float gElapsed = 0.0f, gDt0 = 0.0f, gDt1 = 0.0f;
        ReadRemote(process, base + 0x00FA21CCu, gElapsed);
        ReadRemote(process, base + 0x00FA21D0u, gDt0);
        ReadRemote(process, base + 0x00FA21D4u, gDt1);

        const uintptr_t eip = static_cast<uintptr_t>(ctx.Eip);
        const uintptr_t ebp = static_cast<uintptr_t>(ctx.Ebp);
        float target = -1.0f, maxPeriod = -1.0f;
        const bool inMain = (eip >= base + 0x0002CE22u && eip < base + 0x0002D3B0u);
        const bool inLimiter = (eip >= base + 0x0002D0A4u && eip <= base + 0x0002D0CFu);
        if (inMain) {
            ++mainLoopHits;
            if (ebp > 0x1000u && ReadRemote(process, ebp - 0x18u, target) && ReadRemote(process, ebp - 0x24u, maxPeriod)) {
                ++localReadHits;
                if (std::isfinite(target) && target > 0.0f && target < 1.0f) limiterTargets.push_back(target);
                if (std::isfinite(maxPeriod) && maxPeriod > 0.0f && maxPeriod < 1.0f) maxFpsPeriods.push_back(maxPeriod);
            }
        }
        if (inLimiter) ++limiterHits;

        const double qpcMs = static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(qpf.QuadPart ? qpf.QuadPart : 1);
        if (inLimiter || (inMain && (samples % 20u == 0u))) {
            std::fwprintf(f, L"%.3f\t0x%08lX\t0x%08lX\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\n",
                qpcMs, static_cast<unsigned long>(eip), static_cast<unsigned long>(ebp),
                static_cast<double>(target), static_cast<double>(maxPeriod),
                static_cast<double>(gElapsed), static_cast<double>(gDt0), static_cast<double>(gDt1));
        }
        ++samples;
        ResumeThread(thread);
    }

    CloseHandle(thread);

    auto medianFloat = [](std::vector<float> v)->double {
        if (v.empty()) return -1.0;
        std::sort(v.begin(), v.end());
        const size_t n = v.size();
        if (n & 1u) return static_cast<double>(v[n/2u]);
        return (static_cast<double>(v[n/2u - 1u]) + static_cast<double>(v[n/2u])) * 0.5;
    };

    std::fwprintf(f, L"MAIN_THREAD_SAMPLES_END\n");
    std::fwprintf(f, L"MAIN_SAMPLE_COUNT=%lu\nMAIN_SUSPEND_FAILURES=%lu\nMAIN_CONTEXT_FAILURES=%lu\n", samples, suspendFail, contextFail);
    std::fwprintf(f, L"MAIN_LOOP_HITS=%lu\nLIMITER_42D0A4_42D0CF_HITS=%lu\nFRAME_LOCAL_READ_HITS=%lu\n", mainLoopHits, limiterHits, localReadHits);
    const double medTarget = medianFloat(limiterTargets);
    const double medMax = medianFloat(maxFpsPeriods);
    std::fwprintf(f, L"FRAME_TARGET_INTERVAL_MEDIAN_S=%.9g\nMAXFPS_INTERVAL_MEDIAN_S=%.9g\n", medTarget, medMax);
    if (medTarget > 0.0) std::fwprintf(f, L"FRAME_TARGET_HZ_MEDIAN=%.6f\n", 1.0 / medTarget);
    if (medMax > 0.0) std::fwprintf(f, L"MAXFPS_HZ_MEDIAN=%.6f\n", 1.0 / medMax);

    double endScale = 0.0;
    if (ReadRemote(process, base + 0x00E66030u, endScale)) {
        std::fwprintf(f, L"REMOTE_TIMER_SCALE_END=%.17g\nTIMER_SCALE_CHANGED=%s\n", endScale,
            (okScale && endScale == timerScale) ? L"NO" : L"YES_OR_UNKNOWN");
    }

    fclose(f);
    CloseHandle(process);

    Beep(1000,120); Beep(1300,120); Beep(1600,180);
    std::wprintf(L"[PTAR] Capture V4 terminee. Envoyez ce fichier: %ls\n", outName.c_str());
    return 0;
}
