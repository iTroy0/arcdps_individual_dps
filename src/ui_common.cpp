#include "ui_common.h"

#include <algorithm>
#include <cstdio>

namespace idps {

ImU32 prof_color(uint32_t prof) {
    // Saturation + luminance bumped vs canonical GW2 prof palette so colors
    // pop on the translucent window background. Necro stays darker green
    // than Ranger so they don't blur together in mixed squads.
    switch (prof) {
        case 1: return IM_COL32(120, 220, 255, 255); // Guardian
        case 2: return IM_COL32(255, 220,  90, 255); // Warrior
        case 3: return IM_COL32(240, 180,  85, 255); // Engineer
        case 4: return IM_COL32(150, 240, 110, 255); // Ranger
        case 5: return IM_COL32(255, 170, 190, 255); // Thief
        case 6: return IM_COL32(255, 130, 120, 255); // Elementalist
        case 7: return IM_COL32(225, 145, 255, 255); // Mesmer
        case 8: return IM_COL32( 90, 215, 145, 255); // Necromancer
        case 9: return IM_COL32(245, 130, 110, 255); // Revenant
        default: return IM_COL32(230, 230, 230, 255);
    }
}

const char* prof_short(uint32_t prof) {
    switch (prof) {
        case 1:  return "Grd";
        case 2:  return "War";
        case 3:  return "Eng";
        case 4:  return "Rgr";
        case 5:  return "Thf";
        case 6:  return "Ele";
        case 7:  return "Mes";
        case 8:  return "Nec";
        case 9:  return "Rev";
        default: return "?";
    }
}

void format_time(char* out, size_t n, uint64_t ms) {
    uint64_t total_s = ms / 1000;
    uint64_t m = total_s / 60;
    uint64_t s = total_s % 60;
    std::snprintf(out, n, "%llu:%02llu",
                  (unsigned long long)m, (unsigned long long)s);
}

void format_count(char* out, size_t n, uint64_t v) {
    if (v >= 1000000) std::snprintf(out, n, "%.2fm", v / 1000000.0);
    else if (v >= 10000) std::snprintf(out, n, "%.1fk", v / 1000.0);
    else std::snprintf(out, n, "%llu", (unsigned long long)v);
}

// Drop alpha to ~67% for out-of-combat rows. 50% washed names out against
// the translucent window bg; 170/255 keeps the distinction visible while
// preserving RGB so prof color identity stays intact.
ImU32 dim_alpha(ImU32 col) {
    uint32_t a = (col >> 24) & 0xFFu;
    a = (a * 170u) / 255u;
    return (col & 0x00FFFFFFu) | (a << 24);
}

void sort_rows(std::vector<Snapshot>& rows, int mode) {
    switch (mode) {
        case 1:
            std::sort(rows.begin(), rows.end(),
                [](const Snapshot& a, const Snapshot& b) { return a.dps > b.dps; });
            break;
        case 2:
            std::sort(rows.begin(), rows.end(),
                [](const Snapshot& a, const Snapshot& b) { return a.name < b.name; });
            break;
        case 3:
            std::sort(rows.begin(), rows.end(),
                [](const Snapshot& a, const Snapshot& b) { return a.combat_ms > b.combat_ms; });
            break;
        default:
            std::sort(rows.begin(), rows.end(),
                [](const Snapshot& a, const Snapshot& b) { return a.damage_total > b.damage_total; });
            break;
    }
}

void pin_self_to_top(std::vector<Snapshot>& rows) {
    auto it = std::find_if(rows.begin(), rows.end(),
                           [](const Snapshot& r) { return r.is_self; });
    if (it != rows.begin() && it != rows.end()) {
        std::rotate(rows.begin(), it, it + 1);
    }
}

void pin_self_to_top(std::vector<size_t>& idx,
                     const std::vector<Snapshot>& rows) {
    auto it = std::find_if(idx.begin(), idx.end(),
        [&rows](size_t i) { return rows[i].is_self; });
    if (it != idx.begin() && it != idx.end()) {
        std::rotate(idx.begin(), it, it + 1);
    }
}

// Avoids SetItemTooltip (newer ImGui) so this builds against arc's pinned version.
void item_tooltip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void anchor_cursor_tooltip() {
    ImVec2 mp = ImGui::GetIO().MousePos;
    // Pivot (1,1): the tooltip's bottom-right corner anchors at the cursor,
    // so it grows up and to the LEFT, never under the pointer. Opaque bg so
    // it isn't washed out by the translucent popup over the game world.
    ImGui::SetNextWindowPos(ImVec2(mp.x, mp.y - 6.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.95f);
}

void account_tooltip(const std::string& account) {
    if (account.empty()) return;
    if (!ImGui::IsItemHovered()) return;
    anchor_cursor_tooltip();
    ImGui::BeginTooltip();
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.50f, 1.0f), "%s", account.c_str());
    ImGui::EndTooltip();
}

void align_icon_to_text() {
    float dy = (ImGui::GetTextLineHeight() - 14.0f) * 0.5f;
    if (dy > 0.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dy);
    }
}

// Apply window position with optional viewport-relative mode. ImGui v1.80
// (non-docking) lacks GetMainViewport, so io.DisplaySize (arc fills it with
// the swapchain back-buffer) is the viewport reference.
//
// FirstUseEver alone won't reposition after a resolution change or relative-
// mode toggle (ImGui's cached pos wins). prev_relative + prev_ds detect those
// edges so we force-apply (Always) for one frame, then drop back to
// FirstUseEver so user dragging isn't fought every frame.
void apply_window_pos(float abs_x, float abs_y, float rx, float ry, bool relative,
                      bool& prev_relative, ImVec2& prev_ds) {
    const ImVec2& ds = ImGui::GetIO().DisplaySize;
    bool ds_changed = ds.x != prev_ds.x || ds.y != prev_ds.y;
    bool toggled_on = relative && !prev_relative;
    bool force      = (ds_changed && relative) || toggled_on;

    if (relative && rx >= 0.0f && ry >= 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(ds.x * rx, ds.y * ry),
                                force ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    } else if (abs_x >= 0.0f && abs_y >= 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(abs_x, abs_y), ImGuiCond_FirstUseEver);
    }
    prev_ds       = ds;
    prev_relative = relative;
}

// Capture both absolute pixels and display-relative fractions every frame,
// so toggling pos_relative later has valid fractions ready on next session.
void capture_window_pos(const ImVec2& pos, float& abs_x, float& abs_y,
                        float& rx, float& ry) {
    abs_x = pos.x;
    abs_y = pos.y;
    const ImVec2& ds = ImGui::GetIO().DisplaySize;
    if (ds.x > 0.0f) rx = pos.x / ds.x;
    if (ds.y > 0.0f) ry = pos.y / ds.y;
}

} // namespace idps
