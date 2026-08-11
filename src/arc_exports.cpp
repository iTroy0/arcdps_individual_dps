#include "arc_exports.h"

#include <cstdarg>
#include <cstdio>
#include <windows.h>

#include "arcdps_api.h"
#include "log.h"

namespace idps {

namespace {

    using e0_fn = wchar_t* (*)();
    using e3_fn = void     (*)(const char*);
    using e5_fn = void     (*)(ImVec4**);
    using e6_fn = uint64_t (*)();
    using e7_fn = uint64_t (*)();

    HMODULE g_arc = nullptr;
    e0_fn   g_e0  = nullptr;
    e3_fn   g_e3  = nullptr;
    e5_fn   g_e5  = nullptr;
    e6_fn   g_e6  = nullptr;
    e7_fn   g_e7  = nullptr;

    // Filled by e5. Index order per the api README:
    //   0 core, 1 prof base, 2 prof highlight, 3 subgroup base,
    //   4 subgroup highlight.
    // arcdps owns the memory and mutates it in place when the user edits
    // colours, so we keep the pointers rather than copying the values.
    ImVec4* g_cols[5] = {};
    bool    g_cols_ok = false;

    // Table extents. The README pins core to CCOL_NUM and says prof indices
    // "match prof enum, 0 unknown"; subgroups run to the game maximum of 15.
    // Reading past these would walk off arcdps's arrays, so every accessor
    // bounds-checks against them.
    constexpr uint32_t kProfMax     = 9;
    constexpr uint32_t kSubgroupMax = 15;

    // A colour arcdps left fully transparent would render the row invisible.
    // Treat that as "not supplied" so the caller's palette wins instead.
    constexpr float kMinAlpha = 0.10f;

} // namespace

void arc_bind(void* arcdll) {
    HMODULE mod = static_cast<HMODULE>(arcdll);
    if (!mod || mod == g_arc) return;
    g_arc = mod;

    g_e0 = reinterpret_cast<e0_fn>(GetProcAddress(mod, "e0"));
    g_e3 = reinterpret_cast<e3_fn>(GetProcAddress(mod, "e3"));
    g_e5 = reinterpret_cast<e5_fn>(GetProcAddress(mod, "e5"));
    g_e6 = reinterpret_cast<e6_fn>(GetProcAddress(mod, "e6"));
    g_e7 = reinterpret_cast<e7_fn>(GetProcAddress(mod, "e7"));

    if (g_e5) {
        g_e5(g_cols);
        // arcdps may leave individual slots null. Colour lookups need at
        // least the two prof tables to be worth consulting.
        g_cols_ok = g_cols[1] != nullptr && g_cols[2] != nullptr;
    }

    log_line("arc_exports: e0=%d e3=%d e5=%d e6=%d e7=%d colors=%d",
             g_e0 ? 1 : 0, g_e3 ? 1 : 0, g_e5 ? 1 : 0,
             g_e6 ? 1 : 0, g_e7 ? 1 : 0, g_cols_ok ? 1 : 0);
}

void arc_log(const char* fmt, ...) {
    if (!g_e3) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_e3(buf);
}

uint64_t arc_ui_flags() {
    return g_e6 ? g_e6() : 0ull;
}

bool arc_ui_close_with_esc() {
    return (arc_ui_flags() & (1ull << UI_CLOSE_WITH_ESC)) != 0;
}

bool arc_colors_available() { return g_cols_ok; }

namespace {
    bool read_color(int table, uint32_t idx, uint32_t max_idx, ImVec4& out) {
        if (!g_cols_ok || idx > max_idx) return false;
        const ImVec4* arr = g_cols[table];
        if (!arr) return false;
        const ImVec4& c = arr[idx];
        if (c.w < kMinAlpha) return false;
        out = c;
        return true;
    }
}

bool arc_core_color(uint32_t idx, ImVec4& out) {
    return read_color(0, idx, CCOL_NUM - 1, out);
}

bool arc_prof_color(uint32_t prof, bool highlight, ImVec4& out) {
    return read_color(highlight ? 2 : 1, prof, kProfMax, out);
}

bool arc_subgroup_color(uint32_t subgroup, bool highlight, ImVec4& out) {
    return read_color(highlight ? 4 : 3, subgroup, kSubgroupMax, out);
}

std::wstring arc_ini_path() {
    if (!g_e0) return std::wstring();
    const wchar_t* p = g_e0();
    return p ? std::wstring(p) : std::wstring();
}

} // namespace idps
