#pragma once

#include <array>
#include <vector>

namespace ptar_lab38 {

enum Variant : int {
    LAB37B = 0,
    LAB38A = 1,
    LAB38B = 2,
    LAB38C = 3,
    LAB38D = 4,
    K185   = 5,
    VARIANT_COUNT = 6
};

static constexpr unsigned int kInW = 1280u;
static constexpr unsigned int kInH = 720u;
static constexpr unsigned int kOutW = 1920u;
static constexpr unsigned int kOutH = 1080u;
static constexpr unsigned int kWarmupGroups = 60u;
static constexpr unsigned int kSamples = 600u;
static constexpr unsigned long kQueryTimeoutMs = 1500u;
static constexpr unsigned int kNvidiaVendor = 0x10DEu;

static constexpr const char* kNames[VARIANT_COUNT] = {
    "LAB37B", "LAB38A", "LAB38B", "LAB38C", "LAB38D", "K185"
};

static constexpr const wchar_t* kShaderFiles[VARIANT_COUNT] = {
    L"lab37b.cso", L"lab38a.cso", L"lab38b.cso", L"lab38c.cso", L"lab38d.cso", L"ptar_k185_control_ps.cso"
};

static constexpr const char* kShaderSha256[VARIANT_COUNT] = {
    "cd92ead859b9e7e210b96b1b811c95f91b1eba3c18c64a7cfa83e0a004c5a0b5",
    "e50ff93fca833141fd02f266819c8d1c3700ecc92bee00239bfad5b500166570",
    "b05c573ebeb24754ee88abb6a319b5ed0f62667d4cb021cf285665c061497abc",
    "4147b5964118e4341fd560bce61f8fe01a1bec68943e08848a412913f02aeaa0",
    "12d25c573d00b23717c04c7104d63ca5365c47a3b917d40b67a747c03630cb5b",
    "6bd926e85f21dd08788ff9d189c472800a5e0eb726091158ed1ecde3d06d8c16"
};

static constexpr const char* kLabVsSha256 = "f5d7c0f9be164a924fbe821137fc7bb216c479abc5701411bd1a09346dd5078b";
static constexpr const char* kK185VsSha256 = "6328bbd87aac73b07d6112de593f781b2769381aae65fa9c2bcfa06fdc68585c";

// Williams balanced Latin square for six variants. Across one 6-row cycle,
// every variant occupies every position exactly once and every ordered
// adjacent pair occurs exactly once.
static constexpr int kOrders[VARIANT_COUNT][VARIANT_COUNT] = {
    {0,1,5,2,4,3},
    {1,2,0,3,5,4},
    {2,3,1,4,0,5},
    {3,4,2,5,1,0},
    {4,5,3,0,2,1},
    {5,0,4,1,3,2}
};

struct SampleRow {
    unsigned int index = 0;
    std::array<int, VARIANT_COUNT> order{};
    std::array<double, VARIANT_COUNT> ms{};
};

inline const char* Name(int variant) {
    return (variant >= 0 && variant < VARIANT_COUNT) ? kNames[variant] : "UNKNOWN";
}

} // namespace ptar_lab38
