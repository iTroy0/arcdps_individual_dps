#include "ui_support.h"

#include <imgui.h>

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

            for (size_t i : g_sort_idx) {
                const auto& r = rows[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (uint64_t tex = icon_for(r.prof, r.elite); tex != 0) {
                    align_icon_to_text();
                    ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2(14, 14));
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
                ImGui::Text("%u", r.*field);
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
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!settings().body_borders) f |= ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("downs", 4, f)) {
            ImGui::TableSetupColumn("Prof",  ImGuiTableColumnFlags_WidthFixed,   22.0f);
            ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Contrib", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("Downs", ImGuiTableColumnFlags_WidthFixed,   44.0f);
            ImGui::TableHeadersRow();

            g_sort_idx.resize(rows.size());
            for (size_t i = 0; i < rows.size(); ++i) g_sort_idx[i] = i;
            std::sort(g_sort_idx.begin(), g_sort_idx.end(),
                [&rows](size_t a, size_t b) {
                    return rows[a].damage_to_downed > rows[b].damage_to_downed;
                });

            for (size_t i : g_sort_idx) {
                const auto& r = rows[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (uint64_t tex = icon_for(r.prof, r.elite); tex != 0) {
                    align_icon_to_text();
                    ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2(14, 14));
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
