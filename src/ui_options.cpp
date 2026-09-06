#include "ui_options.h"

#include <atomic>
#include <imgui.h>

#include "arc_exports.h"
#include "exports.h"
#include "settings.h"
#include "tracker.h"
#include "ui.h"
#include "ui_common.h"

namespace idps {

namespace {

void draw_filters_section() {
    auto& s = settings();

    bool gap_en = s.fight_gap_enabled;
    if (ImGui::Checkbox("Smart fight boundaries", &gap_en)) {
        s.fight_gap_enabled = gap_en;
        options().fight_gap_enabled.store(gap_en, std::memory_order_relaxed);
    }
    item_tooltip("Re-entering combat within the gap of your last action "
                 "resumes the same fight. Beyond it, your row keeps its "
                 "previous stats and resets only when you next deal damage "
                 "or cleanse/strip - so NPC aggro or stray AoE never wipes "
                 "your numbers. Fights shorter than the gap are not saved "
                 "to history unless someone scored a down or kill. Off = "
                 "rows reset immediately on every combat entry.");
    ImGui::SameLine();
    int gap_sec = s.fight_gap_seconds;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("##fight_gap_sec", &gap_sec, 1, 5)) {
        if (gap_sec < 1)  gap_sec = 1;
        if (gap_sec > 60) gap_sec = 60;
        s.fight_gap_seconds = gap_sec;
        options().fight_gap_ms.store(static_cast<uint32_t>(gap_sec) * 1000u,
                                     std::memory_order_relaxed);
    }
    item_tooltip("Gap length in seconds (1-60). Default 5.");
    ImGui::SameLine();
    ImGui::TextDisabled("s");

    bool idle_en = s.idle_reset_enabled;
    if (ImGui::Checkbox("Reset idle rows", &idle_en)) {
        s.idle_reset_enabled = idle_en;
        options().idle_reset_enabled.store(idle_en, std::memory_order_relaxed);
    }
    item_tooltip("Zero a player's counters once their last damage, cleanse "
                 "or strip is older than this and they are not in combat. "
                 "They keep their place in the list - only the numbers go. "
                 "Without it a finished row holds its totals until that "
                 "player fights again or you reset the fight, so a "
                 "post-raid or post-skirmish table stays full of results "
                 "nobody is reading any more. Nothing is lost: the fight is "
                 "already saved to history, and the row fills back in the "
                 "moment they act again.");
    ImGui::SameLine();
    int idle_sec = s.idle_reset_seconds;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("##idle_reset_sec", &idle_sec, 10, 60)) {
        if (idle_sec < 10)   idle_sec = 10;
        if (idle_sec > 3600) idle_sec = 3600;
        s.idle_reset_seconds = idle_sec;
        options().idle_reset_ms.store(static_cast<uint32_t>(idle_sec) * 1000u,
                                      std::memory_order_relaxed);
    }
    item_tooltip("How long a row keeps its numbers with no activity, in "
                 "seconds (10-3600). Default 120.");
    ImGui::SameLine();
    ImGui::TextDisabled("s");
    ImGui::Separator();

    bool ex_npcs    = options().exclude_npcs.load(std::memory_order_relaxed);
    bool ex_gadgets = options().exclude_gadgets.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Exclude NPCs", &ex_npcs)) {
        options().exclude_npcs.store(ex_npcs, std::memory_order_relaxed);
        s.exclude_npcs = ex_npcs;
    }
    item_tooltip("Drops damage / strips / cleanses against world NPCs "
                 "(open-world enemies, structure NPCs, most training "
                 "golems in the SAB / Aerodrome lobby).");
    if (ImGui::Checkbox("Exclude Gadgets", &ex_gadgets)) {
        options().exclude_gadgets.store(ex_gadgets, std::memory_order_relaxed);
        s.exclude_gadgets = ex_gadgets;
    }
    item_tooltip("Drops damage against gadget-class targets. Some "
                 "training golems (Special Forces Training Area) are "
                 "classified as gadgets, not NPCs — uncheck this if your "
                 "test target's damage isn't registering.");
}

void draw_windows_section() {
    auto& s = settings();
    bool wo = s.window_open;
    if (ImGui::Checkbox("Damage window", &wo)) s.window_open = wo;

    bool cl = s.cleanses_open;
    bool st = s.strips_open;
    bool dn = s.downs_open;
    if (ImGui::Checkbox("Cleanses window",          &cl)) s.cleanses_open = cl;
    if (ImGui::Checkbox("Strips window",            &st)) s.strips_open   = st;
    if (ImGui::Checkbox("Down contribution window", &dn)) s.downs_open    = dn;
    item_tooltip("Per-player damage dealt to foes that subsequently went "
                 "into downstate, the count of downs they landed the "
                 "finishing hit on, and their killing blows on enemy "
                 "players. Useful for measuring focus-fire and cleave "
                 "attribution in WvW pulls.");
}

