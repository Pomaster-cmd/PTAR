#define wmain PTAR_V4_UNUSED_WMAIN
#include "../conviction_timing_state/PTARConvictionTimingState.cpp"
#undef wmain

#include <utility>
#include <sstream>
#include <iomanip>

static std::wstring V6TimestampName() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[160]{};
    swprintf_s(buf, L"PTAR_CONVICTION_SLEEP_FORENSICS_%04u%02u%02u_%02u%02u%02u.txt",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::wstring V6PtrLabel(uintptr_t p, const std::vector<ModuleInfoLite>& mods) {
    const ModuleInfoLite* m = FindModuleByAddress(mods, p);
    wchar_t buf[256]{};
    if (m) {
        swprintf_s(buf, L"%ls+0x%08lX", m->name.c_str(), static_cast<unsigned long>(p - m->base));
    } else {
        swprintf_s(buf, L"<unmapped>+0x%08lX", static_cast<unsigned long>(p));
    }
    return buf;
}

static void V6PrintMappedPointer(FILE* f, const wchar_t* label, uintptr_t p,
                                 const std::vector<ModuleInfoLite>& mods) {
    const ModuleInfoLite* m = FindModuleByAddress(mods, p);
    if (m) {
        std::fwprintf(f, L"%ls=0x%08lX module=%ls offset=0x%08lX path=%ls\n",
            label, static_cast<unsigned long>(p), m->name.c_str(),
            static_cast<unsigned long>(p - m->base), m->path.c_str());
    } else {
        std::fwprintf(f, L"%ls=0x%08lX module=<unmapped>\n", label, static_cast<unsigned long>(p));
    }
}

static void V6DumpHexRange(FILE* f, const wchar_t* label, HANDLE process,
                           uintptr_t address, SIZE_T bytes) {
    std::vector<unsigned char> buf(bytes);
    SIZE_T got = 0;
    const BOOL ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address),
                                      buf.data(), bytes, &got);
    std::fwprintf(f, L"\nDUMP_BEGIN label=%ls address=0x%08lX requested=%lu read_ok=%d got=%lu\n",
        label, static_cast<unsigned long>(address), static_cast<unsigned long>(bytes),
        (ok && got == bytes) ? 1 : 0, static_cast<unsigned long>(got));
    if (!ok || got != bytes) {
        std::fwprintf(f, L"DUMP_END label=%ls gle=%lu\n", label, GetLastError());
        return;
    }
    for (SIZE_T i = 0; i < bytes; i += 32u) {
        const SIZE_T n = (std::min)(static_cast<SIZE_T>(32u), bytes - i);
        std::fwprintf(f, L"0x%08lX ", static_cast<unsigned long>(address + i));
        for (SIZE_T j = 0; j < n; ++j) std::fwprintf(f, L"%02X", static_cast<unsigned>(buf[i + j]));
        std::fwprintf(f, L"\n");
    }
    std::fwprintf(f, L"DUMP_END label=%ls\n", label);
}

