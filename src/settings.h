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
    // Reverses sort_rows()'s canonical order for the active sort_mode.
    // Persisted so a user-toggled column direction survives reload.
    bool  sort_reverse    = false;

    bool  cleanses_open   = false;
    bool  strips_open     = false;

    // Tints the self row's background so the local player stands out
    // against teammates without overriding profession color.
    bool  highlight_self  = true;

    // White names in the player tables (Damage, Cleanses, Strips). Profession
    // is still conveyed by the icon column and the per-row damage bar tint,
    // so the lift in legibility is worth losing prof color on the text.
    bool  name_white      = true;

    // Auto-hide low-priority columns when the Damage window gets narrow.
    // Drops %, Combat, Damage, then DPS in that order. Prof + Name always
    // stay visible. Disable for manual control via the Hideable header menu.
    bool  responsive_columns = true;

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
