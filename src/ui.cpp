#include "ui.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

#include "exports.h"
#include "icons.h"
#include "settings.h"
#include "tracker.h"

namespace idps {

namespace {
    uintptr_t g_selected_agent = 0;

    // EMA smoothing for the live DPS column so it doesn't tick like a
    // stopwatch every frame. Cadence ~500ms; weight 3:1 (old:new).
    struct DpsCache {
        uint64_t last_wall = 0;
        uint64_t shown     = 0;
    };
    std::unordered_map<uintptr_t, DpsCache> g_dps_cache;

    // Reusable per-frame buffers — sit in static storage so we don't realloc
    // a new vector + per-agent strings every render.
    std::vector<Snapshot>  g_rows;
    std::vector<size_t>    g_sort_idx;
    AgentDetail            g_detail;

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

    // Drop alpha to ~67% (instead of 50%) for out-of-combat rows. Halving
    // alpha was washing names out against the translucent window background;
    // 170/255 keeps the in/out-of-combat distinction visible without making
    // names unreadable. Preserves RGB so prof color identity stays intact.
    ImU32 dim_alpha(ImU32 col) {
        uint32_t a = (col >> 24) & 0xFFu;
        a = (a * 170u) / 255u;
        return (col & 0x00FFFFFFu) | (a << 24);
    }

    ImU32 prof_color(uint32_t prof) {
        // Saturation + luminance lifted across the board so prof identity
        // pops on the translucent window background. Hues kept matching the
        // canonical GW2 prof palette so users still parse them at a glance.
        // Necro stays a darker green vs Ranger's lighter green so the two
        // don't blur into each other in mixed squads.
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

    // Drop EMA cache entries for agents that have left the snapshot. Keeps
    // the cache from accumulating stale ids across long sessions (WvW, etc).
    // Apply window position with optional viewport-relative mode. When
    // pos_relative is on and rx/ry are valid, position is computed as a
    // fraction of the display area so the window keeps its on-screen
    // location across resolution changes / monitor swaps. ImGui v1.80
    // (non-docking branch) doesn't expose GetMainViewport, so we use
    // io.DisplaySize which arc fills with the swapchain back-buffer size.
    void apply_window_pos(float abs_x, float abs_y, float rx, float ry, bool relative) {
        if (relative && rx >= 0.0f && ry >= 0.0f) {
            const ImVec2& ds = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(ds.x * rx, ds.y * ry),
                                    ImGuiCond_FirstUseEver);
        } else if (abs_x >= 0.0f && abs_y >= 0.0f) {
            ImGui::SetNextWindowPos(ImVec2(abs_x, abs_y), ImGuiCond_FirstUseEver);
        }
    }

    // Capture window pos as both absolute pixels and display-relative
    // fractions every frame, so toggling pos_relative later has valid
    // fractions to apply on next session start.
    void capture_window_pos(const ImVec2& pos, float& abs_x, float& abs_y,
                            float& rx, float& ry) {
        abs_x = pos.x;
        abs_y = pos.y;
        const ImVec2& ds = ImGui::GetIO().DisplaySize;
        if (ds.x > 0.0f) rx = pos.x / ds.x;
        if (ds.y > 0.0f) ry = pos.y / ds.y;
    }

