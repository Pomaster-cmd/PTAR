#define wmain PTAR_V4_UNUSED_WMAIN
#include "../conviction_timing_state/PTARConvictionTimingState.cpp"
#undef wmain

#include <numeric>

static std::wstring V5TimestampName() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[128]{};
    swprintf_s(buf, L"PTAR_CONVICTION_FRAME_PIPELINE_%04u%02u%02u_%02u%02u%02u.txt",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static void V5PrintPtr(FILE* f, const wchar_t* label, uintptr_t p, const std::vector<ModuleInfoLite>& mods) {
    const ModuleInfoLite* m = FindModuleByAddress(mods, p);
    if (m) {
        std::fwprintf(f, L"%ls=0x%08lX module=%ls offset=0x%08lX path=%ls\n",
            label, static_cast<unsigned long>(p), m->name.c_str(),
            static_cast<unsigned long>(p - m->base), m->path.c_str());
    } else {
        std::fwprintf(f, L"%ls=0x%08lX module=<unmapped>\n", label, static_cast<unsigned long>(p));
    }
}

static void V5HexDump(FILE* f, const wchar_t* label, HANDLE process, uintptr_t addr, SIZE_T bytes) {
    std::vector<unsigned char> buf(bytes);
    SIZE_T got = 0;
    const BOOL ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(addr), buf.data(), bytes, &got);
    std::fwprintf(f, L"%ls_ADDR=0x%08lX\n%ls_READ_OK=%d\n%ls_BYTES=", label,
        static_cast<unsigned long>(addr), label, (ok && got == bytes) ? 1 : 0, label);
    if (!ok || got != bytes) {
        std::fwprintf(f, L"<READ_FAIL got=%lu gle=%lu>\n", static_cast<unsigned long>(got), GetLastError());
        return;
    }
    for (SIZE_T i = 0; i < bytes; ++i) std::fwprintf(f, L"%02X", static_cast<unsigned>(buf[i]));
    std::fwprintf(f, L"\n");
}

static double V5Median(std::vector<float> v) {
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    if (n & 1u) return v[n / 2u];
    return 0.5 * (static_cast<double>(v[n / 2u - 1u]) + static_cast<double>(v[n / 2u]));
}

