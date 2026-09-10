#pragma once

#include <string>
#include <vector>

#include "Lab38Variants.h"

namespace ptar_lab38 {

bool EnsureDir(const std::wstring& path);
void WriteReports(const std::wstring& outDir, const std::wstring& adapter, const std::vector<SampleRow>& rows);
void PrintConsoleSummary(const std::vector<SampleRow>& rows);

} // namespace ptar_lab38
