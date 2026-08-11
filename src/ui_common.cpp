#include "ui_common.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "arc_exports.h"
#include "settings.h"

namespace idps {

namespace {
    // Canonical Guild Wars 2 profession colours — the same values arcdps
    // ships as its defaults. Used only when arcdps's own tables (e5) are
    // unreachable or the user opted out; while they are available we render
    // arc's live values so the two overlays never disagree, including after
    // the user recolours a profession in arc's options.
    constexpr ImU32 kProfBase[10] = {
        IM_COL32(230, 230, 230, 255), // 0 unknown
        IM_COL32(114, 193, 217, 255), // 1 Guardian
        IM_COL32(255, 209, 102, 255), // 2 Warrior
        IM_COL32(208, 156,  89, 255), // 3 Engineer
        IM_COL32(140, 220, 130, 255), // 4 Ranger
        IM_COL32(192, 143, 149, 255), // 5 Thief
        IM_COL32(246, 138, 136, 255), // 6 Elementalist
        IM_COL32(182, 121, 213, 255), // 7 Mesmer
        IM_COL32( 82, 167, 111, 255), // 8 Necromancer
        IM_COL32(209, 110,  90, 255), // 9 Revenant
    };

    // Lightened companion palette, matching arc's base/highlight split: base
    // is the name text, highlight the fill behind it.
    constexpr ImU32 kProfHighlight[10] = {
        IM_COL32(245, 245, 245, 255), // 0 unknown
        IM_COL32(160, 219, 236, 255), // 1 Guardian
        IM_COL32(255, 226, 156, 255), // 2 Warrior
        IM_COL32(228, 190, 141, 255), // 3 Engineer
        IM_COL32(181, 233, 174, 255), // 4 Ranger
        IM_COL32(216, 183, 187, 255), // 5 Thief
        IM_COL32(250, 180, 179, 255), // 6 Elementalist
        IM_COL32(208, 166, 229, 255), // 7 Mesmer
        IM_COL32(133, 197, 155, 255), // 8 Necromancer
        IM_COL32(228, 157, 143, 255), // 9 Revenant
    };

    // Fallback subgroup palette, walked modulo the subgroup number. Chosen
    // for separation at a glance rather than for meaning — subgroup 3 has no
    // canonical colour the way Necromancer does.
    constexpr ImU32 kSubgroupFallback[8] = {
        IM_COL32(120, 190, 240, 255),
        IM_COL32(240, 175,  95, 255),
        IM_COL32(140, 215, 140, 255),
        IM_COL32(225, 135, 145, 255),
        IM_COL32(185, 150, 235, 255),
        IM_COL32(235, 220, 120, 255),
        IM_COL32(120, 215, 210, 255),
        IM_COL32(225, 160, 205, 255),
    };

    uint32_t clamp_prof(uint32_t prof) { return prof <= 9 ? prof : 0; }
}

namespace {
    // Force full opacity, keeping RGB. Every colour this file hands out is
    // composited over a window that is itself translucent over the game
    // world, so any alpha below 1 lets terrain show through the glyphs and
    // reads as a washed-out colour rather than a softer one. Callers that
    // want translucency (the row bars) set their own alpha explicitly.
    ImU32 opaque(ImU32 c) { return (c & 0x00FFFFFFu) | (0xFFu << 24); }

