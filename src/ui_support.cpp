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
}

// Render a support window (Cleanses / Strips). Uses an index-sort over
// the shared rows vector so we don't copy Snapshots — a 50-player squad
// with both windows open used to do 100 string copies per frame.
void draw_support_window(const char* title, bool* open,
                         const std::vector<Snapshot>& rows,
                         uint32_t Snapshot::*field) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(220, 180), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(settings().window_alpha);
    if (ImGui::Begin(title, open)) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGuiTableFlags sup_flags =
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!settings().body_borders) sup_flags |= ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("tbl", 3, sup_flags)) {
            ImGui::TableSetupColumn("Prof",  ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableHeadersRow();

            g_sort_idx.resize(rows.size());
            for (size_t i = 0; i < rows.size(); ++i) g_sort_idx[i] = i;
            std::sort(g_sort_idx.begin(), g_sort_idx.end(),
                [&rows, field](size_t a, size_t b) {
                    return rows[a].*field > rows[b].*field;
                });

            // Top count drives the per-row bar fraction. Computed once
            // outside the row loop so we don't re-scan per render.
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

                // Full-row count bar drawn on the table background channel
                // so text/icons render on top. Mirrors the Damage window's
                // bar style; fraction = count / max_count.
                if (max_count > 0 && count > 0) {
                    if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                        float frac = static_cast<float>(count) /
                                     static_cast<float>(max_count);
                        float bar_x0 = tbl->WorkRect.Min.x;
                        float bar_x1 = tbl->WorkRect.Max.x;
                        float row_h  = ImGui::GetTextLineHeight();
                        ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                        ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                        ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x50u << 24);
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
                ImGui::TableNextColumn();
                ImGui::Text("%u", count);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

void draw_downs_window(bool* open, const std::vector<Snapshot>& rows) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(settings().window_alpha);
    if (ImGui::Begin("Down contribution", open)) {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGuiTableFlags f =
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!settings().body_borders) f |= ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("downs", 4, f)) {
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
            ImGui::TableHeadersRow();

            // Sort by active spec — defaults to Contrib desc via DefaultSort
            // flag above. Click the Contrib or Downs / Name header to switch.
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
                        case 2:
                        default: less = ra.damage_to_downed <
                                        rb.damage_to_downed; break;
                    }
                    return ascending ? less : !less;
                });

            // Top contribution drives the per-row bar fraction. Computed
            // once outside the loop so it's a single pass per render.
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

                // Full-row contribution bar drawn on the table background
                // channel so text/icons render on top. Fraction =
                // damage_to_downed / max_contrib, prof-colored.
                if (max_contrib > 0 && r.damage_to_downed > 0) {
                    if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                        float frac = static_cast<float>(r.damage_to_downed) /
                                     static_cast<float>(max_contrib);
                        float bar_x0 = tbl->WorkRect.Min.x;
                        float bar_x1 = tbl->WorkRect.Max.x;
                        float row_h  = ImGui::GetTextLineHeight();
                        ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                        ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                        ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x50u << 24);
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
                ImGui::TableNextColumn();
                char buf[16];
                format_count(buf, sizeof(buf), r.damage_to_downed);
                ImGui::TextUnformatted(buf);
                ImGui::TableNextColumn();
                ImGui::Text("%u", r.downs_contributed);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
}

} // namespace idps
