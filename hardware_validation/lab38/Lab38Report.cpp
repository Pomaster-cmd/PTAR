#include "Lab38Report.h"
#include "Lab38Bench.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>

namespace ptar_lab38 {
namespace {

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = (v.size() - 1u) * p;
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi = lo + 1u < v.size() ? lo + 1u : lo;
    const double t = pos - static_cast<double>(lo);
    return v[lo] + (v[hi] - v[lo]) * t;
}

double Median(const std::vector<double>& v) { return Percentile(v, 0.5); }

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string("UNKNOWN");
    std::string s(static_cast<size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr) != n) return std::string("UNKNOWN");
    return s;
}

std::vector<double> Values(const std::vector<SampleRow>& rows, int variant, size_t begin = 0, size_t end = static_cast<size_t>(-1)) {
    end = (std::min)(end, rows.size());
    std::vector<double> v;
    if (begin >= end) return v;
    v.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) v.push_back(rows[i].ms[static_cast<size_t>(variant)]);
    return v;
}

std::vector<double> Delta(const std::vector<SampleRow>& rows, int candidate, size_t begin = 0, size_t end = static_cast<size_t>(-1)) {
    end = (std::min)(end, rows.size());
    std::vector<double> v;
    if (begin >= end) return v;
    v.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) v.push_back(rows[i].ms[LAB37B] - rows[i].ms[static_cast<size_t>(candidate)]);
    return v;
}

std::vector<double> Speedup(const std::vector<SampleRow>& rows, int candidate) {
    std::vector<double> v;
    v.reserve(rows.size());
    for (const auto& r : rows) {
        const double base = r.ms[LAB37B];
        v.push_back(base > 0.0 ? 100.0 * (base - r.ms[static_cast<size_t>(candidate)]) / base : 0.0);
    }
    return v;
}

unsigned int Wins(const std::vector<SampleRow>& rows, int candidate) {
    unsigned int n = 0;
    for (const auto& r : rows) if (r.ms[static_cast<size_t>(candidate)] < r.ms[LAB37B]) ++n;
    return n;
}

void WriteRaw(const std::wstring& path, const std::vector<SampleRow>& rows) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) return;
    fprintf(f, "sample,order0,order1,order2,order3,order4,order5,lab37b_ms,lab38a_ms,lab38b_ms,lab38c_ms,lab38d_ms,k185_ms,lab37b_minus_lab38a_ms,lab37b_minus_lab38b_ms,lab37b_minus_lab38c_ms,lab37b_minus_lab38d_ms,lab38a_faster,lab38b_faster,lab38c_faster,lab38d_faster\r\n");
    for (const auto& r : rows) {
        fprintf(f, "%u,%s,%s,%s,%s,%s,%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%u,%u,%u,%u\r\n",
            r.index,
            Name(r.order[0]), Name(r.order[1]), Name(r.order[2]), Name(r.order[3]), Name(r.order[4]), Name(r.order[5]),
            r.ms[LAB37B], r.ms[LAB38A], r.ms[LAB38B], r.ms[LAB38C], r.ms[LAB38D], r.ms[K185],
            r.ms[LAB37B] - r.ms[LAB38A], r.ms[LAB37B] - r.ms[LAB38B], r.ms[LAB37B] - r.ms[LAB38C], r.ms[LAB37B] - r.ms[LAB38D],
            r.ms[LAB38A] < r.ms[LAB37B] ? 1u : 0u,
            r.ms[LAB38B] < r.ms[LAB37B] ? 1u : 0u,
            r.ms[LAB38C] < r.ms[LAB37B] ? 1u : 0u,
            r.ms[LAB38D] < r.ms[LAB37B] ? 1u : 0u);
    }
    fclose(f);
}

void WriteBlocks(const std::wstring& path, const std::vector<SampleRow>& rows) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) return;
    fprintf(f, "block,first_sample,last_sample,lab37b_median_ms,lab38a_median_ms,lab38b_median_ms,lab38c_median_ms,lab38d_median_ms,k185_median_ms,delta_a_median_ms,delta_b_median_ms,delta_c_median_ms,delta_d_median_ms\r\n");
    for (size_t s = 0; s < rows.size(); s += 60u) {
        const size_t e = (std::min)(s + 60u, rows.size());
        fprintf(f, "%u,%u,%u,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\r\n",
            static_cast<unsigned int>(s / 60u), static_cast<unsigned int>(s), static_cast<unsigned int>(e - 1u),
            Median(Values(rows, LAB37B, s, e)), Median(Values(rows, LAB38A, s, e)), Median(Values(rows, LAB38B, s, e)),
            Median(Values(rows, LAB38C, s, e)), Median(Values(rows, LAB38D, s, e)), Median(Values(rows, K185, s, e)),
            Median(Delta(rows, LAB38A, s, e)), Median(Delta(rows, LAB38B, s, e)), Median(Delta(rows, LAB38C, s, e)), Median(Delta(rows, LAB38D, s, e)));
    }
    fclose(f);
}

