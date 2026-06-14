#include "ui_support.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>

#include "icons.h"
#include "settings.h"
#include "ui_common.h"

namespace idps {

namespace {
    // Reused index buffer so we don't reallocate per render.
    std::vector<size_t> g_sort_idx;

    // "Title (Fight -N)###Title" — the ### keeps the window identity (and
    // saved position/size) stable while the visible label tracks the view.
    void fight_window_title(char* out, size_t n, const char* base,
                            int viewed_fight) {
        if (viewed_fight > 0) {
            std::snprintf(out, n, "%s (Fight -%d)###%s",
                          base, viewed_fight, base);
        } else {
            std::snprintf(out, n, "%s###%s", base, base);
        }
    }

    // Header strip shown while viewing a past fight: label + Live button.
    void fight_view_header(int viewed_fight, bool* go_live) {
        if (viewed_fight <= 0) return;
        ImGui::TextDisabled("Viewing Fight -%d", viewed_fight);
        ImGui::SameLine();
        if (ImGui::SmallButton("Live")) *go_live = true;
        ImGui::Separator();
    }
}

// Index-sort over the shared rows vector to avoid Snapshot copies — a
// 50-player squad with both Cleanses + Strips windows open would otherwise
// do ~100 string copies per frame.
void draw_support_window(const char* title, bool* open,
                         const std::vector<Snapshot>& rows,
                         uint32_t Snapshot::*field,
                         int viewed_fight, bool* go_live) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(220, 180), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(settings().window_alpha);
    ImGuiWindowFlags lock_flags = settings().lock_windows
        ? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)
        : 0;
    char wtitle[96];
    fight_window_title(wtitle, sizeof(wtitle), title, viewed_fight);
    if (ImGui::Begin(wtitle, open, lock_flags)) {
        fight_view_header(viewed_fight, go_live);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGuiTableFlags sup_flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!settings().body_borders) sup_flags |= ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("tbl", 3, sup_flags)) {
            ImGui::TableSetupColumn("Prof",  ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
            // Count column is labeled per-window via the title arg so the
            // Cleanses window reads "Cleanses" and Strips reads "Strips".
            ImGui::TableSetupColumn(title, ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableHeadersRow();

            g_sort_idx.resize(rows.size());
            for (size_t i = 0; i < rows.size(); ++i) g_sort_idx[i] = i;
            std::sort(g_sort_idx.begin(), g_sort_idx.end(),
                [&rows, field](size_t a, size_t b) {
                    return rows[a].*field > rows[b].*field;
                });
            if (settings().self_pin_top) pin_self_to_top(g_sort_idx, rows);

            // Top count drives the per-row bar fraction. Computed once
            // outside the row loop.
            uint32_t max_count = 0;
            for (size_t i : g_sort_idx) {
                uint32_t c = rows[i].*field;
                if (c > max_count) max_count = c;
            }

            for (size_t i : g_sort_idx) {
                const auto& r = rows[i];
                uint32_t count = r.*field;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImU32 prof_col = prof_color(r.prof);
                if (!r.in_combat) prof_col = dim_alpha(prof_col);

                if (max_count > 0 && count > 0) {
                    if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                        float frac = static_cast<float>(count) /
                                     static_cast<float>(max_count);
                        float bar_x0 = tbl->WorkRect.Min.x;
                        float bar_x1 = tbl->WorkRect.Max.x;
                        float row_h  = ImGui::GetTextLineHeight();
                        ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                        ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                        ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x80u << 24);
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
                ImU32 col;
                if (r.is_self && settings().self_name_gold) {
                    col = IM_COL32(255, 200, 60, 255);
                } else if (settings().name_white) {
                    col = IM_COL32(255, 255, 255, 255);
                } else {
                    col = prof_color(r.prof);
                }
                if (!r.in_combat) col = dim_alpha(col);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(r.name.c_str());
                ImGui::PopStyleColor();
                account_tooltip(r.account);
                ImGui::TableNextColumn();
                ImGui::Text("%u", count);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

void draw_downs_window(bool* open, const std::vector<Snapshot>& rows,
                       int viewed_fight, bool* go_live) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(settings().window_alpha);
    ImGuiWindowFlags lock_flags = settings().lock_windows
        ? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)
        : 0;
    char wtitle[96];
    fight_window_title(wtitle, sizeof(wtitle), "Down contribution",
                       viewed_fight);
    if (ImGui::Begin(wtitle, open, lock_flags)) {
        fight_view_header(viewed_fight, go_live);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGuiTableFlags f =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!settings().body_borders) f |= ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("downs", 5, f)) {
            ImGui::TableSetupColumn("Prof",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_NoSort, 22.0f);
            ImGui::TableSetupColumn("Name",
                                    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Contrib",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                    ImGuiTableColumnFlags_DefaultSort, 64.0f);
            ImGui::TableSetupColumn("Downs",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending,
                                    44.0f);
            ImGui::TableSetupColumn("Kills",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending,
                                    40.0f);
            ImGui::TableHeadersRow();

            int  sort_col = 2;
            bool ascending = false;
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
                if (specs->SpecsCount > 0) {
                    sort_col  = specs->Specs[0].ColumnIndex;
                    ascending = specs->Specs[0].SortDirection ==
                                ImGuiSortDirection_Ascending;
                }
            }

            g_sort_idx.resize(rows.size());
            for (size_t i = 0; i < rows.size(); ++i) g_sort_idx[i] = i;
            std::sort(g_sort_idx.begin(), g_sort_idx.end(),
                [&rows, sort_col, ascending](size_t a, size_t b) {
                    const auto& ra = rows[a];
                    const auto& rb = rows[b];
                    bool less;
                    switch (sort_col) {
                        case 1:  less = ra.name < rb.name; break;
                        case 3:  less = ra.downs_contributed <
                                        rb.downs_contributed; break;
                        case 4:  less = ra.kills_contributed <
                                        rb.kills_contributed; break;
                        case 2:
                        default: less = ra.damage_to_downed <
                                        rb.damage_to_downed; break;
                    }
                    return ascending ? less : !less;
                });
            if (settings().self_pin_top) pin_self_to_top(g_sort_idx, rows);

            // Top contribution drives the per-row bar fraction. Computed
            // once outside the row loop.
            uint64_t max_contrib = 0;
            for (size_t i : g_sort_idx) {
                if (rows[i].damage_to_downed > max_contrib)
                    max_contrib = rows[i].damage_to_downed;
            }

            for (size_t i : g_sort_idx) {
                const auto& r = rows[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                ImU32 prof_col = prof_color(r.prof);
                if (!r.in_combat) prof_col = dim_alpha(prof_col);

                if (max_contrib > 0 && r.damage_to_downed > 0) {
                    if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                        float frac = static_cast<float>(r.damage_to_downed) /
                                     static_cast<float>(max_contrib);
                        float bar_x0 = tbl->WorkRect.Min.x;
                        float bar_x1 = tbl->WorkRect.Max.x;
                        float row_h  = ImGui::GetTextLineHeight();
                        ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                        ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                        ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x80u << 24);
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
                ImU32 col;
                if (r.is_self && settings().self_name_gold) {
                    col = IM_COL32(255, 200, 60, 255);
                } else if (settings().name_white) {
                    col = IM_COL32(255, 255, 255, 255);
                } else {
                    col = prof_color(r.prof);
                }
                if (!r.in_combat) col = dim_alpha(col);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(r.name.c_str());
                ImGui::PopStyleColor();
                account_tooltip(r.account);
                ImGui::TableNextColumn();
                char buf[16];
                format_count(buf, sizeof(buf), r.damage_to_downed);
                ImGui::TextUnformatted(buf);
                ImGui::TableNextColumn();
                ImGui::Text("%u", r.downs_contributed);
                ImGui::TableNextColumn();
                ImGui::Text("%u", r.kills_contributed);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

} // namespace idps
