#include "ui.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
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
    // EMA smoothing for the live DPS column so it doesn't tick like a
    // stopwatch every frame. Cadence ~500ms; weight 3:1 (old:new).
    struct DpsCache {
        uint64_t last_wall = 0;
        uint64_t shown     = 0;
    };
    std::unordered_map<uintptr_t, DpsCache> g_dps_cache;

    // Reusable per-frame buffer — sits in static storage so we don't realloc
    // a new vector + per-agent strings every render.
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

    // Drop EMA cache entries for agents that have left the snapshot. Keeps
    // the cache from accumulating stale ids across long sessions (WvW, etc).
    void prune_dps_cache(const std::vector<Snapshot>& rows) {
        // Always rebuild the live set; agent churn (one leaves, one joins)
        // can keep the cache size equal to rows.size() while a stale id
        // lingers. Cost is one set-build per frame for ~50 squadmates.
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

    tracker().snapshot(g_rows);
    sort_rows(g_rows, s.sort_mode);
    if (s.sort_reverse) std::reverse(g_rows.begin(), g_rows.end());

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
            draw_options_controls();
            ImGui::Separator();
            if (ImGui::Button("Reset fight")) {
                tracker().reset_fight();
                g_dps_cache.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Capture available width before BeginTable so we can drop
        // low-priority columns when the user shrinks the window.
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
            // Numeric columns prefer descending on first click — DPS / damage /
            // combat-time / share are conventionally read high-to-low.
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

            // Drop low-priority columns as the window narrows. Order of
            // sacrifice: % -> Combat -> Damage -> DPS. Prof + Name always
            // stay. Thresholds are the column's own fixed width plus typical
            // cell padding; below that the column eats more space than its
            // information value. Skipped when the user disables responsive
            // mode (lets the Hideable header menu drive instead).
            if (s.responsive_columns) {
                if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                    bool show_pct    = table_avail_w > 320.0f;
                    bool show_combat = table_avail_w > 270.0f;
                    bool show_dmg    = table_avail_w > 220.0f;
                    bool show_dps    = table_avail_w > 170.0f;
                    // Only force enabled-state on threshold crossings so the
                    // user's manual header-menu toggles persist between width
                    // changes. Without this guard, every frame stomps the user
                    // checkbox and they can't hide DPS / Damage / Combat / %.
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
                    // Subtle blue tint over the row's striped bg so the local
                    // player is identifiable at a glance without overriding
                    // profession color on the name itself.
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           IM_COL32(80, 150, 220, 40));
                }
                ImGui::TableNextColumn();

                // Full-row damage bar drawn here (before any cell text) so
                // text/icons render on top of the bar in the same drawlist.
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
                        // Draw on the table's background channel so the bar
                        // is clipped to the table rect, not the current cell.
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

                    // Per-cell bar (Name column only) when full-row is off.
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
    draw_detail_window();

    prune_dps_cache(g_rows);
    return 0;
}

} // namespace idps
