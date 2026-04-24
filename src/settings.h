#pragma once

#include <string>

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
};

Settings& settings();

void settings_load();
void settings_save();

} // namespace idps