void WriteSummary(const std::wstring& path, const std::wstring& adapter, const std::vector<SampleRow>& rows) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") || !f) return;
    const std::string ad = Utf8(adapter);
    const size_t half = rows.size() / 2u;
    fprintf(f, "{\r\n");
    fprintf(f, "  \"protocol\": \"LAB38_GTX960M_SIXWAY_WILLIAMS_COMPARE\",\r\n");
    fprintf(f, "  \"adapter\": \"%s\",\r\n", ad.c_str());
    fprintf(f, "  \"input\": \"1280x720\", \"output\": \"1920x1080\",\r\n");
    fprintf(f, "  \"samples\": %u, \"warmup_groups\": %u,\r\n", static_cast<unsigned int>(rows.size()), kWarmupGroups);
    fprintf(f, "  \"order_protocol\": \"SIX_SEQUENCE_WILLIAMS_BALANCED_LATIN_SQUARE\",\r\n");
    fprintf(f, "  \"medians_ms\": {\"LAB37B\": %.9f, \"LAB38A\": %.9f, \"LAB38B\": %.9f, \"LAB38C\": %.9f, \"LAB38D\": %.9f, \"K185\": %.9f},\r\n",
        Median(Values(rows, LAB37B)), Median(Values(rows, LAB38A)), Median(Values(rows, LAB38B)), Median(Values(rows, LAB38C)), Median(Values(rows, LAB38D)), Median(Values(rows, K185)));
    fprintf(f, "  \"paired_delta_median_ms\": {\"LAB38A\": %.9f, \"LAB38B\": %.9f, \"LAB38C\": %.9f, \"LAB38D\": %.9f},\r\n",
        Median(Delta(rows, LAB38A)), Median(Delta(rows, LAB38B)), Median(Delta(rows, LAB38C)), Median(Delta(rows, LAB38D)));
    fprintf(f, "  \"paired_speedup_median_percent\": {\"LAB38A\": %.9f, \"LAB38B\": %.9f, \"LAB38C\": %.9f, \"LAB38D\": %.9f},\r\n",
        Median(Speedup(rows, LAB38A)), Median(Speedup(rows, LAB38B)), Median(Speedup(rows, LAB38C)), Median(Speedup(rows, LAB38D)));
    fprintf(f, "  \"candidate_wins\": {\"LAB38A\": %u, \"LAB38B\": %u, \"LAB38C\": %u, \"LAB38D\": %u},\r\n",
        Wins(rows, LAB38A), Wins(rows, LAB38B), Wins(rows, LAB38C), Wins(rows, LAB38D));
    fprintf(f, "  \"first_half_medians_ms\": {\"LAB37B\": %.9f, \"LAB38A\": %.9f, \"LAB38B\": %.9f, \"LAB38C\": %.9f, \"LAB38D\": %.9f, \"K185\": %.9f},\r\n",
        Median(Values(rows, LAB37B, 0, half)), Median(Values(rows, LAB38A, 0, half)), Median(Values(rows, LAB38B, 0, half)), Median(Values(rows, LAB38C, 0, half)), Median(Values(rows, LAB38D, 0, half)), Median(Values(rows, K185, 0, half)));
    fprintf(f, "  \"second_half_medians_ms\": {\"LAB37B\": %.9f, \"LAB38A\": %.9f, \"LAB38B\": %.9f, \"LAB38C\": %.9f, \"LAB38D\": %.9f, \"K185\": %.9f},\r\n",
        Median(Values(rows, LAB37B, half, rows.size())), Median(Values(rows, LAB38A, half, rows.size())), Median(Values(rows, LAB38B, half, rows.size())), Median(Values(rows, LAB38C, half, rows.size())), Median(Values(rows, LAB38D, half, rows.size())), Median(Values(rows, K185, half, rows.size())));
    fprintf(f, "  \"hashes\": {\"LAB37B\": \"%s\", \"LAB38A\": \"%s\", \"LAB38B\": \"%s\", \"LAB38C\": \"%s\", \"LAB38D\": \"%s\", \"K185\": \"%s\"}\r\n",
        kShaderSha256[LAB37B], kShaderSha256[LAB38A], kShaderSha256[LAB38B], kShaderSha256[LAB38C], kShaderSha256[LAB38D], kShaderSha256[K185]);
    fprintf(f, "}\r\n");
    fclose(f);
}

} // namespace

bool EnsureDir(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

void WriteReports(const std::wstring& outDir, const std::wstring& adapter, const std::vector<SampleRow>& rows) {
    WriteRaw(Join(outDir, L"LAB38_COMPARE_RAW.csv"), rows);
    WriteBlocks(Join(outDir, L"BLOCKS_60.csv"), rows);
    WriteSummary(Join(outDir, L"LAB38_COMPARE_SUMMARY.json"), adapter, rows);
}

void PrintConsoleSummary(const std::vector<SampleRow>& rows) {
    std::cout << std::fixed << std::setprecision(6);
    for (int candidate = LAB38A; candidate <= LAB38D; ++candidate) {
        std::cout << "[RESULT] LAB37B-" << Name(candidate)
                  << " paired median ms=" << Median(Delta(rows, candidate))
                  << " speedup median=" << Median(Speedup(rows, candidate)) << "%"
                  << " wins=" << Wins(rows, candidate) << "/" << rows.size() << std::endl;
    }
}

} // namespace ptar_lab38
