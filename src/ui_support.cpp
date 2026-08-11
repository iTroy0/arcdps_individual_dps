#include "ui_support.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>

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

    ImGuiWindowFlags window_lock_flags() {
        return settings().lock_windows
             ? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)
             : 0;
    }

    ImGuiTableFlags support_table_flags(bool sortable) {
        ImGuiTableFlags f =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_Reorderable | ImGuiTableFlags_ScrollY;
        if (sortable)                  f |= ImGuiTableFlags_Sortable;
        if (!settings().body_borders)  f |= ImGuiTableFlags_NoBordersInBody;
        return f;
    }

    // Opens a row and renders the two leading cells every support window
    // shares: the profession icon and the coloured name. `frac` is the
    // row's share of the window's top value and drives the background bar
    // (0 draws none). Leaves the cursor in the name cell, so the caller
    // continues with TableNextColumn for its own value columns.
    void draw_row_head(const Snapshot& r, float frac) {
        const auto& s = settings();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        // Text takes the base profession shade, the bar the highlight one —
        // the same split arcdps draws with.
        ImU32 prof_col = prof_color(r.prof);
        ImU32 bar_base = prof_color_highlight(r.prof);
        if (!r.in_combat) {
            prof_col = dim_color(prof_col);
            bar_base = dim_color(bar_base);
        }

        if (frac > 0.0f) {
            if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                float bar_x0 = tbl->WorkRect.Min.x;
                float bar_x1 = tbl->WorkRect.Max.x;
                float row_h  = ImGui::GetTextLineHeight();
                ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                ImU32 bar_col = with_alpha(bar_base, s.bar_alpha);
                // Background channel clips the bar to the table rect rather
                // than to the current cell.
                ImGui::TablePushBackgroundChannel();
                ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bar_col);
                ImGui::TablePopBackgroundChannel();
            }
        }

        if (uint64_t tex = icon_for(r.prof, r.elite); tex != 0) {
            align_icon_to_text();
            ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(14, 14));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, prof_col);
            ImGui::TextUnformatted(prof_short(r.prof));
            ImGui::PopStyleColor();
        }
        {
            char spec[32];
            format_spec(spec, sizeof(spec), r.prof, r.elite);
            item_tooltip(spec);
        }

        ImGui::TableNextColumn();
        // Default text colour, matching the value column beside it, and
        // never dimmed out of combat — the icon and the bar already carry
        // the combat state. See the same treatment in ui.cpp.
        bool tint_name = r.is_self && s.self_name_gold;
        if (tint_name)
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 60, 255));
        ImGui::TextUnformatted(r.name.c_str());
        if (tint_name) ImGui::PopStyleColor();
        account_tooltip(r.account);
    }

    // Ratio of `v` to `max`, clamped to [0,1]. 0 when there is nothing to
    // scale against, which draws no bar.
    float bar_fraction(uint64_t v, uint64_t max) {
        if (max == 0 || v == 0) return 0.0f;
        float f = static_cast<float>(v) / static_cast<float>(max);
        return f > 1.0f ? 1.0f : f;
    }

    // Fill g_sort_idx with row indices ordered by `less`, honouring the
    // self-pin setting.
    template <typename Less>
    void build_sort_index(const std::vector<Snapshot>& rows, Less less) {
        g_sort_idx.resize(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) g_sort_idx[i] = i;
        std::sort(g_sort_idx.begin(), g_sort_idx.end(), less);
        if (settings().self_pin_top) pin_self_to_top(g_sort_idx, rows);
    }

    // Fold the header row's sort choice into the caller's persisted state.
    // With the header row hidden there is nothing to click and the table
    // still reports its DefaultSort spec, so reading it would overwrite the
    // user's pick from the right-click menu — hence the early return.
    void resolve_sort(int& col, bool& ascending) {
        if (!settings().show_headers) return;
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsCount > 0) {
                col       = specs->Specs[0].ColumnIndex;
                ascending = specs->Specs[0].SortDirection ==
                            ImGuiSortDirection_Ascending;
            }
        }
    }

    // Right-click sort menu, the route that survives a hidden header row.
    // labels[i] names the column at index first_col + i.
    void sort_context_menu(const char* id, const char* const* labels,
                           int count, int first_col,
                           int& col, bool& ascending) {
        if (!ImGui::BeginPopupContextWindow(id,
                ImGuiPopupFlags_MouseButtonRight)) return;
        // count == 0 for the windows whose order is fixed; they still want
        // the header toggle so it is reachable from any window, not just the
        // ones that happen to be sortable.
        if (count > 0) {
            ImGui::TextDisabled("Sort by");
            for (int i = 0; i < count; ++i) {
                int c = first_col + i;
                if (ImGui::MenuItem(labels[i], nullptr, col == c)) {
                    // Re-picking the active column flips direction, the same
                    // as clicking its header twice.
                    if (col == c) ascending = !ascending;
                    else          { col = c; ascending = false; }
                }
            }
            ImGui::Separator();
        }
        auto& s = settings();
        bool hdr = s.show_headers;
        if (ImGui::MenuItem("Column headers", nullptr, hdr))
            s.show_headers = !hdr;
        ImGui::EndPopup();
    }

    void setup_leading_columns() {
        ImGui::TableSetupColumn("Prof",
                                ImGuiTableColumnFlags_WidthFixed |
                                ImGuiTableColumnFlags_NoSort, 22.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    }

    void table_headers() {
        if (settings().show_headers) ImGui::TableHeadersRow();
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
    char wtitle[96];
    fight_window_title(wtitle, sizeof(wtitle), title, viewed_fight);
    if (ImGui::Begin(wtitle, open, window_lock_flags())) {
        fight_view_header(viewed_fight, go_live);
        {
            // Order is fixed (descending count), so no sort items — the menu
            // exists here purely to reach the header toggle.
            int  unused_col = 0;
            bool unused_asc = false;
            sort_context_menu("##sup_ctx", nullptr, 0, 0,
                              unused_col, unused_asc);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        if (ImGui::BeginTable("tbl", 3, support_table_flags(/*sortable=*/false))) {
            setup_leading_columns();
            // Count column is labeled per-window via the title arg so the
            // Cleanses window reads "Cleanses" and Strips reads "Strips".
            ImGui::TableSetupColumn(title, ImGuiTableColumnFlags_WidthFixed, 64.0f);
            table_headers();

            build_sort_index(rows, [&rows, field](size_t a, size_t b) {
                return rows[a].*field > rows[b].*field;
            });

            uint32_t max_count = 0;
            for (size_t i : g_sort_idx) {
                uint32_t c = rows[i].*field;
                if (c > max_count) max_count = c;
            }

            for (size_t i : g_sort_idx) {
                const auto& r = rows[i];
                uint32_t count = r.*field;
                draw_row_head(r, bar_fraction(count, max_count));
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
    char wtitle[96];
    fight_window_title(wtitle, sizeof(wtitle), "Down contribution",
                       viewed_fight);
    if (ImGui::Begin(wtitle, open, window_lock_flags())) {
        fight_view_header(viewed_fight, go_live);
        {
            static const char* kCols[] = {"Name", "Contrib", "Downs", "Kills"};
            sort_context_menu("##downs_ctx", kCols, 4, /*first_col=*/1,
                              settings().downs_sort, settings().downs_sort_asc);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        if (ImGui::BeginTable("downs", 5, support_table_flags(/*sortable=*/true))) {
            setup_leading_columns();
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
            table_headers();

            int&  sort_col  = settings().downs_sort;
            bool& ascending = settings().downs_sort_asc;
            resolve_sort(sort_col, ascending);

            build_sort_index(rows, [&rows, sort_col, ascending](size_t a, size_t b) {
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

            uint64_t max_contrib = 0;
            for (size_t i : g_sort_idx) {
                if (rows[i].damage_to_downed > max_contrib)
                    max_contrib = rows[i].damage_to_downed;
            }

            for (size_t i : g_sort_idx) {
                const auto& r = rows[i];
                draw_row_head(r, bar_fraction(r.damage_to_downed, max_contrib));
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
