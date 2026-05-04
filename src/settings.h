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

    // Render the local player's name in gold (overrides name_white / prof
    // color for the self row). Pairs with highlight_self for stronger "this
    // is me" pop in big squads.
    bool  self_name_gold  = false;

    // Auto-hide low-priority columns when the Damage window gets narrow.
    // Drops %, Combat, Damage, then DPS in that order. Prof + Name always
    // stay visible. Disable for manual control via the Hideable header menu.
    bool  responsive_columns = true;

    // Hide inner vertical column dividers in the table body. Header still
    // shows them so columns remain draggable.
    bool  body_borders    = false;

    // Damage bar fills the entire row width instead of just the Name cell.
    bool  bar_full_row    = true;

    // Background alpha applied to all plugin windows (Damage, detail,
    // Cleanses, Strips). Clamped to [0.10, 1.0] on load.
    float window_alpha    = 0.95f;

    // Position stored as a fraction of the main viewport (window_rx/ry,
    // detail_rx/ry) instead of absolute pixels. Survives resolution changes
    // / monitor swaps. When enabled the absolute window_x/y are recomputed
    // from rx/ry against the current viewport on first window appearance.
    bool  pos_relative    = false;
    float window_rx       = -1.0f;
    float window_ry       = -1.0f;
    float detail_rx       = -1.0f;
    float detail_ry       = -1.0f;

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
