#include "ui.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

#include "icons.h"
#include "settings.h"
#include "tracker.h"
#include "ui_common.h"
#include "ui_detail.h"
#include "ui_options.h"
#include "ui_support.h"

namespace idps {

namespace {
    // EMA smoothing for the live DPS column. Cadence ~500ms, weight 3:1 (old:new).
    struct DpsCache {
        uint64_t last_wall = 0;
        uint64_t shown     = 0;
    };
    std::unordered_map<uintptr_t, DpsCache> g_dps_cache;

    // Reusable per-frame buffer — static storage avoids realloc + per-agent
    // string copies every render.
    std::vector<Snapshot> g_rows;

    uint64_t smooth_dps(uintptr_t id, uint64_t instant) {
        auto& c = g_dps_cache[id];
        uint64_t now = GetTickCount64();
        if (instant == 0) {
            c.last_wall = now;
            c.shown     = 0;
            return 0;
        }
        if (c.shown == 0) {
            c.last_wall = now;
            c.shown     = instant;
            return instant;
        }
        if (now - c.last_wall >= 500) {
            c.last_wall = now;
            c.shown     = (c.shown * 3 + instant) / 4;
        }
        return c.shown;
    }

    // Drop EMA cache entries for agents no longer in the snapshot, so the
    // cache doesn't accumulate stale ids across long WvW sessions.
    void prune_dps_cache(const std::vector<Snapshot>& rows) {
        // Rebuild the live set every frame: agent churn (one leaves, one
        // joins) can keep cache.size() == rows.size() while a stale id
        // lingers. ~50-element set-build per frame is cheap.
        std::unordered_set<uintptr_t> live;
        live.reserve(rows.size());
        for (const auto& r : rows) live.insert(r.id);
        for (auto it = g_dps_cache.begin(); it != g_dps_cache.end(); ) {
            if (live.find(it->first) == live.end()) it = g_dps_cache.erase(it);
            else                                    ++it;
        }
    }
}

void ui_init(ImGuiContext* ctx) {
    if (ctx) ImGui::SetCurrentContext(ctx);
}

uintptr_t mod_imgui(uint32_t not_charsel_or_loading, uint32_t /*hide_if_combat_or_ooc*/) {
    if (!not_charsel_or_loading) return 0;
    icons_ensure_loaded();
    auto& s = settings();
    if (!s.window_open) return 0;

    static bool   prev_rel = false;
    static ImVec2 prev_ds(0.0f, 0.0f);
    apply_window_pos(s.window_x, s.window_y, s.window_rx, s.window_ry,
                     s.pos_relative, prev_rel, prev_ds);
    ImGui::SetNextWindowSize(ImVec2(s.window_w, s.window_h),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(s.window_alpha);

    // Fight-history navigation. viewed_fight 0 = live current fight,
    // 1 = most recent past fight, 2 = older, etc. Clamped to the deque
    // size each frame so a FIFO drop doesn't leave the user pointing at
    // a vanished entry.
    int hist_n = tracker().history_size();
    static int viewed_fight = 0;
    if (viewed_fight > hist_n) viewed_fight = hist_n;
    if (viewed_fight < 0)      viewed_fight = 0;

    bool viewing_history = viewed_fight > 0;
    if (viewing_history) {
        int storage_idx = hist_n - viewed_fight;
        if (!tracker().snapshot_at(storage_idx, g_rows)) {
            viewed_fight = 0;
            viewing_history = false;
            tracker().snapshot(g_rows);
        }
    } else {
        tracker().snapshot(g_rows);
    }
    sort_rows(g_rows, s.sort_mode);
    if (s.sort_reverse) std::reverse(g_rows.begin(), g_rows.end());
    if (s.self_pin_top)  pin_self_to_top(g_rows);

    uint64_t total_damage = 0;
    for (const auto& r : g_rows) total_damage += r.damage_total;

    bool open = s.window_open;
    if (ImGui::Begin("Damage", &open)) {
        ImVec2 pos  = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        capture_window_pos(pos, s.window_x, s.window_y,
                           s.window_rx, s.window_ry);
        s.window_w = size.x;
        s.window_h = size.y;

        if (ImGui::BeginPopupContextWindow("idps_ctx",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            draw_popup_settings();
            ImGui::Separator();
            if (viewing_history) ImGui::BeginDisabled();
            if (ImGui::Button("Reset fight")) {
                tracker().reset_fight();
                g_dps_cache.clear();
                ImGui::CloseCurrentPopup();
            }
            if (viewing_history) {
                ImGui::EndDisabled();
                ImGui::TextDisabled("(viewing past - go to Current to reset)");
            }
            ImGui::EndPopup();
        }

        // Past-fight state line. Only renders when viewing history so live
        // view stays uncluttered. Nav happens via right-click on a row -> Fight
        // history menu; no arrows.
        if (viewing_history) {
            uint64_t fs_start = 0, fs_end = 0;
            int storage_idx = hist_n - viewed_fight;
            tracker().fight_times_at(storage_idx, fs_start, fs_end);
            uint64_t dur_ms = fs_end > fs_start ? fs_end - fs_start : 0;
            char dbuf[32];
            format_time(dbuf, sizeof(dbuf), dur_ms);
            ImGui::Text("Viewing Fight -%d  (%s) - right-click a row to switch",
                        viewed_fight, dbuf);
            ImGui::SameLine();
            if (ImGui::SmallButton("Live")) viewed_fight = 0;
            ImGui::Separator();
        }

        // Capture available width before BeginTable so responsive-column
        // logic below can drop low-priority columns when the window shrinks.
        float table_avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        ImGuiTableFlags table_flags =
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!s.body_borders) table_flags |= ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("idps", 6, table_flags)) {
            ImGui::TableSetupColumn("Prof",
                                    ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Name",
                                    ImGuiTableColumnFlags_WidthStretch);
            // Numeric columns prefer descending on first click (high-to-low
            // is the natural read for DPS / damage / combat / share).
            ImGui::TableSetupColumn("DPS",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending,
                                    56.0f);
            ImGui::TableSetupColumn("Damage",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending,
                                    64.0f);
            ImGui::TableSetupColumn("Combat",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending,
                                    48.0f);
            ImGui::TableSetupColumn("%",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending,
                                    40.0f);
            ImGui::TableHeadersRow();

            // Drop low-priority columns as the window narrows. Sacrifice
            // order: % -> Combat -> Damage -> DPS. Prof + Name always stay.
            if (s.responsive_columns) {
                if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                    bool show_pct    = table_avail_w > 320.0f;
                    bool show_combat = table_avail_w > 270.0f;
                    bool show_dmg    = table_avail_w > 220.0f;
                    bool show_dps    = table_avail_w > 170.0f;
                    // Force enabled-state only on threshold crossings, otherwise
                    // every frame stomps the user's header-menu toggles and they
                    // can't manually hide a column.
                    static bool prev_pct = true, prev_combat = true,
                                prev_dmg = true, prev_dps = true;
                    static bool init = false;
                    if (tbl->ColumnsCount >= 6) {
                        if (!init || show_dps    != prev_dps)
                            tbl->Columns[2].IsUserEnabledNextFrame = show_dps;
                        if (!init || show_dmg    != prev_dmg)
                            tbl->Columns[3].IsUserEnabledNextFrame = show_dmg;
                        if (!init || show_combat != prev_combat)
                            tbl->Columns[4].IsUserEnabledNextFrame = show_combat;
                        if (!init || show_pct    != prev_pct)
                            tbl->Columns[5].IsUserEnabledNextFrame = show_pct;
                        prev_dps    = show_dps;
                        prev_dmg    = show_dmg;
                        prev_combat = show_combat;
                        prev_pct    = show_pct;
                        init = true;
                    }
                }
            }

            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
                if (specs->SpecsDirty) {
                    if (specs->SpecsCount > 0) {
                        int col_idx = specs->Specs[0].ColumnIndex;
                        bool ascending =
                            specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                        bool handled = true;
                        bool reverse = false;
                        switch (col_idx) {
                            case 1:
                                s.sort_mode = 2;
                                reverse = !ascending;
                                break;
                            case 2:
                                s.sort_mode = 1;
                                reverse = ascending;
                                break;
                            case 3:
                                s.sort_mode = 0;
                                reverse = ascending;
                                break;
                            case 4:
                                s.sort_mode = 3;
                                reverse = ascending;
                                break;
                            case 5:
                                s.sort_mode = 0;
                                reverse = ascending;
                                break;
                            default:
                                handled = false;
                                break;
                        }
                        if (handled) {
                            s.sort_reverse = reverse;
                            sort_rows(g_rows, s.sort_mode);
                            if (reverse) std::reverse(g_rows.begin(), g_rows.end());
                            if (s.self_pin_top) pin_self_to_top(g_rows);
                        }
                    }
                    specs->SpecsDirty = false;
                }
            }

            uint64_t max_damage = 0;
            for (const auto& r : g_rows) {
                if (r.damage_total > max_damage) max_damage = r.damage_total;
            }

            for (const auto& r : g_rows) {
                ImGui::TableNextRow();
                if (r.is_self && s.highlight_self) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           IM_COL32(80, 150, 220, 40));
                }
                ImGui::TableNextColumn();