    // Vertically center a 14x14 prof icon against the row's text baseline so
    // the icon doesn't sit higher than the name text in the same row.
    void align_icon_to_text() {
        float dy = (ImGui::GetTextLineHeight() - 14.0f) * 0.5f;
        if (dy > 0.0f) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dy);
        }
    }

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

        apply_window_pos(s.detail_x, s.detail_y, s.detail_rx, s.detail_ry,
                         s.pos_relative);
        ImGui::SetNextWindowSize(ImVec2(s.detail_w, s.detail_h),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(s.window_alpha);

        bool open = s.detail_open;
        if (ImGui::Begin(title, &open)) {
            // ESC closes the detail window when it has keyboard focus.
            if (ImGui::IsWindowFocused() &&
                ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
                open = false;
            }
            ImVec2 dpos = ImGui::GetWindowPos();
            ImVec2 dsiz = ImGui::GetWindowSize();
            capture_window_pos(dpos, s.detail_x, s.detail_y,
                               s.detail_rx, s.detail_ry);
            s.detail_w = dsiz.x;
            s.detail_h = dsiz.y;
            // Damage over time graph.
            // DPS-over-time: first difference of the cumulative damage
            // samples divided by the elapsed wall-ms between them.
            // Per-interval DPS, manual plot so the hover tooltip can format
            // values with a "k" suffix instead of ImGui's built-in "%8.4g".
            // Compute per-sample DPS over a 1-second lookback window. The
            // raw history is sampled every 500ms, so adjacent-pair diffs
            // double-count short bursts and inflate peaks vs arc's panel.
            // 1s window matches arc's peak more closely.
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
            ImGui::Text("DPS over time  peak: %.1fk",  peak_dps / 1000.0f);

            const float axis_w  = 50.0f;
            const float graph_h = 120.0f;
            float total_w = ImGui::GetContentRegionAvail().x;
            float graph_w = total_w - axis_w;
            if (!samples.empty() && peak_dps > 0.0f && graph_w > 20.0f) {
                ImVec2 origin   = ImGui::GetCursorScreenPos();
                ImVec2 graph_p0 = ImVec2(origin.x + axis_w, origin.y);
                ImVec2 graph_p1 = ImVec2(graph_p0.x + graph_w, graph_p0.y + graph_h);
                auto* dl = ImGui::GetWindowDrawList();

                // Hit-test region first so visuals render on top.
                ImGui::InvisibleButton("##graph", ImVec2(total_w, graph_h));
                bool hovered = ImGui::IsItemHovered();

                dl->AddRectFilled(graph_p0, graph_p1, IM_COL32(20, 20, 20, 180));
                dl->AddRect      (graph_p0, graph_p1, IM_COL32(80, 80, 80, 255));

                // Y-axis grid + labels at 0%, 25%, 50%, 75%, 100%.
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

                // DPS line.
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

                    // Vertical crosshair + dot on the line at the hovered sample.
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
            if (ImGui::BeginTable("skills", 5, sk_flags)) {
                ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 22.0f);
                ImGui::TableSetupColumn("Skill",  ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Damage", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("DPS",    ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableSetupColumn("Hits",   ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableHeadersRow();

                // Skills already sorted by damage descending in tracker::detail().
                int rank = 0;
                for (const auto& sk : d.skills) {
                    ++rank;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", rank);
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
                    // DPS over this skill's own active window (first hit -> last hit),
                    // not over total fight time. Matches arc's "/as" per-active-second.
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

    // Tooltip helper that doesn't depend on the newer SetItemTooltip API.
    void item_tooltip(const char* text) {
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void draw_options_controls() {
        auto& s = settings();
        bool ex_npcs    = options().exclude_npcs.load(std::memory_order_relaxed);
        bool ex_gadgets = options().exclude_gadgets.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Exclude NPCs", &ex_npcs)) {
            options().exclude_npcs.store(ex_npcs, std::memory_order_relaxed);
            s.exclude_npcs = ex_npcs;
        }
        item_tooltip("Drops damage / strips / cleanses against world NPCs "
                     "(open-world enemies, structure NPCs, most training "
                     "golems in the SAB / Aerodrome lobby).");
        ImGui::SameLine();
        if (ImGui::Checkbox("Exclude Gadgets", &ex_gadgets)) {
            options().exclude_gadgets.store(ex_gadgets, std::memory_order_relaxed);
            s.exclude_gadgets = ex_gadgets;
        }
        item_tooltip("Drops damage against gadget-class targets. Some "
                     "training golems (Special Forces Training Area) are "
                     "classified as gadgets, not NPCs — uncheck this if your "
                     "test target's damage isn't registering.");

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

        bool hs = s.highlight_self;
        if (ImGui::Checkbox("Highlight self row", &hs)) s.highlight_self = hs;

        bool nw = s.name_white;
        if (ImGui::Checkbox("White names", &nw)) s.name_white = nw;

        bool gold = s.self_name_gold;
        if (ImGui::Checkbox("Gold self name", &gold)) s.self_name_gold = gold;
        item_tooltip("Render your own player name in gold so it stands out "
                     "against squadmates, regardless of profession color.");

        bool rc = s.responsive_columns;
        if (ImGui::Checkbox("Responsive columns", &rc)) s.responsive_columns = rc;

        bool bb = s.body_borders;
        if (ImGui::Checkbox("Column dividers in body", &bb)) s.body_borders = bb;
        item_tooltip("Show thin vertical lines between columns in the table "
                     "body. Header dividers stay visible either way so "
                     "columns remain resizable.");

        bool fb = s.bar_full_row;
        if (ImGui::Checkbox("Full-row damage bar", &fb)) s.bar_full_row = fb;
        item_tooltip("When on, the per-player damage bar fills the entire "
                     "row width. Off restricts it to the Name column.");

        bool pr = s.pos_relative;
        if (ImGui::Checkbox("Screen-relative position", &pr)) s.pos_relative = pr;
        item_tooltip("Store window position as a fraction of the game "
                     "viewport instead of absolute pixels, so the window "
                     "stays in roughly the same on-screen spot after "
                     "resolution changes or monitor swaps.");

        float alpha = s.window_alpha;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("Window opacity", &alpha, 0.10f, 1.00f, "%.2f")) {
            if (alpha < 0.10f) alpha = 0.10f;
            if (alpha > 1.00f) alpha = 1.00f;
            s.window_alpha = alpha;
        }
    }
}

void ui_init(ImGuiContext* ctx) {
    if (ctx) ImGui::SetCurrentContext(ctx);
}

uintptr_t mod_imgui(uint32_t not_charsel_or_loading) {
    if (!not_charsel_or_loading) return 0;
    icons_ensure_loaded();
    auto& s = settings();
    if (!s.window_open) return 0;

    apply_window_pos(s.window_x, s.window_y, s.window_rx, s.window_ry,
                     s.pos_relative);
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
                    if (tbl->ColumnsCount >= 6) {
                        tbl->Columns[2].IsEnabledNextFrame = show_dps;
                        tbl->Columns[3].IsEnabledNextFrame = show_dmg;
                        tbl->Columns[4].IsEnabledNextFrame = show_combat;
                        tbl->Columns[5].IsEnabledNextFrame = show_pct;
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
                        ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bar_col);
                    }
                }

                if (uint64_t tex = icon_for(r.prof, r.elite); tex != 0) {
                    align_icon_to_text();
                    ImGui::Image(reinterpret_cast<ImTextureID>(tex), ImVec2(14, 14));
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
                                          g_selected_agent == r.id && s.detail_open,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowItemOverlap)) {
                        g_selected_agent = r.id;
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
                    ImGui::Text("%.1f%%", pct);
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
    draw_detail_window();

    prune_dps_cache(g_rows);
    return 0;
}


uintptr_t mod_options_end() {
    if (ImGui::CollapsingHeader("Individual DPS")) {
        auto& s = settings();
        bool open = s.window_open;
        if (ImGui::Checkbox("Show window", &open)) s.window_open = open;
        draw_options_controls();
        ImGui::Text("v%s", version());
    }
    return 0;
}

} // namespace idps