void draw_integration_section() {
    auto& s = settings();

    bool ac = s.use_arc_colors;
    if (ImGui::Checkbox("Use arcdps profession colors", &ac)) s.use_arc_colors = ac;
    item_tooltip("Read profession and subgroup colors straight from "
                 "arcdps's own tables, so this overlay matches arc's "
                 "exactly - including any recolor you apply in arc's "
                 "options, which takes effect immediately. Off uses the "
                 "canonical Guild Wars 2 palette built into the plugin.");
    if (s.use_arc_colors && !arc_colors_available()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(arc palette unavailable)");
        item_tooltip("arcdps did not expose its color tables to this "
                     "plugin, so the built-in palette is in use. Older "
                     "arcdps builds predate the export.");
    }

}

void draw_appearance_section() {
    auto& s = settings();
    bool hs = s.highlight_self;
    if (ImGui::Checkbox("Highlight self row", &hs)) s.highlight_self = hs;

    bool gold = s.self_name_gold;
    if (ImGui::Checkbox("Gold self name", &gold)) s.self_name_gold = gold;
    item_tooltip("Render your own player name in gold so it stands out "
                 "against squadmates, regardless of profession color.");

    bool pin = s.self_pin_top;
    if (ImGui::Checkbox("Pin self to top", &pin)) s.self_pin_top = pin;
    item_tooltip("Keep your own row at the top of every player table "
                 "regardless of the active sort order.");

    bool hdr = s.show_headers;
    if (ImGui::Checkbox("Column headers", &hdr)) s.show_headers = hdr;
    item_tooltip("Show the header row naming each column, on the overlay "
                 "tables. Hiding it buys back a row of height for a compact "
                 "overlay, but the header is also where click-to-sort, "
                 "drag-to-resize and the per-column hide menu live - with it "
                 "hidden, sorting moves to each window's right-click menu "
                 "and columns keep their current widths. The detail window "
                 "always keeps its headers.");

    bool bb = s.body_borders;
    if (ImGui::Checkbox("Column dividers in body", &bb)) s.body_borders = bb;
    item_tooltip("Show thin vertical lines between columns in the table "
                 "body. Header dividers stay visible either way so "
                 "columns remain resizable.");

    bool tot = s.show_totals;
    if (ImGui::Checkbox("Squad totals line", &tot)) s.show_totals = tot;
    item_tooltip("Show combined squad damage, summed DPS, and player "
                 "count above the table.");

    bool sg = s.show_subgroup;
    if (ImGui::Checkbox("Subgroup column", &sg)) s.show_subgroup = sg;
    item_tooltip("Show each player's squad subgroup, colored to match. "
                 "Subgroups arrive with arcdps's squad tracking, so the "
                 "column reads blank outside a squad.");

    float alpha = s.window_alpha;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("Window opacity", &alpha, 0.10f, 1.00f, "%.2f")) {
        if (alpha < 0.10f) alpha = 0.10f;
        if (alpha > 1.00f) alpha = 1.00f;
        s.window_alpha = alpha;
    }
    item_tooltip("Opacity of the window background only. Text and icons "
                 "stay fully opaque at any setting.");

    float balpha = s.bar_alpha;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("Damage bar opacity", &balpha, 0.15f, 1.00f, "%.2f")) {
        if (balpha < 0.15f) balpha = 0.15f;
        if (balpha > 1.00f) balpha = 1.00f;
        s.bar_alpha = balpha;
    }
    item_tooltip("Opacity of the per-player damage bar. The bar is the "
                 "largest profession-colored surface on screen, so a low "
                 "value lets the game world through and the profession "
                 "color reads as washed out. Raise it for solid, saturated "
                 "bars; lower it to see more of the world behind the "
                 "overlay.");
}

void draw_layout_section() {
    auto& s = settings();
    bool rc = s.responsive_columns;
    if (ImGui::Checkbox("Responsive columns", &rc)) s.responsive_columns = rc;
    item_tooltip("Auto-hide low-priority columns (%, Combat, Damage, DPS in "
                 "that order) as the window narrows. Off hands column "
                 "management to the table header right-click menu.");

    bool lk = s.lock_windows;
    if (ImGui::Checkbox("Lock windows (anchor)", &lk)) s.lock_windows = lk;
    item_tooltip("Prevent move and resize on every plugin window so a "
                 "stray drag can't relocate or shrink your overlay. Saved "
                 "between sessions; toggle off to rearrange.");
}

