#include "ui_options.h"

#include <imgui.h>

#include "exports.h"
#include "settings.h"
#include "tracker.h"
#include "ui.h"
#include "ui_common.h"

namespace idps {

void draw_options_controls() {
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
    ImGui::SameLine();
    if (ImGui::Checkbox("Exclude Gadgets", &ex_gadgets)) {
        options().exclude_gadgets.store(ex_gadgets, std::memory_order_relaxed);
        s.exclude_gadgets = ex_gadgets;
    }
    item_tooltip("Drops damage against gadget-class targets. Some "
                 "training golems (Special Forces Training Area) are "
                 "classified as gadgets, not NPCs — uncheck this if your "
                 "test target's damage isn't registering.");

    const char* sort_items[] = {"Damage", "DPS", "Name", "Combat time"};
    int sort_mode = s.sort_mode;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("Sort", &sort_mode, sort_items, IM_ARRAYSIZE(sort_items))) {
        s.sort_mode = sort_mode;
    }

    ImGui::Separator();
    bool cl = s.cleanses_open;
    bool st = s.strips_open;
    bool dn = s.downs_open;
    if (ImGui::Checkbox("Cleanses window",         &cl)) s.cleanses_open = cl;
    if (ImGui::Checkbox("Strips window",           &st)) s.strips_open   = st;
    if (ImGui::Checkbox("Down contribution window",&dn)) s.downs_open    = dn;
    item_tooltip("Per-player damage dealt to foes that subsequently went "
                 "into downstate, plus the count of distinct downs they "
                 "contributed to. Useful for measuring focus-fire and "
                 "cleave attribution in WvW pulls.");

    bool hs = s.highlight_self;
    if (ImGui::Checkbox("Highlight self row", &hs)) s.highlight_self = hs;

    bool nw = s.name_white;
    if (ImGui::Checkbox("White names", &nw)) s.name_white = nw;

    bool gold = s.self_name_gold;
    if (ImGui::Checkbox("Gold self name", &gold)) s.self_name_gold = gold;
    item_tooltip("Render your own player name in gold so it stands out "
                 "against squadmates, regardless of profession color.");

    bool rc = s.responsive_columns;
    if (ImGui::Checkbox("Responsive columns", &rc)) s.responsive_columns = rc;

    bool bb = s.body_borders;
    if (ImGui::Checkbox("Column dividers in body", &bb)) s.body_borders = bb;
    item_tooltip("Show thin vertical lines between columns in the table "
                 "body. Header dividers stay visible either way so "
                 "columns remain resizable.");

    bool fb = s.bar_full_row;
    if (ImGui::Checkbox("Full-row damage bar", &fb)) s.bar_full_row = fb;
    item_tooltip("When on, the per-player damage bar fills the entire "
                 "row width. Off restricts it to the Name column.");

    bool pr = s.pos_relative;
    if (ImGui::Checkbox("Screen-relative position", &pr)) s.pos_relative = pr;
    item_tooltip("Store window position as a fraction of the game "
                 "viewport instead of absolute pixels, so the window "
                 "stays in roughly the same on-screen spot after "
                 "resolution changes or monitor swaps.");

    float alpha = s.window_alpha;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("Window opacity", &alpha, 0.10f, 1.00f, "%.2f")) {
        if (alpha < 0.10f) alpha = 0.10f;
        if (alpha > 1.00f) alpha = 1.00f;
        s.window_alpha = alpha;
    }
}

uintptr_t mod_options_end() {
    if (ImGui::CollapsingHeader("Individual DPS")) {
        auto& s = settings();
        bool open = s.window_open;
        if (ImGui::Checkbox("Show window", &open)) s.window_open = open;
        draw_options_controls();
        ImGui::Text("v%s", version());
    }
    return 0;
}

} // namespace idps
