#pragma once

#include <string>

namespace idps {

struct Settings {
    bool  exclude_npcs    = false;
    bool  exclude_gadgets = false;

    // Smart fight boundaries (see Options in tracker.h). Seconds clamped
    // to [1, 60] on load.
    bool  fight_gap_enabled = true;
    int   fight_gap_seconds = 5;

    // Idle-row reset. A row keeps its last fight's numbers while out of
    // combat — that is what makes them readable after the pull — but with
    // nothing to clear them, a player who stops fighting sits in the table
    // with a full row until they fight again or the fight is reset by hand.
    // Once their last credited action is this old and they are not in
    // combat, the row's counters read zero. The player keeps their place in
    // the list; only the numbers go. Nothing is destroyed either: the fight
    // reached history long before (see fight_gap_seconds), and the row
    // refills on their next action. Seconds clamped to [10, 3600].
    bool  idle_reset_enabled = true;
    int   idle_reset_seconds = 120;

    bool  window_open     = true;
    float window_x        = -1.0f;
    float window_y        = -1.0f;
    float window_w        = 380.0f;
    float window_h        = 260.0f;
    int   sort_mode       = 0; // 0=damage 1=dps 2=name 3=combat 4=subgroup
    bool  sort_reverse    = false;

    bool  cleanses_open   = false;
    bool  strips_open     = false;
    bool  downs_open      = false;

    bool  highlight_self  = true;
    bool  self_name_gold  = false;
    bool  self_pin_top    = true;

    // Source profession / subgroup colours from arcdps's own tables (its e5
    // export) so both overlays agree exactly, and follow along when the user
    // recolours a profession in arc's options. Off falls back to the
    // canonical GW2 palette compiled into the plugin.
    bool  use_arc_colors  = true;

    // Subgroup column in the Damage table, coloured per subgroup.
    bool  show_subgroup   = false;

    // Auto-hides low-priority columns as the Damage window narrows: drops
    // %, Combat, Damage, then DPS. Prof + Name always stay visible.
    bool  responsive_columns = true;

    bool  body_borders    = false;

    // Opacity of the per-player damage bar, independent of window_alpha.
    // The bar is the largest profession-coloured surface on screen, and it
    // sits over a translucent window over the game world — at low values the
    // terrain dominates and the profession colour reads as washed out.
    // Clamped to [0.15, 1.0] on load.
    float bar_alpha       = 0.70f;

    // Column header row on every table. Off is a compact, chrome-free
    // overlay — at the cost of the interactions that live in that row:
    // click-to-sort, drag-to-resize, and the per-column hide menu. Sorting
    // is reachable from each window's right-click menu either way, which is
    // why the sort choices below are persisted rather than read back out of
    // the header.
    bool  show_headers    = true;

    // Sort state for the Downs window: column index into its table, and
    // direction. Kept in sync with the header row while it is visible, and
    // the only source of truth once it is hidden.
    int   downs_sort      = 2; // Contrib
    bool  downs_sort_asc  = false;

    // Squad totals line (Σ damage / Σ DPS / player count) above the table.
    bool  show_totals     = true;

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

    bool  detail_open     = false;
    float detail_x        = -1.0f;
    float detail_y        = -1.0f;
    // Wide enough for all eight skill-table columns at their default
    // widths. Only applies to a fresh install — an existing detail_w in
    // the ini wins, and the table is resizable either way.
    float detail_w        = 480.0f;
    float detail_h        = 420.0f;

    // Persisted across launches; used by exports.cpp to detect a version
    // bump that arc applied via get_update_url and surface a one-shot
    // "Updated to vX.Y.Z" banner in the main window.
    std::string last_seen_version;
};

Settings& settings();

void settings_load();
void settings_save();

// Cheap periodic flush: serializes the current settings and writes only
// when the content differs from the last write. Call from the render
// thread (~every 15s) so a game crash doesn't lose the session's layout
// — settings_save() in mod_release never runs on a hard crash.
void settings_autosave();

} // namespace idps
