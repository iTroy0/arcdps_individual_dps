#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <windows.h>

#include "icons.h"
#include "settings.h"
#include "tracker.h"

namespace idps {

namespace {
    struct DpsCache {
        uint64_t last_wall = 0;
        uint64_t shown     = 0;
    };
    std::unordered_map<uintptr_t, DpsCache> g_dps_cache;
    bool g_window_pos_applied = false;
    uintptr_t g_selected_agent = 0;
    bool g_detail_open = false;

    uint64_t smooth_dps(uintptr_t id, uint64_t instant) {
        auto& c = g_dps_cache[id];
        uint64_t now = GetTickCount64();
        if (instant == 0) {
            c.last_wall = now;
            c.shown = 0;
            return 0;
        }
        if (c.shown == 0) {
            c.last_wall = now;
            c.shown = instant;
            return instant;
        }
        if (now - c.last_wall >= 500) {
            c.last_wall = now;
            c.shown = (c.shown * 3 + instant) / 4;
        }
        return c.shown;
    }

    ImU32 prof_color(uint32_t prof) {
        switch (prof) {
            case 1: return IM_COL32(114, 193, 217, 255); // Guardian
            case 2: return IM_COL32(255, 209, 102, 255); // Warrior
            case 3: return IM_COL32(208, 156,  89, 255); // Engineer
            case 4: return IM_COL32(140, 220, 130, 255); // Ranger
            case 5: return IM_COL32(192, 143, 149, 255); // Thief
            case 6: return IM_COL32(246, 138, 135, 255); // Elementalist
            case 7: return IM_COL32(182, 121, 213, 255); // Mesmer
            case 8: return IM_COL32( 82, 167, 111, 255); // Necromancer
            case 9: return IM_COL32(209, 110,  90, 255); // Revenant
            default: return IM_COL32(200, 200, 200, 255);
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

    void draw_detail_window() {
        if (!g_detail_open || g_selected_agent == 0) return;
        auto d = tracker().detail(g_selected_agent);
        if (d.name.empty()) {
            g_detail_open = false;
            return;
        }

        char title[128];
        std::snprintf(title, sizeof(title), "%s - details###idps_detail", d.name.c_str());

        ImGui::SetNextWindowSize(ImVec2(420, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &g_detail_open)) {
            // Damage over time graph.
            // DPS-over-time: first difference of the cumulative damage
            // samples divided by the elapsed wall-ms between them.
            // Samples in kilo-DPS so ImGui's hardcoded "%8.4g" hover format
            // doesn't flip to scientific notation above 10k.
            std::vector<float> samples;
            samples.reserve(d.history.size());
            float peak_dps_k = 0.0f;
            float peak_dps   = 0.0f;
            for (size_t i = 1; i < d.history.size(); ++i) {
                uint64_t dd = d.history[i].damage_total - d.history[i-1].damage_total;
                uint64_t dt = d.history[i].wall_ms      - d.history[i-1].wall_ms;
                float dps   = dt > 0 ? (static_cast<float>(dd) * 1000.0f / static_cast<float>(dt)) : 0.0f;
                float dps_k = dps / 1000.0f;
                samples.push_back(dps_k);
                if (dps_k > peak_dps_k) peak_dps_k = dps_k;
                if (dps   > peak_dps)   peak_dps   = dps;
            }
            ImGui::Text("DPS over time (k)  peak: %.1fk", peak_dps_k);
            if (samples.size() >= 2 && peak_dps_k > 0.0f) {
                ImGui::PlotLines("##graph", samples.data(),
                                 static_cast<int>(samples.size()),
                                 0, nullptr,
                                 0.0f, peak_dps_k,
                                 ImVec2(ImGui::GetContentRegionAvail().x, 100.0f));
            } else {
                ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 100.0f));
                ImGui::TextDisabled("(not enough data yet)");
            }

            ImGui::Separator();
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
            if (ImGui::BeginTable("skills", 4,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("Skill",  ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Damage", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("DPS",    ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableSetupColumn("Hits",   ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableHeadersRow();

                uint64_t window_ms = 0;
                if (!d.history.empty())
                    window_ms = d.history.back().wall_ms - d.history_start_wall;
                uint64_t denom = window_ms < 1000 ? 1000 : window_ms;

                for (const auto& sk : d.skills) {
                    ImGui::TableNextRow();
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
                    uint64_t sk_dps = sk.damage * 1000ull / denom;
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
    }

    void draw_support_window(const char* title, bool* open,
                             const std::vector<Snapshot>& rows,
                             uint32_t Snapshot::*field) {
        if (!*open) return;
        ImGui::SetNextWindowSize(ImVec2(220, 180), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, open)) {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
            if (ImGui::BeginTable("tbl", 3,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("Prof",  ImGuiTableColumnFlags_WidthFixed, 22.0f);
                ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableHeadersRow();

                std::vector<Snapshot> sorted = rows;
                std::sort(sorted.begin(), sorted.end(),
                    [field](const Snapshot& a, const Snapshot& b) {
                        return a.*field > b.*field;
                    });

                for (const auto& r : sorted) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (uint64_t tex = icon_for(r.prof, r.elite); tex != 0) {
                        ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2(14, 14));
                    } else {
                        ImGui::TextUnformatted(prof_short(r.prof));
                    }
                    ImGui::TableNextColumn();
                    ImU32 col = prof_color(r.prof);
                    if (!r.in_combat) col = (col & 0x00FFFFFF) | (0x80u << 24);
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

    void draw_options_controls() {
        auto& s = settings();
        bool ex_npcs    = options().exclude_npcs.load(std::memory_order_relaxed);
        bool ex_gadgets = options().exclude_gadgets.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Exclude NPCs", &ex_npcs)) {
            options().exclude_npcs.store(ex_npcs, std::memory_order_relaxed);
            s.exclude_npcs = ex_npcs;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Exclude Gadgets", &ex_gadgets)) {
            options().exclude_gadgets.store(ex_gadgets, std::memory_order_relaxed);
            s.exclude_gadgets = ex_gadgets;
        }

        const char* sort_items[] = {"Damage", "DPS", "Name", "Combat time"};
        int sort_mode = s.sort_mode;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Sort", &sort_mode, sort_items, IM_ARRAYSIZE(sort_items))) {
            s.sort_mode = sort_mode;
        }

        ImGui::Separator();
        bool cl = s.cleanses_open;
        bool st = s.strips_open;
        if (ImGui::Checkbox("Cleanses window", &cl)) s.cleanses_open = cl;
        if (ImGui::Checkbox("Strips window",   &st)) s.strips_open   = st;
    }
}

void ui_init(ImGuiContext* ctx) {
    if (ctx) ImGui::SetCurrentContext(ctx);
}

uintptr_t mod_imgui(uint32_t not_charsel_or_loading) {
    if (!not_charsel_or_loading) return 0;
    auto& s = settings();
    if (!s.window_open) return 0;

    if (!g_window_pos_applied) {
        if (s.window_x >= 0.0f && s.window_y >= 0.0f) {
            ImGui::SetNextWindowPos(ImVec2(s.window_x, s.window_y),
                                    ImGuiCond_FirstUseEver);
        }
        ImGui::SetNextWindowSize(ImVec2(s.window_w, s.window_h),
                                 ImGuiCond_FirstUseEver);
        g_window_pos_applied = true;
    }

    bool open = s.window_open;
    if (ImGui::Begin("Damage", &open)) {
        ImVec2 pos  = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        s.window_x = pos.x;
        s.window_y = pos.y;
        s.window_w = size.x;
        s.window_h = size.y;

        if (ImGui::BeginPopupContextWindow("idps_ctx",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            draw_options_controls();
            ImGui::EndPopup();
        }

        auto rows = tracker().snapshot();
        sort_rows(rows, s.sort_mode);

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        if (ImGui::BeginTable("idps", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Prof",   ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("DPS",    ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Damage", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("Combat", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableHeadersRow();

            for (const auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (uint64_t tex = icon_for(r.prof, r.elite); tex != 0) {
                    ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2(14, 14));
                } else {
                    ImGui::TextUnformatted(prof_short(r.prof));
                }
                ImGui::TableNextColumn();
                {
                    ImU32 col = prof_color(r.prof);
                    if (!r.in_combat) {
                        // Dim OOC rows by halving alpha.
                        col = (col & 0x00FFFFFF) | (0x80u << 24);
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, col);
                    ImGui::PushID(static_cast<ImGuiID>(r.id));
                    if (ImGui::Selectable(r.name.c_str(),
                                          g_selected_agent == r.id && g_detail_open,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowItemOverlap)) {
                        g_selected_agent = r.id;
                        g_detail_open = true;
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
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(2);
    }
    ImGui::End();
    s.window_open = open;

    if (s.cleanses_open || s.strips_open) {
        auto rows = tracker().snapshot();
        draw_support_window("Cleanses", &s.cleanses_open, rows, &Snapshot::cleanse_count);
        draw_support_window("Strips",   &s.strips_open,   rows, &Snapshot::strip_count);
    }
    draw_detail_window();
    return 0;
}

uintptr_t mod_wnd_nofilter(void* /*hwnd*/, uint32_t umsg,
                           uintptr_t /*wparam*/, intptr_t /*lparam*/) {
    return umsg;
}

uintptr_t mod_options_end() {
    if (ImGui::CollapsingHeader("Individual DPS")) {
        auto& s = settings();
        bool open = s.window_open;
        if (ImGui::Checkbox("Show window", &open)) s.window_open = open;
        draw_options_controls();
        ImGui::TextDisabled("v0.3.0");
    }
    return 0;
}

} // namespace idps
