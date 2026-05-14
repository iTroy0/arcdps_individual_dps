#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <windows.h>

#include "util.h"

namespace idps {

namespace {
    FILE*      g_file = nullptr;
    std::mutex g_mutex;

    void write_timestamp(FILE* f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::fprintf(f, "[%02u:%02u:%02u.%03u] ",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }

    // Anchor the log next to the DLL (same as ini_path / default_icons_dir),
    // not the process CWD — GW2's CWD is the game root, not the addon dir.
    std::string log_path() {
        char path[MAX_PATH]{};
        HMODULE self = self_module();
        DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return "arcdps_individual_dps.log";
        std::string p(path, n);
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) p.resize(slash);
        p += "\\arcdps_individual_dps.log";
        return p;
    }
}

void log_init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) return;
    // Truncate mode: the log resets every launch, so it can never grow
    // across sessions no matter how many error lines a session writes.
    // log_init is idempotent (guarded above) so the truncate happens once
    // per load. No session-start marker — log_line timestamps each entry.
    fopen_s(&g_file, log_path().c_str(), "w");
}

void log_shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void log_line(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file) return;
    write_timestamp(g_file);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_file, fmt, args);
    va_end(args);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}

} // namespace idps
