#pragma once

namespace idps {

// Simple ini-style settings stored next to the DLL as
// arcdps_individual_dps.ini. Load once at startup, save on shutdown.
struct Settings {
    bool  exclude_npcs    = false;
    bool  exclude_gadgets = false;
    bool  window_open     = true;
    float window_x        = -1.0f;
    float window_y        = -1.0f;
    float window_w        = 380.0f;
    float window_h        = 260.0f;
    int   sort_mode       = 0; // 0=damage 1=dps 2=name 3=combat

    bool  cleanses_open   = false;
    bool  strips_open     = false;

    // Background alpha applied to all plugin windows (Damage, detail,
    // Cleanses, Strips). Clamped to [0.10, 1.0] on load.
    float window_alpha    = 0.95f;

    // Per-skill detail window. Position/size only persist if it was open
    // when the plugin last shut down.
    bool  detail_open     = false;
    float detail_x        = -1.0f;
    float detail_y        = -1.0f;
    float detail_w        = 420.0f;
    float detail_h        = 420.0f;
};

Settings& settings();

void settings_load();
void settings_save();

} // namespace idps
