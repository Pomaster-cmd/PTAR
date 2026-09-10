#include "Lab38Bench.h"
#include "Lab38Report.h"

#include <windows.h>
#include <cstring>
#include <iostream>
#include <vector>

using namespace ptar_lab38;

int wmain(int argc, wchar_t** argv) {
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--warp-smoke") == 0) warp = true;
    }

    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return 2;
    std::wstring base = exe;
    const size_t slash = base.find_last_of(L"\\/");
    if (slash != std::wstring::npos) base.resize(slash); else base = L".";
    const std::wstring shaders = Join(base, L"shaders");

    Bench bench;
    std::wstring adapter;
    HRESULT hr = bench.Init(warp, adapter);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] D3D init hr=0x" << std::hex << static_cast<unsigned int>(hr) << std::dec << std::endl;
        return 10;
    }
    hr = bench.Load(shaders);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] shader load hr=0x" << std::hex << static_cast<unsigned int>(hr) << std::dec << std::endl;
        return 11;
    }

    if (warp) {
        hr = bench.Smoke();
        if (FAILED(hr)) return 12;
        std::cout << "[PASS] WARP smoke: LAB37B/LAB38A/LAB38B/LAB38C/LAB38D/K185 loaded and drawn." << std::endl;
        return 0;
    }

    std::wcout << L"[GPU] selected=" << adapter << std::endl;
    std::vector<SampleRow> rows;
    hr = bench.Run(rows);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] timing hr=0x" << std::hex << static_cast<unsigned int>(hr) << std::dec << std::endl;
        return 20;
    }

    const std::wstring outDir = Join(base, L"results");
    if (!EnsureDir(outDir)) return 21;
    WriteReports(outDir, adapter, rows);
    PrintConsoleSummary(rows);
    std::cout << "[PASS] results written under .\\results" << std::endl;
    return 0;
}