static int V5SelfTest() {
    if (sizeof(void*) != 4u) return 10;
    const uintptr_t game = 0x00400000u;
    if (game + 0x00FA236Cu != 0x013A236Cu) return 11;
    if (game + 0x00B29428u != 0x00F29428u) return 12;
    if (game + 0x00CE0C30u != 0x010E0C30u) return 13;
    if (game + 0x00CE0C54u != 0x010E0C54u) return 14;
    std::puts("SELFTEST=PASS");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && _wcsicmp(argv[1], L"--selftest") == 0) return V5SelfTest();

    std::wprintf(L"PTAR Conviction Frame Pipeline Probe V5 (x86 Win8.1)\n");
    std::wprintf(L"Lecture seule: aucun patch, aucune injection, aucune ecriture jeu/PTAR/D3D11.\n");

    DWORD pid = 0;
    while (!pid) {
        pid = FindNewestProcessByName(L"Conviction_game.exe");
        if (!pid) {
            std::wprintf(L"[PTAR] En attente de Conviction_game.exe...\r");
            Sleep(500);
        }
    }
    std::wprintf(L"\n[PTAR] Conviction detecte pid=%lu. Reste dans la scene puis CTRL+T.\n", pid);
    WaitForCtrlT();

    pid = FindNewestProcessByName(L"Conviction_game.exe");
    if (!pid) return 3;

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 4;
    const auto mods = EnumerateModules(pid);
    const ModuleInfoLite* game = FindModuleByName(mods, L"Conviction_game.exe");
    if (!game) { CloseHandle(process); return 5; }
    const uintptr_t base = game->base;

    const BYTE expectedLimiter[5] = {0xE8,0x54,0x9F,0x01,0x00};
    BYTE gotLimiter[5]{};
    if (!ReadRemoteBytes(process, base + 0x0002D0A4u, gotLimiter, sizeof(gotLimiter)) ||
        memcmp(gotLimiter, expectedLimiter, sizeof(expectedLimiter)) != 0) {
        CloseHandle(process); return 6;
    }

    const std::wstring outName = V5TimestampName();
    FILE* f = nullptr;
    _wfopen_s(&f, outName.c_str(), L"wb");
    if (!f) { CloseHandle(process); return 7; }

    std::fwprintf(f, L"PTAR_CONVICTION_FRAME_PIPELINE=5\nPID=%lu\nGAME_BASE=0x%08lX\n",
        pid, static_cast<unsigned long>(base));
    std::fwprintf(f, L"POLICY=READ_ONLY_EXTERNAL_NO_GAME_OR_PTAR_FILE_CHANGE_NO_D3D11_CHANGE\nSIGNATURE_GUARD=PASS\n");

    uint32_t enginePtr32 = 0;
    const uintptr_t engineGlobal = base + 0x00FA236Cu;
    const bool enginePtrOk = ReadRemote(process, engineGlobal, enginePtr32) && enginePtr32 != 0;
    const uintptr_t enginePtr = static_cast<uintptr_t>(enginePtr32);
    std::fwprintf(f, L"ENGINE_GLOBAL=0x%08lX\nENGINE_PTR=0x%08lX read_ok=%d\n",
        static_cast<unsigned long>(engineGlobal), static_cast<unsigned long>(enginePtr), enginePtrOk ? 1 : 0);
    if (enginePtrOk) {
        uint32_t engineVt = 0;
        ReadRemote(process, enginePtr, engineVt);
        V5PrintPtr(f, L"ENGINE_VTABLE", static_cast<uintptr_t>(engineVt), mods);
    }

    uint32_t renderGlobal32 = 0, baseThread32 = 0, baseVt32 = 0, outerVt32 = 0;
    DWORD renderTid = 0;
    uint32_t stopEvent = 0, workEvent = 0, doneEvent = 0;
    uintptr_t outer = 0;
    const uintptr_t renderIatSlot = base + 0x00B29428u;
    bool renderOk = ReadRemote(process, renderIatSlot, renderGlobal32) && renderGlobal32 != 0;
    if (renderOk) renderOk = ReadRemote(process, static_cast<uintptr_t>(renderGlobal32), baseThread32) && baseThread32 >= 0x1FCu;
    if (renderOk) {
        outer = static_cast<uintptr_t>(baseThread32) - 0x1FCu;
        renderOk = ReadRemote(process, static_cast<uintptr_t>(baseThread32), baseVt32) &&
                   ReadRemote(process, outer, outerVt32) &&
                   ReadRemote(process, outer + 0x8u, renderTid) && renderTid != 0;
        ReadRemote(process, outer + 0x9Cu, stopEvent);
        ReadRemote(process, outer + 0xA0u, workEvent);
        ReadRemote(process, outer + 0xA4u, doneEvent);
    }
    const uintptr_t expectedBaseVt = base + 0x00CE0C30u;
    const uintptr_t expectedOuterVt = base + 0x00CE0C54u;
    if (renderOk && (static_cast<uintptr_t>(baseVt32) != expectedBaseVt || static_cast<uintptr_t>(outerVt32) != expectedOuterVt)) renderOk = false;

    std::fwprintf(f, L"RENDER_RESOLVE_OK=%d\nRENDER_IAT_SLOT=0x%08lX\nRENDER_GLOBAL=0x%08X\nBASETHREAD_PTR=0x%08X\nOUTER_PTR=0x%08lX\n",
        renderOk ? 1 : 0, static_cast<unsigned long>(renderIatSlot), renderGlobal32, baseThread32, static_cast<unsigned long>(outer));
    std::fwprintf(f, L"BASE_VTABLE=0x%08X expected=0x%08lX\nOUTER_VTABLE=0x%08X expected=0x%08lX\nRENDER_TID=%lu\nSTOP_EVENT=0x%08X\nWORK_EVENT=0x%08X\nDONE_EVENT=0x%08X\n",
        baseVt32, static_cast<unsigned long>(expectedBaseVt), outerVt32, static_cast<unsigned long>(expectedOuterVt),
        renderTid, stopEvent, workEvent, doneEvent);

    if (renderOk) {
        uint32_t baseVfunc14 = 0, outerVfunc14 = 0;
        ReadRemote(process, static_cast<uintptr_t>(baseVt32) + 0x14u, baseVfunc14);
        ReadRemote(process, static_cast<uintptr_t>(outerVt32) + 0x14u, outerVfunc14);
        V5PrintPtr(f, L"BASE_VFUNC_PLUS_14", static_cast<uintptr_t>(baseVfunc14), mods);
        V5PrintPtr(f, L"OUTER_VFUNC_PLUS_14", static_cast<uintptr_t>(outerVfunc14), mods);
        V5HexDump(f, L"BASE_VFUNC_PLUS_14_CODE", process, static_cast<uintptr_t>(baseVfunc14), 192u);
        if (outerVfunc14 != baseVfunc14)
            V5HexDump(f, L"OUTER_VFUNC_PLUS_14_CODE", process, static_cast<uintptr_t>(outerVfunc14), 192u);
    }

    struct ImportProbe { const wchar_t* label; uintptr_t rva; } imports[] = {
        {L"IAT_GetRenderThread", 0x00B295A0u},
        {L"IAT_FlushBuffers", 0x00B2958Cu},
        {L"IAT_RenderFrame", 0x00B29560u},
        {L"IAT_AllowRenderPresent", 0x00B29564u},
        {L"IAT_FlushRenderBuffers", 0x00B29810u},
        {L"IAT_SwapBuffers", 0x00B29814u},
        {L"IAT_Sleep", 0x00B2932Cu}
    };
    for (const auto& ip : imports) {
        uint32_t p = 0;
        ReadRemote(process, base + ip.rva, p);
        V5PrintPtr(f, ip.label, static_cast<uintptr_t>(p), mods);
    }

    std::vector<ThreadCpuSample> cpu;
    DWORD mainTid = 0;
    const bool mainOk = SelectHottestThread(pid, 600, mainTid, cpu);
    std::fwprintf(f, L"MAIN_HOTTEST_SELECT_OK=%d\nMAIN_TID=%lu\n", mainOk ? 1 : 0, mainTid);
    for (size_t i = 0; i < cpu.size() && i < 6u; ++i)
        std::fwprintf(f, L"CPU_RANK_%u_TID=%lu CPU_MS=%.3f\n", static_cast<unsigned>(i+1u), cpu[i].tid, static_cast<double>(cpu[i].delta100ns)/10000.0);

    unsigned long long mainCpuBefore = 0, mainCpuAfter = 0, renderCpuBefore = 0, renderCpuAfter = 0;
    if (mainOk) GetThreadCpu100ns(mainTid, mainCpuBefore);
    if (renderOk) GetThreadCpu100ns(renderTid, renderCpuBefore);

    LARGE_INTEGER qpf{}, start{}, now{};
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&start);
    std::vector<float> engineFps;
    std::vector<float> frameElapsed;
    std::vector<float> dt0s;
    unsigned long samples = 0;
    unsigned long engineReadFail = 0;
    float lastFps = -1000.0f;
    unsigned long fpsChanges = 0;

    std::fwprintf(f, L"\nTIMELINE_BEGIN\nqpc_ms\tengine_fps\tframe_elapsed\tdt0\tdt1\n");
    while (true) {
        Sleep(20);
        QueryPerformanceCounter(&now);
        const double sec = static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(qpf.QuadPart ? qpf.QuadPart : 1);
        if (sec >= 6.0) break;

        float fps = -1.0f, ge = -1.0f, d0 = -1.0f, d1 = -1.0f;
        const bool fpsOk = enginePtrOk && ReadRemote(process, enginePtr + 0xF8u, fps);
        if (!fpsOk) ++engineReadFail;
        ReadRemote(process, base + 0x00FA21CCu, ge);
        ReadRemote(process, base + 0x00FA21D0u, d0);
        ReadRemote(process, base + 0x00FA21D4u, d1);

        if (fpsOk && std::isfinite(fps) && fps >= 0.0f && fps < 1000.0f) {
            engineFps.push_back(fps);
            if (lastFps < -999.0f || std::fabs(fps - lastFps) > 0.01f) {
                ++fpsChanges;
                const double ms = sec * 1000.0;
                std::fwprintf(f, L"%.3f\t%.6f\t%.9g\t%.9g\t%.9g\n", ms, static_cast<double>(fps), static_cast<double>(ge), static_cast<double>(d0), static_cast<double>(d1));
                lastFps = fps;
            }
        }
        if (std::isfinite(ge) && ge >= 0.0f && ge < 10.0f) frameElapsed.push_back(ge);
        if (std::isfinite(d0) && d0 >= 0.0f && d0 < 10.0f) dt0s.push_back(d0);
        ++samples;
    }
    std::fwprintf(f, L"TIMELINE_END\n");

    if (mainOk) GetThreadCpu100ns(mainTid, mainCpuAfter);
    if (renderOk) GetThreadCpu100ns(renderTid, renderCpuAfter);

    std::fwprintf(f, L"SAMPLES=%lu\nENGINE_FPS_READ_FAILURES=%lu\nENGINE_FPS_CHANGE_EVENTS=%lu\n", samples, engineReadFail, fpsChanges);
    if (!engineFps.empty()) {
        const auto mm = std::minmax_element(engineFps.begin(), engineFps.end());
        std::fwprintf(f, L"ENGINE_LOOP_FPS_MIN=%.6f\nENGINE_LOOP_FPS_MEDIAN=%.6f\nENGINE_LOOP_FPS_MAX=%.6f\n",
            static_cast<double>(*mm.first), V5Median(engineFps), static_cast<double>(*mm.second));
    }
    if (!frameElapsed.empty()) {
        const auto mm = std::minmax_element(frameElapsed.begin(), frameElapsed.end());
        std::fwprintf(f, L"GLOBAL_FRAME_ELAPSED_MIN=%.9g\nGLOBAL_FRAME_ELAPSED_MEDIAN=%.9g\nGLOBAL_FRAME_ELAPSED_MAX=%.9g\n",
            static_cast<double>(*mm.first), V5Median(frameElapsed), static_cast<double>(*mm.second));
    }
    if (!dt0s.empty()) {
        const auto mm = std::minmax_element(dt0s.begin(), dt0s.end());
        std::fwprintf(f, L"DT0_MIN=%.9g\nDT0_MEDIAN=%.9g\nDT0_MAX=%.9g\n",
            static_cast<double>(*mm.first), V5Median(dt0s), static_cast<double>(*mm.second));
    }
    if (mainOk && mainCpuAfter >= mainCpuBefore)
        std::fwprintf(f, L"MAIN_CPU_MS_OVER_6S=%.3f\n", static_cast<double>(mainCpuAfter-mainCpuBefore)/10000.0);
    if (renderOk && renderCpuAfter >= renderCpuBefore)
        std::fwprintf(f, L"RENDER_CPU_MS_OVER_6S=%.3f\n", static_cast<double>(renderCpuAfter-renderCpuBefore)/10000.0);

    fclose(f);
    CloseHandle(process);
    MessageBeep(MB_OK); Sleep(80); MessageBeep(MB_ICONASTERISK); Sleep(80); MessageBeep(MB_OK);
    std::wprintf(L"[PTAR] Capture terminee: %ls\n", outName.c_str());
    return 0;
}
