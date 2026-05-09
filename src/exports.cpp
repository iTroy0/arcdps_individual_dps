#include "exports.h"

#include <imgui.h>
#include <windows.h>

#include "arcdps_api.h"
#include "combat.h"
#include "icons.h"
#include "log.h"
#include "settings.h"
#include "tracker.h"
#include "ui.h"
#include "ui_detail.h"

namespace idps {

namespace {
    arcdps_exports g_exports{};
    constexpr uint32_t kPluginSig = 0x1D9A2E01u; // "individual dps" — avoid collisions
    constexpr const char* kName    = "individual_dps";
    constexpr const char* kVersion = "0.6.0";
    constexpr const char* kBuild   = "0.6.0 (" __DATE__ " " __TIME__ ")";

    // arc's raw WndProc hook. Return convention: `msg` = pass through,
    // 0 = consume so neither arc nor GW2 see it. Returning 0 in the
    // default branch would block every click and keystroke from
    // reaching the game — only consume the specific message we handle.
    // ESC is grabbed at arc's keybind layer before reaching ImGui, so
    // closing the detail window has to happen here.
    uintptr_t mod_wnd_nofilter(HWND /*hwnd*/, UINT msg,
                               WPARAM wparam, LPARAM /*lparam*/) {
        if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) {
            if (consume_esc_for_detail()) return 0;
        }
        return msg;
    }
}

const char* version() { return kVersion; }

arcdps_exports* mod_init() {
    log_init();
    log_line("mod_init name=%s build=%s", kName, kBuild);

    settings_load();
    const auto& s = settings();
    options().exclude_npcs.store(s.exclude_npcs, std::memory_order_relaxed);
    options().exclude_gadgets.store(s.exclude_gadgets, std::memory_order_relaxed);

    g_exports.size         = sizeof(arcdps_exports);
    g_exports.sig          = kPluginSig;
    g_exports.imguivers    = IMGUI_VERSION_NUM;
    g_exports.out_name     = kName;
    g_exports.out_build    = kBuild;
    g_exports.combat       = reinterpret_cast<void*>(&mod_combat);
    g_exports.imgui        = reinterpret_cast<void*>(&mod_imgui);
    g_exports.options_tab  = reinterpret_cast<void*>(&mod_options_end);
    // combat_local is a strict duplicate of combat for self events — arc fires
    // the same payload on both callbacks. Keeping the dispatch wired would
    // double-count damage and combat time. Null pointer = arc skips the call.
    g_exports.combat_local    = nullptr;
    g_exports.wnd_nofilter    = reinterpret_cast<void*>(&mod_wnd_nofilter);
    g_exports.wnd_filter      = nullptr;
    g_exports.options_windows = nullptr;
    return &g_exports;
}

uintptr_t mod_release() {
    auto& s = settings();
    s.exclude_npcs    = options().exclude_npcs.load(std::memory_order_relaxed);
    s.exclude_gadgets = options().exclude_gadgets.load(std::memory_order_relaxed);
    settings_save();

    log_line("mod_release");
    icons_shutdown();
    log_shutdown();
    return 0;
}

} // namespace idps