                // Full-row damage bar drawn before any cell text so text/icons
                // render on top of the bar in the same drawlist.
                ImU32 prof_col = prof_color(r.prof);
                if (!r.in_combat) prof_col = dim_alpha(prof_col);

                if (s.bar_full_row && max_damage > 0 && r.damage_total > 0) {
                    if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                        float frac = static_cast<float>(r.damage_total) /
                                     static_cast<float>(max_damage);
                        float bar_x0 = tbl->WorkRect.Min.x;
                        float bar_x1 = tbl->WorkRect.Max.x;
                        float row_h  = ImGui::GetTextLineHeight();
                        ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                        ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                        ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x50u << 24);
                        // Background channel clips the bar to the table rect,
                        // not the current cell.
                        ImGui::TablePushBackgroundChannel();
                        ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bar_col);
                        ImGui::TablePopBackgroundChannel();
                    }
                }

                if (uint64_t tex = icon_for(r.prof, r.elite); tex != 0) {
                    align_icon_to_text();
                    ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(14, 14));
                } else {
                    ImGui::TextUnformatted(prof_short(r.prof));
                }
                ImGui::TableNextColumn();
                {
                    ImU32 text_col;
                    if (r.is_self && s.self_name_gold) {
                        text_col = IM_COL32(255, 200, 60, 255);
                    } else if (s.name_white) {
                        text_col = IM_COL32(255, 255, 255, 255);
                    } else {
                        text_col = prof_col;
                    }
                    if (!r.in_combat) text_col = dim_alpha(text_col);

                    if (!s.bar_full_row && max_damage > 0 && r.damage_total > 0) {
                        float col_w = ImGui::GetContentRegionAvail().x;
                        if (col_w > 0.0f) {
                            float frac = static_cast<float>(r.damage_total) /
                                         static_cast<float>(max_damage);
                            ImVec2 p0 = ImGui::GetCursorScreenPos();
                            float row_h = ImGui::GetTextLineHeight();
                            ImVec2 p1 = ImVec2(p0.x + col_w * frac, p0.y + row_h);
                            ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x50u << 24);
                            ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bar_col);
                        }
                    }

                    ImGui::PushStyleColor(ImGuiCol_Text, text_col);
                    ImGui::PushID(static_cast<ImGuiID>(r.id));
                    if (ImGui::Selectable(r.name.c_str(),
                                          selected_agent() == r.id && s.detail_open,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowOverlap)) {
                        set_selected_agent(r.id);
                        s.detail_open = true;
                    }
                    // Hover tooltip: top-3 skills for this agent in the
                    // currently-viewed fight (live or past). top_skills*
                    // skips the DamagePoint history copy so per-frame cost
                    // stays trivial while hovering.
                    if (ImGui::IsItemHovered() && r.damage_total > 0) {
                        static std::vector<SkillDetail> tip;
                        bool ok = viewing_history
                            ? tracker().top_skills_at(hist_n - viewed_fight,
                                                      r.id, 3, tip)
                            : tracker().top_skills(r.id, 3, tip);
                        if (ok && !tip.empty()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Top skills - %s", r.name.c_str());
                            ImGui::Separator();
                            for (const auto& sd : tip) {
                                char dmgbuf[16];
                                format_count(dmgbuf, sizeof(dmgbuf), sd.damage);
                                const char* nm = sd.name.empty() ? "(unknown)"
                                                                  : sd.name.c_str();
                                ImGui::Text("%s : %s  (%u hits)",
                                            nm, dmgbuf, sd.hits);
                            }
                            ImGui::EndTooltip();
                        }
                    }
                    // Per-agent fight history menu. Right-click row -> jump
                    // straight to a stored fight that contained this agent.
                    // The window-level popup uses NoOpenOverItems, so this
                    // item-level popup wins the right-click on a row.
                    if (ImGui::BeginPopupContextItem("##agent_fh")) {
                        ImGui::Text("Fight history - %s", r.name.c_str());
                        ImGui::Separator();
                        if (viewed_fight != 0) {
                            if (ImGui::MenuItem("Current (live)")) {
                                viewed_fight = 0;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        int n_hist = tracker().history_size();
                        if (n_hist == 0) {
                            ImGui::TextDisabled("(no past fights yet)");
                        }
                        for (int back = 1; back <= n_hist; ++back) {
                            int storage_idx = n_hist - back;
                            uint64_t fs_start = 0, fs_end = 0;
                            tracker().fight_times_at(storage_idx, fs_start, fs_end);
                            uint64_t dur = fs_end > fs_start ? fs_end - fs_start : 0;
                            char dbuf[32];
                            format_time(dbuf, sizeof(dbuf), dur);
                            Snapshot ag{};
                            char label[160];
                            if (tracker().agent_snapshot_at(storage_idx, r.id, ag)) {
                                char dpsbuf[16], dmgbuf[16];
                                format_count(dpsbuf, sizeof(dpsbuf), ag.dps);
                                format_count(dmgbuf, sizeof(dmgbuf), ag.damage_total);
                                snprintf(label, sizeof(label),
                                         "Fight -%d  (%s)   %s DPS   %s dmg",
                                         back, dbuf, dpsbuf, dmgbuf);
                            } else {
                                snprintf(label, sizeof(label),
                                         "Fight -%d  (%s)   (not present)",
                                         back, dbuf);
                            }
                            bool selected = (viewed_fight == back);
                            if (ImGui::MenuItem(label, nullptr, selected)) {
                                viewed_fight = back;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    ImGui::PopStyleColor();
                }
                ImGui::TableNextColumn();
                char buf[16];
                format_count(buf, sizeof(buf), smooth_dps(r.id, r.dps));
                ImGui::TextUnformatted(buf);
                ImGui::TableNextColumn();
                format_count(buf, sizeof(buf), r.damage_total);
                ImGui::TextUnformatted(buf);
                ImGui::TableNextColumn();
                char tbuf[32];
                format_time(tbuf, sizeof(tbuf), r.combat_ms);
                ImGui::TextUnformatted(tbuf);
                ImGui::TableNextColumn();
                if (total_damage > 0) {
                    float pct = static_cast<float>(r.damage_total) * 100.0f /
                                static_cast<float>(total_damage);
                    ImGui::Text("%.0f%%", pct);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(2);
    }
    ImGui::End();
    s.window_open = open;

    if (s.cleanses_open || s.strips_open) {
        draw_support_window("Cleanses", &s.cleanses_open, g_rows, &Snapshot::cleanse_count);
        draw_support_window("Strips",   &s.strips_open,   g_rows, &Snapshot::strip_count);
    }
    if (s.downs_open) draw_downs_window(&s.downs_open, g_rows);
    draw_detail_window(viewed_fight);

    prune_dps_cache(g_rows);
    return 0;
}

} // namespace idps
