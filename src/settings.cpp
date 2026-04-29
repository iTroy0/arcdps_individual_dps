#include "settings.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale.h>
#include <string>
#include <windows.h>
#include "util.h"

namespace idps {

Settings& settings() {
    static Settings s;
    return s;
}

namespace {

    // Forces LC_NUMERIC = "C" for the current thread so fprintf("%.1f") and
    // atof() use a "." decimal separator regardless of the user's system
    // locale. Without this, German / French / etc. locales serialize floats
    // as "1,5", and atof on reload stops at the comma and returns 1.0,
    // silently corrupting window geometry and alpha. Restored on scope exit.
    class ScopedCNumericLocale {
    public:
        ScopedCNumericLocale() {
            prev_mode_ = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
            if (const char* p = std::setlocale(LC_NUMERIC, nullptr)) prev_ = p;
            std::setlocale(LC_NUMERIC, "C");
        }
        ~ScopedCNumericLocale() {
            if (!prev_.empty()) std::setlocale(LC_NUMERIC, prev_.c_str());
            if (prev_mode_ != -1) _configthreadlocale(prev_mode_);
        }
        ScopedCNumericLocale(const ScopedCNumericLocale&)            = delete;
        ScopedCNumericLocale& operator=(const ScopedCNumericLocale&) = delete;
    private:
        int         prev_mode_ = -1;
        std::string prev_;
    };

    std::string ini_path() {
        char path[MAX_PATH]{};
        HMODULE self = self_module();
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
        else if (k == "sort_reverse") s.sort_reverse = std::atoi(v.c_str()) != 0;
        else if (k == "cleanses_open") s.cleanses_open = std::atoi(v.c_str()) != 0;
        else if (k == "strips_open")   s.strips_open   = std::atoi(v.c_str()) != 0;
        else if (k == "highlight_self") s.highlight_self = std::atoi(v.c_str()) != 0;
        else if (k == "name_white")     s.name_white     = std::atoi(v.c_str()) != 0;
        else if (k == "responsive_columns") s.responsive_columns = std::atoi(v.c_str()) != 0;
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

    // Clamp window geometry after load so a corrupted ini can't park a
    // window off-screen where the user can't drag it back. The position
    // range allows slight off-screen for multi-monitor setups; size has a
    // hard floor so collapsed windows stay grabbable.
    void clamp_geometry() {
        auto& s = settings();
        auto cx  = [](float& v) { if (v < -1024.0f) v = -1024.0f; if (v > 16384.0f) v = 16384.0f; };
        auto csz = [](float& v, float def) {
            if (!std::isfinite(v) || v < 100.0f) v = def;
            if (v > 8192.0f) v = 8192.0f;
        };
        // x/y == -1.0 is the "uninitialized" sentinel — leave alone.
        if (s.window_x != -1.0f) cx(s.window_x);
        if (s.window_y != -1.0f) cx(s.window_y);
        if (s.detail_x != -1.0f) cx(s.detail_x);
        if (s.detail_y != -1.0f) cx(s.detail_y);
        csz(s.window_w, 380.0f);
        csz(s.window_h, 260.0f);
        csz(s.detail_w, 420.0f);
        csz(s.detail_h, 420.0f);
    }
}

void settings_load() {
    ScopedCNumericLocale c_locale;
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
    clamp_geometry();
}

void settings_save() {
    ScopedCNumericLocale c_locale;
    // Atomic write: stream to <path>.tmp, fclose, then MoveFileEx with
    // REPLACE_EXISTING so a crash mid-write can't truncate the live ini
    // and silently restore defaults on next load.
    std::string final_path = ini_path();
    std::string tmp_path   = final_path + ".tmp";
    FILE* f = nullptr;
    if (fopen_s(&f, tmp_path.c_str(), "w") != 0 || !f) return;
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
    std::fprintf(f, "sort_reverse=%d\n",    s.sort_reverse ? 1 : 0);
    std::fprintf(f, "cleanses_open=%d\n",   s.cleanses_open ? 1 : 0);
    std::fprintf(f, "strips_open=%d\n",     s.strips_open   ? 1 : 0);
    std::fprintf(f, "highlight_self=%d\n",  s.highlight_self ? 1 : 0);
    std::fprintf(f, "name_white=%d\n",      s.name_white     ? 1 : 0);
    std::fprintf(f, "responsive_columns=%d\n", s.responsive_columns ? 1 : 0);
    std::fprintf(f, "detail_open=%d\n",     s.detail_open   ? 1 : 0);
    std::fprintf(f, "detail_x=%.1f\n",      s.detail_x);
    std::fprintf(f, "detail_y=%.1f\n",      s.detail_y);
    std::fprintf(f, "detail_w=%.1f\n",      s.detail_w);
    std::fprintf(f, "detail_h=%.1f\n",      s.detail_h);
    std::fprintf(f, "window_alpha=%.2f\n",  s.window_alpha);
    std::fflush(f);
    std::fclose(f);
    MoveFileExA(tmp_path.c_str(), final_path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

} // namespace idps