static void V6DumpPeMetadata(FILE* f, HANDLE process, const ModuleInfoLite& m) {
    IMAGE_DOS_HEADER dos{};
    if (!ReadRemote(process, m.base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        std::fwprintf(f, L"MODULE_PE_META name=%ls status=BAD_DOS\n", m.name.c_str());
        return;
    }
    IMAGE_NT_HEADERS32 nt{};
    const uintptr_t ntAddr = m.base + static_cast<uintptr_t>(dos.e_lfanew);
    if (!ReadRemote(process, ntAddr, nt) || nt.Signature != IMAGE_NT_SIGNATURE) {
        std::fwprintf(f, L"MODULE_PE_META name=%ls status=BAD_NT\n", m.name.c_str());
        return;
    }
    std::fwprintf(f, L"MODULE_PE_META name=%ls base=0x%08lX snapshot_size=0x%08lX timestamp=0x%08lX "
        L"image_size=0x%08lX entry_rva=0x%08lX checksum=0x%08lX sections=%u path=%ls\n",
        m.name.c_str(), static_cast<unsigned long>(m.base), static_cast<unsigned long>(m.size),
        static_cast<unsigned long>(nt.FileHeader.TimeDateStamp),
        static_cast<unsigned long>(nt.OptionalHeader.SizeOfImage),
        static_cast<unsigned long>(nt.OptionalHeader.AddressOfEntryPoint),
        static_cast<unsigned long>(nt.OptionalHeader.CheckSum),
        static_cast<unsigned>(nt.FileHeader.NumberOfSections), m.path.c_str());

    const uintptr_t secBase = ntAddr + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    const unsigned count = (std::min)(static_cast<unsigned>(nt.FileHeader.NumberOfSections), 32u);
    for (unsigned i = 0; i < count; ++i) {
        IMAGE_SECTION_HEADER sh{};
        if (!ReadRemote(process, secBase + static_cast<uintptr_t>(i) * sizeof(sh), sh)) break;
        char nbuf[9]{};
        std::memcpy(nbuf, sh.Name, 8u);
        std::fwprintf(f, L"MODULE_SECTION index=%u name=%hs va=0x%08lX vsize=0x%08lX raw=0x%08lX chars=0x%08lX\n",
            i, nbuf, static_cast<unsigned long>(sh.VirtualAddress),
            static_cast<unsigned long>(sh.Misc.VirtualSize),
            static_cast<unsigned long>(sh.SizeOfRawData),
            static_cast<unsigned long>(sh.Characteristics));
    }
}

static int V6SelfTest() {
    if (sizeof(void*) != 4u) return 10;
    const uintptr_t game = 0x00400000u;
    if (game + 0x00B2932Cu != 0x00F2932Cu) return 11;
    if (game + 0x00FA236Cu != 0x013A236Cu) return 12;
    if ((0x0003C33Cu & ~static_cast<uintptr_t>(0xFFu)) != 0x0003C300u) return 13;
    std::vector<ModuleInfoLite> mods;
    mods.push_back({0x10000000u, 0x00100000u, L"VERSION.dll", L"X"});
    if (V6PtrLabel(0x10038FA0u, mods) != L"VERSION.dll+0x00038FA0") return 14;
    std::puts("SELFTEST=PASS");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && _wcsicmp(argv[1], L"--selftest") == 0) return V6SelfTest();

    std::wprintf(L"PTAR Conviction Sleep/Main-Thread Forensics V6 (x86 Win8.1)\n");
    std::wprintf(L"Lecture seule: aucun patch, injection ou ecriture dans Conviction/PTAR/D3D11.\n");

    DWORD pid = 0;
    while (!pid) {
        pid = FindNewestProcessByName(L"Conviction_game.exe");
        if (!pid) {
            std::wprintf(L"[PTAR] En attente de Conviction_game.exe...\r");
            Sleep(500);
        }
    }
    std::wprintf(L"\n[PTAR] Conviction detecte pid=%lu. Reste dans la scene lente puis CTRL+T.\n", pid);
    WaitForCtrlT();

    pid = FindNewestProcessByName(L"Conviction_game.exe");
    if (!pid) return 3;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 4;

    const auto mods = EnumerateModules(pid);
    const ModuleInfoLite* game = FindModuleByName(mods, L"Conviction_game.exe");
    const ModuleInfoLite* version = FindModuleByName(mods, L"VERSION.dll");
    const ModuleInfoLite* ntdll = FindModuleByName(mods, L"ntdll.dll");
    const ModuleInfoLite* kernelbase = FindModuleByName(mods, L"KERNELBASE.dll");
    if (!game || !version || !ntdll) { CloseHandle(process); return 5; }
    const uintptr_t base = game->base;

    const BYTE expectedLimiter[5] = {0xE8,0x54,0x9F,0x01,0x00};
    BYTE gotLimiter[5]{};
    if (!ReadRemoteBytes(process, base + 0x0002D0A4u, gotLimiter, sizeof(gotLimiter)) ||
        std::memcmp(gotLimiter, expectedLimiter, sizeof(expectedLimiter)) != 0) {
        CloseHandle(process);
        return 6;
    }

    const std::wstring outName = V6TimestampName();
    FILE* f = nullptr;
    _wfopen_s(&f, outName.c_str(), L"wb");
    if (!f) { CloseHandle(process); return 7; }

    std::fwprintf(f, L"PTAR_CONVICTION_SLEEP_FORENSICS=6\nPID=%lu\nGAME_BASE=0x%08lX\n",
        pid, static_cast<unsigned long>(base));
    std::fwprintf(f, L"POLICY=READ_ONLY_EXTERNAL_NO_REMOTE_WRITE_NO_INJECTION_NO_GAME_OR_PTAR_FILE_CHANGE_NO_D3D11_CHANGE\n");
    std::fwprintf(f, L"SIGNATURE_GUARD=PASS\nEIP_SAMPLE_TARGET_MS=4\nIAT_POLL_TARGET_MS=1\nSAMPLE_DURATION_S=6\n");
    V6DumpPeMetadata(f, process, *version);
    V6DumpPeMetadata(f, process, *ntdll);
    if (kernelbase) V6DumpPeMetadata(f, process, *kernelbase);

    const uintptr_t sleepSlot = base + 0x00B2932Cu;
    uint32_t initialSleep32 = 0;
    ReadRemote(process, sleepSlot, initialSleep32);
    std::fwprintf(f, L"SLEEP_IAT_SLOT=0x%08lX\n", static_cast<unsigned long>(sleepSlot));
    V6PrintMappedPointer(f, L"SLEEP_IAT_INITIAL", static_cast<uintptr_t>(initialSleep32), mods);

    std::vector<ThreadCpuSample> cpu;
    DWORD mainTid = 0;
    const bool mainOk = SelectHottestThread(pid, 600, mainTid, cpu);
    std::fwprintf(f, L"MAIN_HOTTEST_SELECT_OK=%d\nMAIN_TID=%lu\n", mainOk ? 1 : 0, mainTid);
    for (size_t i = 0; i < cpu.size() && i < 6u; ++i) {
        std::fwprintf(f, L"CPU_RANK_%u_TID=%lu CPU_MS=%.3f\n", static_cast<unsigned>(i + 1u),
            cpu[i].tid, static_cast<double>(cpu[i].delta100ns) / 10000.0);
    }
    if (!mainOk) {
        fclose(f); CloseHandle(process); return 8;
    }

    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, mainTid);
    if (!thread) {
        std::fwprintf(f, L"MAIN_THREAD_OPEN=FAIL gle=%lu\n", GetLastError());
        fclose(f); CloseHandle(process); return 9;
    }

    LARGE_INTEGER qpf{}, start{}, now{};
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&start);
    if (qpf.QuadPart <= 0) qpf.QuadPart = 1;

    std::map<std::wstring, unsigned long> eipCounts;
    std::map<std::wstring, unsigned long> returnCounts;
    std::map<std::wstring, unsigned long> pairCounts;
    std::map<uint32_t, unsigned long> sleepPtrCounts;
    unsigned long samples = 0, suspendFail = 0, contextFail = 0, stackReadFail = 0;
    unsigned long iatReadFail = 0, iatChanges = 0, iatPolls = 0;
    uint32_t lastSleep32 = initialSleep32;
    unsigned long long mainCpuBefore = 0, mainCpuAfter = 0;
    GetThreadCpu100ns(mainTid, mainCpuBefore);

    std::fwprintf(f, L"\nINTERESTING_SAMPLES_BEGIN\nqpc_ms\teip\teip_site\tesp\tstack0\tfirst_mapped_index\tfirst_mapped_site\tengine_fps\n");
    while (true) {
        Sleep(1);
        QueryPerformanceCounter(&now);
        const double sec = static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(qpf.QuadPart);
        if (sec >= 6.0) break;

        ++iatPolls;
        uint32_t sleep32 = 0;
        if (ReadRemote(process, sleepSlot, sleep32)) {
            ++sleepPtrCounts[sleep32];
            if (sleep32 != lastSleep32) {
                ++iatChanges;
                const double ms = sec * 1000.0;
                std::fwprintf(f, L"IAT_CHANGE qpc_ms=%.3f old=0x%08X new=0x%08X\n", ms, lastSleep32, sleep32);
                lastSleep32 = sleep32;
            }
        } else {
            ++iatReadFail;
        }

        // Keep IAT observation dense (~1 ms), but only suspend the game thread about every 4 polls.
        if ((iatPolls & 3u) != 0u) continue;

        const DWORD sr = SuspendThread(thread);
        if (sr == 0xFFFFFFFFu) { ++suspendFail; continue; }
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL;
        const BOOL gotCtx = GetThreadContext(thread, &ctx);
        uint32_t stackWords[16]{};
        bool stackOk = false;
        if (gotCtx && ctx.Esp >= 0x1000u) {
            stackOk = ReadRemoteBytes(process, static_cast<uintptr_t>(ctx.Esp), stackWords, sizeof(stackWords));
            if (!stackOk) ++stackReadFail;
        }
        ResumeThread(thread);
        if (!gotCtx) { ++contextFail; continue; }

        const uintptr_t eip = static_cast<uintptr_t>(ctx.Eip);
        const std::wstring eipLabel = V6PtrLabel(eip, mods);
        ++eipCounts[eipLabel];

        int firstMappedIndex = -1;
        uintptr_t firstMapped = 0;
        if (stackOk) {
            for (int i = 0; i < 16; ++i) {
                const uintptr_t candidate = static_cast<uintptr_t>(stackWords[i]);
                if (FindModuleByAddress(mods, candidate)) {
                    firstMappedIndex = i;
                    firstMapped = candidate;
                    break;
                }
            }
        }
        const std::wstring retLabel = firstMappedIndex >= 0 ? V6PtrLabel(firstMapped, mods) : L"<none>";
        ++returnCounts[retLabel];
        ++pairCounts[eipLabel + L" <- " + retLabel];

        float engineFps = -1.0f;
        uint32_t enginePtr32 = 0;
        if (ReadRemote(process, base + 0x00FA236Cu, enginePtr32) && enginePtr32)
            ReadRemote(process, static_cast<uintptr_t>(enginePtr32) + 0xF8u, engineFps);

        const ModuleInfoLite* eipMod = FindModuleByAddress(mods, eip);
        const uintptr_t eipOff = eipMod ? (eip - eipMod->base) : 0u;
        const bool interesting = eipMod &&
            ((_wcsicmp(eipMod->name.c_str(), L"VERSION.dll") == 0) ||
             (_wcsicmp(eipMod->name.c_str(), L"ntdll.dll") == 0 && eipOff >= 0x0003C200u && eipOff < 0x0003C500u) ||
             (_wcsicmp(eipMod->name.c_str(), L"Conviction_game.exe") == 0 && eipOff >= 0x0002CE00u && eipOff < 0x0002D400u));
        if (interesting && (samples < 40u || (samples % 8u) == 0u)) {
            const double ms = sec * 1000.0;
            std::fwprintf(f, L"%.3f\t0x%08lX\t%ls\t0x%08lX\t0x%08X\t%d\t%ls\t%.6f\n",
                ms, static_cast<unsigned long>(eip), eipLabel.c_str(),
                static_cast<unsigned long>(ctx.Esp), stackOk ? stackWords[0] : 0u,
                firstMappedIndex, retLabel.c_str(), static_cast<double>(engineFps));
        }
        ++samples;
    }
    std::fwprintf(f, L"INTERESTING_SAMPLES_END\n");
    GetThreadCpu100ns(mainTid, mainCpuAfter);
    CloseHandle(thread);

    uint32_t finalSleep32 = 0;
    ReadRemote(process, sleepSlot, finalSleep32);
    std::fwprintf(f, L"\nSUMMARY eip_samples=%lu iat_polls=%lu suspend_fail=%lu context_fail=%lu stack_read_fail=%lu iat_read_fail=%lu iat_changes=%lu\n",
        samples, iatPolls, suspendFail, contextFail, stackReadFail, iatReadFail, iatChanges);
    if (mainCpuAfter >= mainCpuBefore)
        std::fwprintf(f, L"MAIN_CPU_MS_OVER_6S=%.3f\n", static_cast<double>(mainCpuAfter - mainCpuBefore) / 10000.0);
    V6PrintMappedPointer(f, L"SLEEP_IAT_FINAL", static_cast<uintptr_t>(finalSleep32), mods);

    std::fwprintf(f, L"\nSLEEP_IAT_POINTER_COUNTS\n");
    for (const auto& it : sleepPtrCounts) {
        std::fwprintf(f, L"0x%08X\t%lu\t%ls\n", it.first, it.second,
            V6PtrLabel(static_cast<uintptr_t>(it.first), mods).c_str());
    }

    auto dumpTop = [f](const wchar_t* title, const std::map<std::wstring, unsigned long>& m, size_t topN) {
        std::vector<std::pair<std::wstring, unsigned long>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::fwprintf(f, L"\n%ls\n", title);
        const size_t n = (std::min)(topN, v.size());
        for (size_t i = 0; i < n; ++i) std::fwprintf(f, L"%ls\t%lu\n", v[i].first.c_str(), v[i].second);
    };
    dumpTop(L"TOP_EXACT_EIP", eipCounts, 80u);
    dumpTop(L"TOP_FIRST_MAPPED_STACK", returnCounts, 60u);
    dumpTop(L"TOP_EIP_RETURN_PAIRS", pairCounts, 100u);

    V6DumpHexRange(f, L"VERSION_23680_2397F", process, version->base + 0x00023680u, 0x300u);
    V6DumpHexRange(f, L"VERSION_26680_2697F", process, version->base + 0x00026680u, 0x300u);
    V6DumpHexRange(f, L"VERSION_38F00_390FF", process, version->base + 0x00038F00u, 0x200u);
    V6DumpHexRange(f, L"VERSION_3EA80_3EC7F", process, version->base + 0x0003EA80u, 0x200u);
    V6DumpHexRange(f, L"VERSION_47780_47AFF", process, version->base + 0x00047780u, 0x380u);
    V6DumpHexRange(f, L"VERSION_86580_8687F", process, version->base + 0x00086580u, 0x300u);
    V6DumpHexRange(f, L"NTDLL_3C280_3C47F", process, ntdll->base + 0x0003C280u, 0x200u);
    if (kernelbase) V6DumpHexRange(f, L"KERNELBASE_2A80_2C7F", process, kernelbase->base + 0x00002A80u, 0x200u);

    fclose(f);
    CloseHandle(process);
    Beep(1000, 120); Beep(1300, 120); Beep(1600, 180);
    std::wprintf(L"[PTAR] Capture V6 terminee. Envoyez: %ls\n", outName.c_str());
    return 0;
}