    ImU32 opaque(const ImVec4& c) {
        return opaque(ImGui::ColorConvertFloat4ToU32(c));
    }
}

ImU32 prof_color(uint32_t prof) {
    prof = clamp_prof(prof);
    ImVec4 c;
    if (settings().use_arc_colors && arc_prof_color(prof, /*highlight=*/false, c))
        return opaque(c);
    return kProfBase[prof];
}

ImU32 prof_color_highlight(uint32_t prof) {
    prof = clamp_prof(prof);
    ImVec4 c;
    if (settings().use_arc_colors && arc_prof_color(prof, /*highlight=*/true, c))
        return opaque(c);
    return kProfHighlight[prof];
}

ImU32 subgroup_color(uint32_t subgroup) {
    ImVec4 c;
    if (settings().use_arc_colors &&
        arc_subgroup_color(subgroup, /*highlight=*/false, c))
        return opaque(c);
    if (subgroup == 0) return IM_COL32(170, 170, 170, 255);
    return kSubgroupFallback[(subgroup - 1) % 8];
}

ImU32 with_alpha(ImU32 col, float a) {
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    uint32_t v = static_cast<uint32_t>(a * 255.0f + 0.5f);
    return (col & 0x00FFFFFFu) | (v << 24);
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

const char* prof_name(uint32_t prof) {
    switch (prof) {
        case 1:  return "Guardian";
        case 2:  return "Warrior";
        case 3:  return "Engineer";
        case 4:  return "Ranger";
        case 5:  return "Thief";
        case 6:  return "Elementalist";
        case 7:  return "Mesmer";
        case 8:  return "Necromancer";
        case 9:  return "Revenant";
        default: return "Unknown";
    }
}

// Elite specialisation ids as they arrive in ag::elite / cbtevent payloads.
// 0 means the player is on their core profession.
//
// Covers the HoT / PoF / EoD sets (ids 5..72). The contiguous 73..81 block
// that icons.cpp also carries artwork for is deliberately absent: those ids
// resolve to no name here and the caller falls back to the profession, which
// is better than shipping a guessed label. Add them once the mapping is
// confirmed against a live log.
const char* elite_name(uint32_t elite) {
    switch (elite) {
        case 27: return "Dragonhunter";
        case 62: return "Firebrand";
        case 65: return "Willbender";
        case 18: return "Berserker";
        case 61: return "Spellbreaker";
        case 68: return "Bladesworn";
        case 43: return "Scrapper";
        case 57: return "Holosmith";
        case 70: return "Mechanist";
        case  5: return "Druid";
        case 55: return "Soulbeast";
        case 72: return "Untamed";
        case  7: return "Daredevil";
        case 58: return "Deadeye";
        case 71: return "Specter";
        case 48: return "Tempest";
        case 56: return "Weaver";
        case 67: return "Catalyst";
        case 40: return "Chronomancer";
        case 59: return "Mirage";
        case 66: return "Virtuoso";
        case 34: return "Reaper";
        case 60: return "Scourge";
        case 64: return "Harbinger";
        case 52: return "Herald";
        case 63: return "Renegade";
        case 69: return "Vindicator";
        default: return nullptr;
    }
}

void format_spec(char* out, size_t n, uint32_t prof, uint32_t elite) {
    if (n == 0) return;
    if (const char* e = elite_name(elite)) {
        std::snprintf(out, n, "%s", e);
        return;
    }
    std::snprintf(out, n, "%s", prof_name(prof));
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

void format_clock(char* out, size_t n, uint64_t unix_s) {
    if (n == 0) return;
    out[0] = '\0';
    if (unix_s == 0) return;
    time_t t = static_cast<time_t>(unix_s);
    struct tm local{};
    if (localtime_s(&local, &t) != 0) return;
    std::snprintf(out, n, "%02d:%02d", local.tm_hour, local.tm_min);
}

void format_ago(char* out, size_t n, uint64_t unix_s) {
    if (n == 0) return;
    out[0] = '\0';
    if (unix_s == 0) return;
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    uint64_t d   = now > unix_s ? now - unix_s : 0;
    if (d < 60)        std::snprintf(out, n, "%llus ago", (unsigned long long)d);
    else if (d < 3600) std::snprintf(out, n, "%llum ago", (unsigned long long)(d / 60));
    else               std::snprintf(out, n, "%lluh ago", (unsigned long long)(d / 3600));
}

// Mark a row as out of combat by darkening it, not by making it
// see-through.
//
// This used to scale alpha to ~67%. Alpha is the wrong channel here: the
// plugin's windows are translucent over the game world, so a partly
// transparent glyph blends with whatever terrain happens to be behind it —
// the colour loses saturation, shifts with the background, and reads as
// washed out rather than as dimmed. Scaling RGB instead keeps every pixel
// fully opaque, so the profession stays identifiable and the only thing
// that changes is brightness. Out-of-combat rows are what you look at most
// of the time (reviewing after a fight), so this is the common case, not
// the edge case.
ImU32 dim_color(ImU32 col) {
    constexpr uint32_t kScale = 160; // /255 ≈ 63% brightness
    uint32_t r = ((col       ) & 0xFFu) * kScale / 255u;
    uint32_t g = ((col >>  8) & 0xFFu) * kScale / 255u;
    uint32_t b = ((col >> 16) & 0xFFu) * kScale / 255u;
    return (col & 0xFF000000u) | (b << 16) | (g << 8) | r;
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
        case 4:
            // Subgroup ascending, then damage within a subgroup so each
            // group reads like a miniature ranking. Unknown subgroups (0)
            // sort last rather than first.
            std::sort(rows.begin(), rows.end(),
                [](const Snapshot& a, const Snapshot& b) {
                    uint32_t ka = a.subgroup ? a.subgroup : 0xFFFFu;
                    uint32_t kb = b.subgroup ? b.subgroup : 0xFFFFu;
                    if (ka != kb) return ka < kb;
                    return a.damage_total > b.damage_total;
                });
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

// Seed a window's saved position. FirstUseEver so the placement applies once
// and the user's dragging is never fought afterwards; -1 is the "never
// positioned" sentinel, which leaves ImGui to pick.
void apply_window_pos(float abs_x, float abs_y) {
    if (abs_x >= 0.0f && abs_y >= 0.0f) {
        ImGui::SetNextWindowPos(ImVec2(abs_x, abs_y), ImGuiCond_FirstUseEver);
    }
}

void capture_window_pos(const ImVec2& pos, float& abs_x, float& abs_y) {
    abs_x = pos.x;
    abs_y = pos.y;
}

} // namespace idps
