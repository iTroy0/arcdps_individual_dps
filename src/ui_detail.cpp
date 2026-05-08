#include "ui_detail.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "settings.h"
#include "tracker.h"
#include "ui_common.h"

namespace idps {

namespace {
    uintptr_t   g_selected_agent = 0;
    AgentDetail g_detail;

    // 0 = nothing selected. Drives the spike overlay on the DPS graph.
    uint32_t    g_selected_skill = 0;
}

void      set_selected_agent(uintptr_t id) {
    if (g_selected_agent != id) g_selected_skill = 0;
    g_selected_agent = id;
}
uintptr_t selected_agent()                 { return g_selected_agent; }

bool consume_esc_for_detail() {
    auto& s = settings();
    if (s.detail_open && g_selected_agent != 0) {
        s.detail_open = false;
        return true;
    }
    return false;
}

void draw_detail_window() {
    auto& s = settings();
    if (!s.detail_open || g_selected_agent == 0) return;

    tracker().detail(g_selected_agent, g_detail);
    const auto& d = g_detail;
    if (d.name.empty()) {
        s.detail_open = false;
        return;
    }

    char title[128];
    std::snprintf(title, sizeof(title), "%s - details###idps_detail", d.name.c_str());

    static bool   prev_rel = false;
    static ImVec2 prev_ds(0.0f, 0.0f);
    apply_window_pos(s.detail_x, s.detail_y, s.detail_rx, s.detail_ry,
                     s.pos_relative, prev_rel, prev_ds);
    ImGui::SetNextWindowSize(ImVec2(s.detail_w, s.detail_h),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(s.window_alpha);

    bool open = s.detail_open;
    if (ImGui::Begin(title, &open)) {
        // ESC dismissal lives in mod_wnd_filter (exports.cpp) via
        // consume_esc_for_detail() — arc swallows ESC at the wndproc layer
        // before ImGui::IsKeyPressed sees it.
        ImVec2 dpos = ImGui::GetWindowPos();
        ImVec2 dsiz = ImGui::GetWindowSize();
        capture_window_pos(dpos, s.detail_x, s.detail_y,
                           s.detail_rx, s.detail_ry);
        s.detail_w = dsiz.x;
        s.detail_h = dsiz.y;

        // Per-sample DPS over a 1-second lookback window. The raw history
        // is sampled every 500ms, so adjacent-pair diffs double-count short
        // bursts and inflate peaks vs arc's panel — a 1s window matches
        // arc's peak more closely. Manual plot (instead of PlotLines) so
        // the hover tooltip can format values with a "k" suffix.
        std::vector<float> samples;
        samples.reserve(d.history.size());
        float peak_dps = 0.0f;
        for (size_t i = 1; i < d.history.size(); ++i) {
            uint64_t target = d.history[i].wall_ms > 1000
                            ? d.history[i].wall_ms - 1000 : 0;
            size_t j = i;
            while (j > 0 && d.history[j - 1].wall_ms >= target) --j;
            uint64_t dd = d.history[i].damage_total - d.history[j].damage_total;
            uint64_t dt = d.history[i].wall_ms      - d.history[j].wall_ms;
            if (dt < 1000) dt = 1000;  // floor to 1s window
            float dps = static_cast<float>(dd) * 1000.0f / static_cast<float>(dt);
            samples.push_back(dps);
            if (dps > peak_dps) peak_dps = dps;
        }
        const SkillDetail* sel_skill = nullptr;
        if (g_selected_skill != 0) {
            for (const auto& sk : d.skills) {
                if (sk.skill_id == g_selected_skill) { sel_skill = &sk; break; }
            }
        }
        if (sel_skill) {
            const char* nm = !sel_skill->name.empty()
                           ? sel_skill->name.c_str() : "?";
            ImGui::Text("DPS over time  peak: %.1fk  |  ",
                        peak_dps / 1000.0f);
            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 90, 255));
            ImGui::Text("spike: %s (#%u)", nm, sel_skill->skill_id);
            ImGui::PopStyleColor();
        } else {
            ImGui::Text("DPS over time  peak: %.1fk", peak_dps / 1000.0f);
        }

        const float axis_w  = 50.0f;
        const float graph_h = 120.0f;
        float total_w = ImGui::GetContentRegionAvail().x;
        float graph_w = total_w - axis_w;
        if (!samples.empty() && peak_dps > 0.0f && graph_w > 20.0f) {
            ImVec2 origin   = ImGui::GetCursorScreenPos();
            ImVec2 graph_p0 = ImVec2(origin.x + axis_w, origin.y);
            ImVec2 graph_p1 = ImVec2(graph_p0.x + graph_w, graph_p0.y + graph_h);
            auto* dl = ImGui::GetWindowDrawList();

            // InvisibleButton first so visuals render on top of the hit rect.
            ImGui::InvisibleButton("##graph", ImVec2(total_w, graph_h));
            bool hovered = ImGui::IsItemHovered();

            dl->AddRectFilled(graph_p0, graph_p1, IM_COL32(20, 20, 20, 180));
            dl->AddRect      (graph_p0, graph_p1, IM_COL32(80, 80, 80, 255));

            const ImU32 grid_col  = IM_COL32(60, 60, 60, 255);
            const ImU32 label_col = IM_COL32(180, 180, 180, 255);
            for (int g = 0; g <= 4; ++g) {
                float t  = static_cast<float>(g) / 4.0f;
                float y  = graph_p0.y + (1.0f - t) * graph_h;
                if (g > 0 && g < 4)
                    dl->AddLine({graph_p0.x, y}, {graph_p1.x, y}, grid_col, 1.0f);
                char lbl[16];
                float v = peak_dps * t;
                if (v >= 1000.0f) std::snprintf(lbl, sizeof(lbl), "%.1fk", v / 1000.0f);
                else              std::snprintf(lbl, sizeof(lbl), "%.0f",  v);
                ImVec2 ts = ImGui::CalcTextSize(lbl);
                dl->AddText({graph_p0.x - ts.x - 4.0f, y - ts.y * 0.5f}, label_col, lbl);
            }

            const int n = static_cast<int>(samples.size());
            for (int i = 0; i < n - 1; ++i) {
                float t0 = static_cast<float>(i)     / static_cast<float>(n - 1);
                float t1 = static_cast<float>(i + 1) / static_cast<float>(n - 1);
                float y0 = graph_p1.y - (samples[i]     / peak_dps) * graph_h;
                float y1 = graph_p1.y - (samples[i + 1] / peak_dps) * graph_h;
                dl->AddLine({graph_p0.x + t0 * graph_w, y0},
                            {graph_p0.x + t1 * graph_w, y1},
                            IM_COL32(110, 180, 255, 255), 1.5f);
            }

            // Skill spike overlay: per-hit vertical bar at its wall-time,
            // height ∝ damage. Time anchors come from the same history
            // range as sample placement so the overlay stays aligned to
            // the DPS curve.
            if (sel_skill && !sel_skill->hits_history.empty() &&
                d.history.size() >= 2) {
                uint64_t t_start = d.history[1].wall_ms;
                uint64_t t_end   = d.history.back().wall_ms;
                if (t_end > t_start) {
                    uint64_t max_hit_dmg = 0;
                    for (const auto& h : sel_skill->hits_history) {
                        if (h.damage > max_hit_dmg) max_hit_dmg = h.damage;
                    }
                    if (max_hit_dmg > 0) {
                        const ImU32 spike_col = IM_COL32(255, 220, 90, 200);
                        const float span = static_cast<float>(t_end - t_start);
                        for (const auto& h : sel_skill->hits_history) {
                            if (h.wall_ms < t_start || h.wall_ms > t_end) continue;
                            float t  = static_cast<float>(h.wall_ms - t_start) / span;
                            float x  = graph_p0.x + t * graph_w;
                            float bh = (static_cast<float>(h.damage) /
                                        static_cast<float>(max_hit_dmg)) * graph_h;
                            dl->AddLine({x, graph_p1.y},
                                        {x, graph_p1.y - bh},
                                        spike_col, 1.5f);
                        }
                    }
                }
            }

            if (hovered) {
                float mx = ImGui::GetIO().MousePos.x - graph_p0.x;
                if (mx < 0.0f)     mx = 0.0f;
                if (mx > graph_w)  mx = graph_w;
                int idx = static_cast<int>((mx / graph_w) * (n - 1) + 0.5f);
                if (idx < 0) idx = 0;
                if (idx >= n) idx = n - 1;

                float v = samples[idx];
                float hx = graph_p0.x + (n > 1
                    ? (static_cast<float>(idx) / static_cast<float>(n - 1)) * graph_w
                    : graph_w * 0.5f);
                float hy = graph_p1.y - (v / peak_dps) * graph_h;

                dl->AddLine({hx, graph_p0.y}, {hx, graph_p1.y},
                            IM_COL32(255, 255, 255, 100), 1.0f);
                dl->AddCircleFilled({hx, hy}, 3.0f,
                            IM_COL32(255, 255, 255, 220));

                uint64_t sample_ms = (idx + 1) < static_cast<int>(d.history.size())
                                   ? d.history[idx + 1].wall_ms - d.history_start_wall
                                   : 0;
                uint64_t cumulative = (idx + 1) < static_cast<int>(d.history.size())
                                   ? d.history[idx + 1].damage_total
                                   : 0;

                char tbuf[16];
                format_time(tbuf, sizeof(tbuf), sample_ms);
                char cbuf[16];
                format_count(cbuf, sizeof(cbuf), cumulative);

                ImGui::BeginTooltip();
                ImGui::Text("t = %s", tbuf);
                if (v >= 1000.0f) ImGui::Text("DPS:    %.1fk", v / 1000.0f);
                else              ImGui::Text("DPS:    %.0f",  v);
                ImGui::Text("Total:  %s", cbuf);
                ImGui::EndTooltip();
            }
        } else {
            ImGui::Dummy(ImVec2(total_w, graph_h));
            ImGui::TextDisabled("(not enough data yet)");
        }

        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGuiTableFlags sk_flags =
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!s.body_borders) sk_flags |= ImGuiTableFlags_NoBordersInBody;
        // tracker::detail() pre-sorts skills descending, so [0] is the top.
        uint64_t top_sk_dmg = d.skills.empty() ? 0 : d.skills.front().damage;

