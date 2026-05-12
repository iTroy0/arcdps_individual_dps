#include "ui_options.h"

#include <atomic>
#include <imgui.h>

#include "exports.h"
#include "settings.h"
#include "tracker.h"
#include "ui.h"
#include "ui_common.h"

namespace idps {

namespace {

void draw_filters_section() {
    auto& s = settings();
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
    bool cl = s.cleanses_open;
    bool st = s.strips_open;
    bool dn = s.downs_open;
    if (ImGui::Checkbox("Cleanses window",          &cl)) s.cleanses_open = cl;
    if (ImGui::Checkbox("Strips window",            &st)) s.strips_open   = st;
    if (ImGui::Checkbox("Down contribution window", &dn)) s.downs_open    = dn;
    item_tooltip("Per-player damage dealt to foes that subsequently went "
                 "into downstate, plus the count of distinct downs they "
                 "contributed to. Useful for measuring focus-fire and "
                 "cleave attribution in WvW pulls.");
}

void draw_appearance_section() {
    auto& s = settings();
    bool hs = s.highlight_self;
    if (ImGui::Checkbox("Highlight self row", &hs)) s.highlight_self = hs;

    bool nw = s.name_white;
    if (ImGui::Checkbox("White names", &nw)) s.name_white = nw;

    bool gold = s.self_name_gold;
    if (ImGui::Checkbox("Gold self name", &gold)) s.self_name_gold = gold;
    item_tooltip("Render your own player name in gold so it stands out "
                 "against squadmates, regardless of profession color.");

    bool pin = s.self_pin_top;
    if (ImGui::Checkbox("Pin self to top", &pin)) s.self_pin_top = pin;
    item_tooltip("Keep your own row at the top of every player table "
                 "regardless of the active sort order.");

    bool bb = s.body_borders;
    if (ImGui::Checkbox("Column dividers in body", &bb)) s.body_borders = bb;
    item_tooltip("Show thin vertical lines between columns in the table "
                 "body. Header dividers stay visible either way so "
                 "columns remain resizable.");

    bool fb = s.bar_full_row;
    if (ImGui::Checkbox("Full-row damage bar", &fb)) s.bar_full_row = fb;
    item_tooltip("When on, the per-player damage bar fills the entire "
                 "row width. Off restricts it to the Name column.");

    float alpha = s.window_alpha;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("Window opacity", &alpha, 0.10f, 1.00f, "%.2f")) {
        if (alpha < 0.10f) alpha = 0.10f;
        if (alpha > 1.00f) alpha = 1.00f;
        s.window_alpha = alpha;
    }
}

void draw_layout_section() {
    auto& s = settings();
    bool rc = s.responsive_columns;
    if (ImGui::Checkbox("Responsive columns", &rc)) s.responsive_columns = rc;
    item_tooltip("Auto-hide low-priority columns (%, Combat, Damage, DPS in "
                 "that order) as the window narrows. Off hands column "
                 "management to the table header right-click menu.");

    bool pr = s.pos_relative;
    if (ImGui::Checkbox("Screen-relative position", &pr)) s.pos_relative = pr;
    item_tooltip("Store window position as a fraction of the game "
                 "viewport instead of absolute pixels, so the window "
                 "stays in roughly the same on-screen spot after "
                 "resolution changes or monitor swaps.");

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
    s.sort_mode          = def.sort_mode;
    s.sort_reverse       = def.sort_reverse;
    s.highlight_self     = def.highlight_self;
    s.name_white         = def.name_white;
    s.self_name_gold     = def.self_name_gold;
    s.responsive_columns = def.responsive_columns;
    s.body_borders       = def.body_borders;
    s.bar_full_row       = def.bar_full_row;
    s.window_alpha       = def.window_alpha;
    s.pos_relative       = def.pos_relative;
    options().exclude_npcs.store   (def.exclude_npcs,
                                    std::memory_order_relaxed);
    options().exclude_gadgets.store(def.exclude_gadgets,
                                    std::memory_order_relaxed);
}

void draw_settings_sections(bool default_open) {
    int flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    if (ImGui::CollapsingHeader("Appearance##idps", flags)) {
        draw_appearance_section();
    }
    if (ImGui::CollapsingHeader("Filters##idps", flags)) {
        draw_filters_section();
    }
    if (ImGui::CollapsingHeader("Layout##idps", flags)) {
        draw_layout_section();
    }
    if (ImGui::CollapsingHeader("Windows##idps", flags)) {
        draw_windows_section();
    }
}

} // namespace

void draw_popup_settings() {
    draw_settings_sections(/*default_open=*/true);
}

uintptr_t mod_options_end() {
    if (ImGui::CollapsingHeader("Individual DPS")) {
        auto& s = settings();
        ImGui::Text("v%s", version());
        ImGui::Separator();

        bool open = s.window_open;
        if (ImGui::Checkbox("Show window", &open)) s.window_open = open;

        draw_settings_sections(/*default_open=*/false);

        ImGui::Separator();
        if (ImGui::Button("Reset to defaults")) reset_to_defaults();
        item_tooltip("Restore appearance / filter / layout defaults. "
                     "Window positions and sizes are preserved.");
    }
    return 0;
}

} // namespace idps
