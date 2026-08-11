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
#include "log.h"
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
        else if (k == "fight_gap_enabled") s.fight_gap_enabled = std::atoi(v.c_str()) != 0;
        else if (k == "fight_gap_seconds") {
            int sec = std::atoi(v.c_str());
            if (sec < 1)  sec = 1;
            if (sec > 60) sec = 60;
            s.fight_gap_seconds = sec;
        }
        else if (k == "window_open") s.window_open = std::atoi(v.c_str()) != 0;
        else if (k == "window_x")    s.window_x = static_cast<float>(std::atof(v.c_str()));
        else if (k == "window_y")    s.window_y = static_cast<float>(std::atof(v.c_str()));
        else if (k == "window_w")    s.window_w = static_cast<float>(std::atof(v.c_str()));
        else if (k == "window_h")    s.window_h = static_cast<float>(std::atof(v.c_str()));
        else if (k == "sort_mode") {
            int mode = std::atoi(v.c_str());
            if (mode < 0) mode = 0;
            if (mode > 4) mode = 4;
            s.sort_mode = mode;
        }
        else if (k == "sort_reverse") s.sort_reverse = std::atoi(v.c_str()) != 0;
        else if (k == "cleanses_open") s.cleanses_open = std::atoi(v.c_str()) != 0;
        else if (k == "strips_open")   s.strips_open   = std::atoi(v.c_str()) != 0;
        else if (k == "downs_open")    s.downs_open    = std::atoi(v.c_str()) != 0;
        else if (k == "highlight_self") s.highlight_self = std::atoi(v.c_str()) != 0;
        else if (k == "self_name_gold") s.self_name_gold = std::atoi(v.c_str()) != 0;
        else if (k == "self_pin_top")   s.self_pin_top   = std::atoi(v.c_str()) != 0;
        else if (k == "use_arc_colors")  s.use_arc_colors  = std::atoi(v.c_str()) != 0;
        else if (k == "show_subgroup")   s.show_subgroup   = std::atoi(v.c_str()) != 0;
        else if (k == "responsive_columns") s.responsive_columns = std::atoi(v.c_str()) != 0;
        else if (k == "body_borders")   s.body_borders   = std::atoi(v.c_str()) != 0;
        else if (k == "show_headers")   s.show_headers   = std::atoi(v.c_str()) != 0;
        else if (k == "downs_sort") {
            int c = std::atoi(v.c_str());
            s.downs_sort = (c >= 1 && c <= 4) ? c : 2;
        }
        else if (k == "downs_sort_asc")   s.downs_sort_asc   = std::atoi(v.c_str()) != 0;
        else if (k == "show_totals")    s.show_totals    = std::atoi(v.c_str()) != 0;
        else if (k == "lock_windows")   s.lock_windows   = std::atoi(v.c_str()) != 0;
        else if (k == "chart_smooth")   s.chart_smooth   = std::atoi(v.c_str()) != 0;
        else if (k == "chart_cum")      s.chart_cum      = std::atoi(v.c_str()) != 0;
        else if (k == "chart_avg")      s.chart_avg      = std::atoi(v.c_str()) != 0;
        else if (k == "chart_burst")    s.chart_burst    = std::atoi(v.c_str()) != 0;
        else if (k == "detail_open")   s.detail_open   = std::atoi(v.c_str()) != 0;
        else if (k == "detail_x")      s.detail_x      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "detail_y")      s.detail_y      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "detail_w")      s.detail_w      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "detail_h")      s.detail_h      = static_cast<float>(std::atof(v.c_str()));
        else if (k == "bar_alpha") {
            float a = static_cast<float>(std::atof(v.c_str()));
            if (a < 0.15f) a = 0.15f;
            if (a > 1.00f) a = 1.00f;
            s.bar_alpha = a;
        }
        else if (k == "window_alpha") {
            float a = static_cast<float>(std::atof(v.c_str()));
            if (a < 0.10f) a = 0.10f;
            if (a > 1.00f) a = 1.00f;
            s.window_alpha = a;
        }
        else if (k == "last_seen_version") s.last_seen_version = v;
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

namespace {

    // Content of the most recent successful write. Lets settings_autosave
    // skip the disk when nothing changed (the common case every 15s tick).
    std::string g_last_written;

