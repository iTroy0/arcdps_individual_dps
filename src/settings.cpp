#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

namespace idps {

Settings& settings() {
    static Settings s;
    return s;
}

namespace {
    std::string ini_path() {
        char path[MAX_PATH]{};
        HMODULE self = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&ini_path), &self);
        DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return "arcdps_individual_dps.ini";
        std::string p(path, n);
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos) p.resize(slash);
        p += "\\arcdps_individual_dps.ini";
        return p;
    }

    void trim(std::string& s) {
        size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
        size_t b = s.size();
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
        s = s.substr(a, b - a);
    }

    void apply_kv(const std::string& k, const std::string& v) {
        auto& s = settings();
        if (k == "exclude_npcs")    s.exclude_npcs    = std::atoi(v.c_str()) != 0;
        else if (k == "exclude_gadgets") s.exclude_gadgets = std::atoi(v.c_str()) != 0;
        else if (k == "window_open") s.window_open = std::atoi(v.c_str()) != 0;
        else if (k == "window_x")    s.window_x = static_cast<float>(std::atof(v.c_str()));
        else if (k == "window_y")    s.window_y = static_cast<float>(std::atof(v.c_str()));
        else if (k == "window_w")    s.window_w = static_cast<float>(std::atof(v.c_str()));
        else if (k == "window_h")    s.window_h = static_cast<float>(std::atof(v.c_str()));
        else if (k == "sort_mode") {
            int mode = std::atoi(v.c_str());
            if (mode < 0) mode = 0;
            if (mode > 3) mode = 3;
            s.sort_mode = mode;
        }
        else if (k == "cleanses_open") s.cleanses_open = std::atoi(v.c_str()) != 0;
        else if (k == "strips_open")   s.strips_open   = std::atoi(v.c_str()) != 0;
        else if (k == "detail_open")   s.detail_open   = std::atoi(v.c_str()) != 0;
        else if (k == "detail_x")      s.detail_x      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "detail_y")      s.detail_y      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "detail_w")      s.detail_w      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "detail_h")      s.detail_h      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "window_alpha") {
            float a = static_cast<float>(std::atof(v.c_str()));
            if (a < 0.10f) a = 0.10f;
            if (a > 1.00f) a = 1.00f;
            s.window_alpha = a;
        }
    }
}

void settings_load() {
    FILE* f = nullptr;
    if (fopen_s(&f, ini_path().c_str(), "r") != 0 || !f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s = line;
        trim(s);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq);
        std::string v = s.substr(eq + 1);
        trim(k); trim(v);
        apply_kv(k, v);
    }
    std::fclose(f);
}

void settings_save() {
    FILE* f = nullptr;
    if (fopen_s(&f, ini_path().c_str(), "w") != 0 || !f) return;
    const auto& s = settings();
    std::fprintf(f, "# arcdps_individual_dps settings\n");
    std::fprintf(f, "exclude_npcs=%d\n",    s.exclude_npcs    ? 1 : 0);
    std::fprintf(f, "exclude_gadgets=%d\n", s.exclude_gadgets ? 1 : 0);
    std::fprintf(f, "window_open=%d\n",     s.window_open     ? 1 : 0);
    std::fprintf(f, "window_x=%.1f\n",      s.window_x);
    std::fprintf(f, "window_y=%.1f\n",      s.window_y);
    std::fprintf(f, "window_w=%.1f\n",      s.window_w);
    std::fprintf(f, "window_h=%.1f\n",      s.window_h);
    std::fprintf(f, "sort_mode=%d\n",       s.sort_mode);
    std::fprintf(f, "cleanses_open=%d\n",   s.cleanses_open ? 1 : 0);
    std::fprintf(f, "strips_open=%d\n",     s.strips_open   ? 1 : 0);
    std::fprintf(f, "detail_open=%d\n",     s.detail_open   ? 1 : 0);
    std::fprintf(f, "detail_x=%.1f\n",      s.detail_x);
    std::fprintf(f, "detail_y=%.1f\n",      s.detail_y);
    std::fprintf(f, "detail_w=%.1f\n",      s.detail_w);
    std::fprintf(f, "detail_h=%.1f\n",      s.detail_h);
    std::fprintf(f, "window_alpha=%.2f\n",  s.window_alpha);
    std::fclose(f);
}

} // namespace idps
