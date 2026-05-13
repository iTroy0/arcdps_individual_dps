#pragma once

#include <string>

namespace idps {

struct Settings {
    bool  exclude_npcs    = false;
    bool  exclude_gadgets = false;
    bool  window_open     = true;
    float window_x        = -1.0f;
    float window_y        = -1.0f;
    float window_w        = 380.0f;
    float window_h        = 260.0f;
    int   sort_mode       = 0; // 0=damage 1=dps 2=name 3=combat
    bool  sort_reverse    = false;

    bool  cleanses_open   = false;
    bool  strips_open     = false;
    bool  downs_open      = false;

    bool  highlight_self  = true;
    bool  name_white      = true;
    bool  self_name_gold  = false;
    bool  self_pin_top    = true;

    // Auto-hides low-priority columns as the Damage window narrows: drops
    // %, Combat, Damage, then DPS. Prof + Name always stay visible.
    bool  responsive_columns = true;

    bool  body_borders    = false;
    bool  bar_full_row    = true;

    // UI anchor: when on, all plugin windows refuse Move/Resize so they
    // stay locked to their current position and size. Off by default.
    bool  lock_windows    = false;

    // Detail-graph layer visibility. Peak (raw 1s) line is always drawn;
    // these four are click-toggleable via the legend chips.
    bool  chart_smooth    = true;
    bool  chart_cum       = true;
    bool  chart_avg       = true;
    bool  chart_burst     = true;

    // Clamped to [0.10, 1.0] on load.
    float window_alpha    = 0.95f;

    // When enabled, absolute window_x/y are recomputed from rx/ry against
    // the current viewport on first window appearance — survives resolution
    // changes / monitor swaps.
    bool  pos_relative    = false;
    float window_rx       = -1.0f;
    float window_ry       = -1.0f;
    float detail_rx       = -1.0f;
    float detail_ry       = -1.0f;

    bool  detail_open     = false;
    float detail_x        = -1.0f;
    float detail_y        = -1.0f;
    float detail_w        = 420.0f;
    float detail_h        = 420.0f;

    // Persisted across launches; used by exports.cpp to detect a version
    // bump that arc applied via get_update_url and surface a one-shot
    // "Updated to vX.Y.Z" banner in the main window.
    std::string last_seen_version;
};

Settings& settings();

void settings_load();
void settings_save();

} // namespace idps