        // Drop selection if it points at a skill no longer in the snapshot
        // (fight reset, etc.).
        if (g_selected_skill != 0) {
            bool still_present = false;
            for (const auto& sk : d.skills) {
                if (sk.skill_id == g_selected_skill) { still_present = true; break; }
            }
            if (!still_present) g_selected_skill = 0;
        }

        if (ImGui::BeginTable("skills", 6, sk_flags)) {
            ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Skill",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Damage", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("%",      ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("DPS",    ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Hits",   ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int rank = 0;
            for (const auto& sk : d.skills) {
                ++rank;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                if (top_sk_dmg > 0 && sk.damage > 0) {
                    if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                        float frac = static_cast<float>(sk.damage) /
                                     static_cast<float>(top_sk_dmg);
                        float bar_x0 = tbl->WorkRect.Min.x;
                        float bar_x1 = tbl->WorkRect.Max.x;
                        float row_h  = ImGui::GetTextLineHeight();
                        ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                        ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                        ImU32 bar_col = (sk.skill_id == g_selected_skill)
                                        ? IM_COL32(255, 220, 90, 110)
                                        : IM_COL32(110, 180, 255,  70);
                        ImGui::TablePushBackgroundChannel();
                        ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bar_col);
                        ImGui::TablePopBackgroundChannel();
                    }
                }

                ImGui::PushID(static_cast<int>(sk.skill_id));
                bool is_sel = (g_selected_skill == sk.skill_id);
                char id_lbl[24];
                std::snprintf(id_lbl, sizeof(id_lbl), "%d##rk", rank);
                if (ImGui::Selectable(id_lbl, is_sel,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap)) {
                    g_selected_skill = is_sel ? 0u : sk.skill_id;
                }
                ImGui::PopID();

                ImGui::TableNextColumn();
                if (!sk.name.empty())
                    ImGui::TextUnformatted(sk.name.c_str());
                else
                    ImGui::Text("#%u", sk.skill_id);
                ImGui::TableNextColumn();
                char buf[16];
                format_count(buf, sizeof(buf), sk.damage);
                ImGui::TextUnformatted(buf);
                ImGui::TableNextColumn();
                if (top_sk_dmg > 0) {
                    float pct = static_cast<float>(sk.damage) * 100.0f /
                                static_cast<float>(top_sk_dmg);
                    ImGui::Text("%.0f%%", pct);
                } else {
                    ImGui::TextUnformatted("-");
                }
                ImGui::TableNextColumn();
                // DPS over the skill's active window (first hit -> last hit),
                // not total fight time. Matches arc's "/as" per-active-second.
                uint64_t sk_window = sk.last_hit_wall > sk.first_hit_wall
                                   ? sk.last_hit_wall - sk.first_hit_wall : 0;
                uint64_t sk_denom  = sk_window < 1000 ? 1000 : sk_window;
                uint64_t sk_dps    = sk.damage * 1000ull / sk_denom;
                format_count(buf, sizeof(buf), sk_dps);
                ImGui::TextUnformatted(buf);
                ImGui::TableNextColumn();
                ImGui::Text("%u", sk.hits);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    s.detail_open = open;
}

} // namespace idps
