#include "exports.h"

#include <imgui.h>
#include <string>
#include <windows.h>

#include "arc_exports.h"
#include "arcdps_api.h"
#include "combat.h"
#include "icons.h"
#include "log.h"
#include "settings.h"
#include "tracker.h"
#include "ui.h"
#include "ui_detail.h"
#include "ui_options.h"
#include "update.h"

namespace idps {

namespace {
    arcdps_exports g_exports{};
    constexpr uint32_t kPluginSig = 0x1D9A2E01u; // "individual dps" — avoid collisions
    // Single source of truth for the version string. Bump this one line
    // per release. kVersion is read at runtime (idps::version(), the
    // get_update_url semver compare); kBuild is arc's out_build display.
    #define IDPS_VERSION "0.9.0"
    constexpr const char* kName    = "individual_dps";
    constexpr const char* kVersion = IDPS_VERSION;
    constexpr const char* kBuild   = IDPS_VERSION " (" __DATE__ " " __TIME__ ")";
    #undef IDPS_VERSION

    bool        g_update_banner   = false;
    std::string g_update_previous;

    // arc's raw WndProc hook. Return convention: `msg` = pass through,
    // 0 = consume so neither arc nor GW2 see it. Returning 0 in the
    // default branch would block every click and keystroke from
    // reaching the game — only consume the specific message we handle.
    // ESC is grabbed at arc's keybind layer before reaching ImGui, so
    // closing the detail window has to happen here.
    uintptr_t mod_wnd_nofilter(HWND /*hwnd*/, UINT msg,
                               WPARAM wparam, LPARAM lparam) {
        // Bit 30 of lparam is the previous-key-state flag: set when the
        // key was already down. Skip auto-repeats so a held ESC doesn't
        // toggle the detail-close request 30+ times a second.
        if (msg == WM_KEYDOWN && wparam == VK_ESCAPE &&
            !(lparam & 0x40000000)) {
            // Only when the user actually wants ESC to close windows —
            // arcdps exposes that preference in its UI-settings mask, and
            // swallowing ESC against their wish would break the game menu.
            if (arc_ui_close_with_esc() && consume_esc_for_detail()) return 0;
        }
        return msg;
    }

    // Compare ini's last_seen_version against the kVersion baked into
    // this freshly-loaded DLL. A strictly newer kVersion means arc just
    // swapped us in via get_update_url; record the previous version,
    // raise the banner flag, and persist the new baseline so the banner
    // fires only once per upgrade. First run (empty ini field) just
    // seeds the baseline without nagging.
    void detect_post_update_banner() {
        auto& s = settings();
        if (s.last_seen_version.empty()) {
            s.last_seen_version = kVersion;
            settings_save();
            return;
        }
        if (s.last_seen_version == kVersion) return;
        if (compare_semver(kVersion, s.last_seen_version.c_str()) > 0) {
            g_update_previous = s.last_seen_version;
            g_update_banner   = true;
        }
        s.last_seen_version = kVersion;
        settings_save();
    }
}

const char* version() { return kVersion; }

bool        update_banner_visible()      { return g_update_banner; }
const char* update_banner_prev_version() { return g_update_previous.c_str(); }
void        update_banner_dismiss()      { g_update_banner = false; }

arcdps_exports* mod_init() {
    log_init();

    settings_load();
    detect_post_update_banner();
    const auto& s = settings();
    options().exclude_npcs.store(s.exclude_npcs, std::memory_order_relaxed);
    options().exclude_gadgets.store(s.exclude_gadgets, std::memory_order_relaxed);
    options().fight_gap_enabled.store(s.fight_gap_enabled,
                                      std::memory_order_relaxed);
    options().fight_gap_ms.store(
        static_cast<uint32_t>(s.fight_gap_seconds) * 1000u,
        std::memory_order_relaxed);

    g_exports.size         = sizeof(arcdps_exports);
    g_exports.sig          = kPluginSig;
    g_exports.imguivers    = IMGUI_VERSION_NUM;
    g_exports.out_name     = kName;
    g_exports.out_build    = kBuild;
    g_exports.combat       = reinterpret_cast<void*>(&mod_combat);
    g_exports.imgui        = reinterpret_cast<void*>(&mod_imgui);
    g_exports.options_tab  = reinterpret_cast<void*>(&mod_options_end);
    // combat_local is arc's chatbox-only event stream (per arc api README:
    // "same as combat, but for chatbox events"). Tracker has no chat-side
    // use, so leave it null — arc skips the dispatch entirely.
    g_exports.combat_local    = nullptr;
    g_exports.wnd_nofilter    = reinterpret_cast<void*>(&mod_wnd_nofilter);
    g_exports.wnd_filter      = nullptr;
    // Contributes this plugin's windows to arcdps's own window list, so
    // they can be toggled from the same place as arc's.
    g_exports.options_windows = reinterpret_cast<void*>(&mod_options_windows);
    return &g_exports;
}

uintptr_t mod_release() {
    auto& s = settings();
    s.exclude_npcs    = options().exclude_npcs.load(std::memory_order_relaxed);
    s.exclude_gadgets = options().exclude_gadgets.load(std::memory_order_relaxed);
    settings_save();

    icons_shutdown();
    log_shutdown();
    return 0;
}

} // namespace idps