    std::string serialize_settings() {
        const auto& s = settings();
        char buf[4096];
        int  off = 0;
        auto put = [&](const char* fmt, auto... args) {
            if (off < 0 || off >= static_cast<int>(sizeof(buf))) return;
            int n = std::snprintf(buf + off, sizeof(buf) - off, fmt, args...);
            if (n > 0) off += n;
        };
        put("# arcdps_individual_dps settings\n");
        put("exclude_npcs=%d\n",    s.exclude_npcs    ? 1 : 0);
        put("exclude_gadgets=%d\n", s.exclude_gadgets ? 1 : 0);
        put("fight_gap_enabled=%d\n", s.fight_gap_enabled ? 1 : 0);
        put("fight_gap_seconds=%d\n", s.fight_gap_seconds);
        put("window_open=%d\n",     s.window_open     ? 1 : 0);
        put("window_x=%.1f\n",      s.window_x);
        put("window_y=%.1f\n",      s.window_y);
        put("window_w=%.1f\n",      s.window_w);
        put("window_h=%.1f\n",      s.window_h);
        put("sort_mode=%d\n",       s.sort_mode);
        put("sort_reverse=%d\n",    s.sort_reverse ? 1 : 0);
        put("cleanses_open=%d\n",   s.cleanses_open ? 1 : 0);
        put("strips_open=%d\n",     s.strips_open   ? 1 : 0);
        put("downs_open=%d\n",      s.downs_open    ? 1 : 0);
        put("highlight_self=%d\n",  s.highlight_self ? 1 : 0);
        put("self_name_gold=%d\n",  s.self_name_gold ? 1 : 0);
        put("self_pin_top=%d\n",    s.self_pin_top   ? 1 : 0);
        put("use_arc_colors=%d\n",  s.use_arc_colors  ? 1 : 0);
        put("show_subgroup=%d\n",   s.show_subgroup   ? 1 : 0);
        put("responsive_columns=%d\n", s.responsive_columns ? 1 : 0);
        put("body_borders=%d\n",    s.body_borders   ? 1 : 0);
        put("show_headers=%d\n",    s.show_headers   ? 1 : 0);
        put("downs_sort=%d\n",      s.downs_sort);
        put("downs_sort_asc=%d\n",  s.downs_sort_asc   ? 1 : 0);
        put("show_totals=%d\n",     s.show_totals    ? 1 : 0);
        put("lock_windows=%d\n",    s.lock_windows   ? 1 : 0);
        put("chart_smooth=%d\n",    s.chart_smooth   ? 1 : 0);
        put("chart_cum=%d\n",       s.chart_cum      ? 1 : 0);
        put("chart_avg=%d\n",       s.chart_avg      ? 1 : 0);
        put("chart_burst=%d\n",     s.chart_burst    ? 1 : 0);
        put("detail_open=%d\n",     s.detail_open   ? 1 : 0);
        put("detail_x=%.1f\n",      s.detail_x);
        put("detail_y=%.1f\n",      s.detail_y);
        put("detail_w=%.1f\n",      s.detail_w);
        put("detail_h=%.1f\n",      s.detail_h);
        put("window_alpha=%.2f\n",  s.window_alpha);
        put("bar_alpha=%.2f\n",     s.bar_alpha);
        put("last_seen_version=%s\n", s.last_seen_version.c_str());
        return std::string(buf, static_cast<size_t>(off));
    }

    void write_settings_file(const std::string& content) {
        // Atomic write: stream to <path>.tmp, fclose, then MoveFileEx with
        // REPLACE_EXISTING so a crash mid-write can't truncate the live ini
        // and silently restore defaults on next load.
        std::string final_path = ini_path();
        std::string tmp_path   = final_path + ".tmp";
        FILE* f = nullptr;
        if (fopen_s(&f, tmp_path.c_str(), "wb") != 0 || !f) return;
        std::fwrite(content.data(), 1, content.size(), f);
        std::fflush(f);
        std::fclose(f);
        if (!MoveFileExA(tmp_path.c_str(), final_path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            // Move failed (destination locked, cross-volume, ...). The live
            // ini is untouched; drop the orphaned .tmp so it isn't mistaken
            // for a real file later.
            log_line("settings_save: MoveFileEx failed (err=%lu)", GetLastError());
            DeleteFileA(tmp_path.c_str());
            return;
        }
        g_last_written = content;
    }
}

void settings_save() {
    ScopedCNumericLocale c_locale;
    write_settings_file(serialize_settings());
}

void settings_autosave() {
    ScopedCNumericLocale c_locale;
    std::string content = serialize_settings();
    if (content == g_last_written) return;
    write_settings_file(content);
}

} // namespace idps
