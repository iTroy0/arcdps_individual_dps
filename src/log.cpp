#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <windows.h>

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
}

void log_init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) return;
    fopen_s(&g_file, "arcdps_individual_dps.log", "a");
    if (g_file) {
        write_timestamp(g_file);
        std::fprintf(g_file, "log_init\n");
        std::fflush(g_file);
    }
}

void log_shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        write_timestamp(g_file);
        std::fprintf(g_file, "log_shutdown\n");
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
