#include "exports.h"

#include <imgui.h>

#include "combat.h"
#include "icons.h"
#include "log.h"
#include "settings.h"
#include "tracker.h"
#include "ui.h"

namespace idps {

namespace {
    arcdps_exports g_exports{};
    constexpr uint32_t kPluginSig = 0x1D9A2E01u; // "individual dps" — avoid collisions
    constexpr const char* kName    = "individual_dps";
    constexpr const char* kVersion = "0.3.0";
    constexpr const char* kBuild   = "0.3.0 (" __DATE__ " " __TIME__ ")";
}

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
    g_exports.wnd_nofilter = nullptr;
    g_exports.combat       = reinterpret_cast<void*>(&mod_combat);
    g_exports.imgui        = reinterpret_cast<void*>(&mod_imgui);
    g_exports.options_end  = reinterpret_cast<void*>(&mod_options_end);
    g_exports.combat_local = reinterpret_cast<void*>(&mod_combat_local);
    g_exports.wnd_filter   = nullptr;
    g_exports.options_windows = nullptr;
    return &g_exports;
}

uintptr_t mod_release() {
    // Snapshot UI options into settings before persisting.
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