// Window positions / sizes are intentionally preserved so a reset doesn't
// yank the user's carefully-placed overlay back to (0,0).
void reset_to_defaults() {
    Settings def{};
    auto& s = settings();
    s.exclude_npcs       = def.exclude_npcs;
    s.exclude_gadgets    = def.exclude_gadgets;
    s.fight_gap_enabled  = def.fight_gap_enabled;
    s.fight_gap_seconds  = def.fight_gap_seconds;
    s.idle_reset_enabled = def.idle_reset_enabled;
    s.idle_reset_seconds = def.idle_reset_seconds;
    s.sort_mode          = def.sort_mode;
    s.sort_reverse       = def.sort_reverse;
    s.highlight_self     = def.highlight_self;
    s.self_name_gold     = def.self_name_gold;
    s.self_pin_top       = def.self_pin_top;
    s.responsive_columns = def.responsive_columns;
    s.body_borders       = def.body_borders;
    s.show_headers       = def.show_headers;
    s.downs_sort         = def.downs_sort;
    s.downs_sort_asc     = def.downs_sort_asc;
    s.show_totals        = def.show_totals;
    s.show_subgroup      = def.show_subgroup;
    s.use_arc_colors     = def.use_arc_colors;
    s.lock_windows       = def.lock_windows;
    s.chart_smooth       = def.chart_smooth;
    s.chart_cum          = def.chart_cum;
    s.chart_avg          = def.chart_avg;
    s.chart_burst        = def.chart_burst;
    s.window_alpha       = def.window_alpha;
    s.bar_alpha          = def.bar_alpha;
    options().exclude_npcs.store   (def.exclude_npcs,
                                    std::memory_order_relaxed);
    options().exclude_gadgets.store(def.exclude_gadgets,
                                    std::memory_order_relaxed);
    options().fight_gap_enabled.store(def.fight_gap_enabled,
                                      std::memory_order_relaxed);
    options().fight_gap_ms.store(
        static_cast<uint32_t>(def.fight_gap_seconds) * 1000u,
        std::memory_order_relaxed);
    options().idle_reset_enabled.store(def.idle_reset_enabled,
                                       std::memory_order_relaxed);
    options().idle_reset_ms.store(
        static_cast<uint32_t>(def.idle_reset_seconds) * 1000u,
        std::memory_order_relaxed);
}

void draw_settings_sections(bool default_open) {
    int flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    // Windows first: opening a panel is the most common reason to come here.
    if (ImGui::CollapsingHeader("Windows##idps", flags)) {
        draw_windows_section();
    }
    if (ImGui::CollapsingHeader("Appearance##idps", flags)) {
        draw_appearance_section();
    }
    if (ImGui::CollapsingHeader("Layout##idps", flags)) {
        draw_layout_section();
    }
    if (ImGui::CollapsingHeader("Filters##idps", flags)) {
        draw_filters_section();
    }
    if (ImGui::CollapsingHeader("arcdps integration##idps", flags)) {
        draw_integration_section();
    }
}

} // namespace

void draw_popup_settings() {
    draw_settings_sections(/*default_open=*/false);
}

uintptr_t mod_options_windows(const char* windowname) {
    // arcdps passes each of its own window names in turn, then a null to
    // let extensions append theirs. Only the null pass is ours.
    if (windowname) return 0;
    auto& s = settings();
    bool wo = s.window_open;
    bool cl = s.cleanses_open;
    bool st = s.strips_open;
    bool dn = s.downs_open;
    if (ImGui::Checkbox("Individual DPS",     &wo)) s.window_open   = wo;
    if (ImGui::Checkbox("IDPS Cleanses",      &cl)) s.cleanses_open = cl;
    if (ImGui::Checkbox("IDPS Strips",        &st)) s.strips_open   = st;
    if (ImGui::Checkbox("IDPS Downs",         &dn)) s.downs_open    = dn;
    return 0;
}

uintptr_t mod_options_end() {
    if (ImGui::CollapsingHeader("Individual DPS")) {
        ImGui::TextDisabled("v%s", version());
        ImGui::Separator();

        draw_settings_sections(/*default_open=*/false);

        ImGui::Separator();
        if (ImGui::Button("Reset to defaults")) reset_to_defaults();
        item_tooltip("Restore appearance / filter / layout defaults. "
                     "Window positions and sizes are preserved.");
    }
    return 0;
}

} // namespace idps
