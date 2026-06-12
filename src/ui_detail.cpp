#include "ui_detail.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "settings.h"
#include "tracker.h"
#include "ui_common.h"

namespace idps {

namespace {
    // g_selected_agent is written by the render thread (set_selected_agent,
    // from mod_imgui) and read by the window thread (consume_esc_for_detail,
    // from mod_wnd_nofilter) — atomic so cross-thread access is not a race.
    std::atomic<uintptr_t> g_selected_agent{0};
    AgentDetail            g_detail;

    // 0 = nothing selected. Drives the spike overlay on the DPS graph.
    // Render-thread only.
    uint32_t    g_selected_skill = 0;

    // ESC handoff: the window thread cannot touch Settings directly (the
    // render thread owns it), so consume_esc_for_detail just raises a
    // request and draw_detail_window applies it on the next frame.
    // g_detail_visible is published by the render thread so the window
    // thread knows whether an ESC press should be consumed.
    std::atomic<bool> g_detail_visible{false};
    std::atomic<bool> g_esc_close_request{false};
}

void      set_selected_agent(uintptr_t id) {
    if (g_selected_agent.load(std::memory_order_relaxed) != id)
        g_selected_skill = 0;
    g_selected_agent.store(id, std::memory_order_relaxed);
}
uintptr_t selected_agent() {
    return g_selected_agent.load(std::memory_order_relaxed);
}

bool consume_esc_for_detail() {
    // Window/message thread. Reads only atomics; the actual Settings write
    // happens on the render thread in draw_detail_window.
    if (!g_detail_visible.load(std::memory_order_acquire)) return false;
    g_esc_close_request.store(true, std::memory_order_release);
    return true;
}

void draw_detail_window(int viewed_history_idx) {
    auto& s = settings();

    // Apply a pending ESC-close request from the window thread, then keep
    // g_detail_visible in sync with the real open state on every exit path
    // so the window thread's consume_esc_for_detail stays accurate.
    if (g_esc_close_request.exchange(false, std::memory_order_acq_rel)) {
        s.detail_open = false;
    }
    const uintptr_t sel = g_selected_agent.load(std::memory_order_relaxed);
    auto publish_visible = [&]() {
        g_detail_visible.store(s.detail_open && sel != 0,
                               std::memory_order_release);
    };

    if (!s.detail_open || sel == 0) { publish_visible(); return; }

    if (viewed_history_idx > 0) {
        int hist_idx = tracker().history_size() - viewed_history_idx;
        if (!tracker().detail_at(hist_idx, sel, g_detail)) {
            s.detail_open = false;
            publish_visible();
            return;
        }
    } else {
        tracker().detail(sel, g_detail);
    }
    const auto& d = g_detail;
    if (d.name.empty()) {
        s.detail_open = false;
        publish_visible();
        return;
    }

    char title[160];
    if (!d.account.empty())
        std::snprintf(title, sizeof(title), "%s (%s) - details###idps_detail",
                      d.name.c_str(), d.account.c_str());
    else
        std::snprintf(title, sizeof(title), "%s - details###idps_detail",
                      d.name.c_str());

    static bool   prev_rel = false;
    static ImVec2 prev_ds(0.0f, 0.0f);
    apply_window_pos(s.detail_x, s.detail_y, s.detail_rx, s.detail_ry,
                     s.pos_relative, prev_rel, prev_ds);
    ImGui::SetNextWindowSize(ImVec2(s.detail_w, s.detail_h),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(s.window_alpha);

    bool open = s.detail_open;
    // Detail window is intentionally exempt from lock_windows — the graph
    // benefits from being resized to fit different fight lengths, and the
    // user opens it on demand rather than as a persistent overlay.
    if (ImGui::Begin(title, &open)) {
        // ESC dismissal lives in mod_wnd_nofilter (exports.cpp) via
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
        // Static buffer reused across frames — avoids reallocating a
        // ~4096-element float vector every render of the detail window.
        static std::vector<float> samples;
        samples.clear();
        samples.reserve(d.history.size());
        float peak_dps = 0.0f;
        for (size_t i = 1; i < d.history.size(); ++i) {
            uint64_t target = d.history[i].wall_ms > 1000
                            ? d.history[i].wall_ms - 1000 : 0;
            size_t j = i;
            while (j > 0 && d.history[j - 1].wall_ms >= target) --j;
            // When the previous sample falls outside the 1s window (sparse
            // events, common on condi fights between tick clusters), back
            // off one step so dd reflects the damage rate across the gap
            // instead of dropping the line to zero.
            if (j == i && i > 0) j = i - 1;
            uint64_t dd = d.history[i].damage_total - d.history[j].damage_total;
            uint64_t dt = d.history[i].wall_ms      - d.history[j].wall_ms;
            if (dt < 1000) dt = 1000;  // floor to 1s window
            float dps = static_cast<float>(dd) * 1000.0f / static_cast<float>(dt);
            samples.push_back(dps);
            if (dps > peak_dps) peak_dps = dps;
        }

        // EMA-smoothed companion samples for the bell-shape visual line.
        // Raw `samples` still drives peak_dps so the label reflects real 1s
        // damage, not the lagged smoothed value.
        static std::vector<float> ema_samples;
        ema_samples.clear();
        ema_samples.reserve(samples.size());
        {
            constexpr float kEmaAlpha = 0.30f;
            float ema = 0.0f;
            for (size_t k = 0; k < samples.size(); ++k) {
                ema = (k == 0) ? samples[0]
                               : kEmaAlpha * samples[k]
                                 + (1.0f - kEmaAlpha) * ema;
                ema_samples.push_back(ema);
            }
        }

        // Average DPS across the rendered history span — horizontal ref.
        uint64_t total_dmg = 0;
        uint64_t total_dt  = 0;
        if (d.history.size() >= 2) {
            total_dmg = d.history.back().damage_total
                      - d.history.front().damage_total;
            total_dt  = d.history.back().wall_ms
                      - d.history.front().wall_ms;
        }
        float avg_dps = total_dt > 0
                      ? static_cast<float>(total_dmg) * 1000.0f /
                        static_cast<float>(total_dt)
                      : 0.0f;

        // Burst-window threshold: top 25% of samples. nth_element is O(n)
        // — only the pivot matters, full sort is wasted work.
        float burst_threshold = 0.0f;
        if (samples.size() >= 4) {
            static std::vector<float> tmp;
            tmp.assign(samples.begin(), samples.end());
            size_t pivot = tmp.size() * 3 / 4;
            std::nth_element(tmp.begin(), tmp.begin() + pivot, tmp.end());
            burst_threshold = tmp[pivot];
        }
        const SkillDetail* sel_skill = nullptr;
        if (g_selected_skill != 0) {
            for (const auto& sk : d.skills) {
                if (sk.skill_id == g_selected_skill) { sel_skill = &sk; break; }
            }
        }
        ImGui::TextUnformatted("DPS over time");
        if (sel_skill) {
            const char* nm = !sel_skill->name.empty()
                           ? sel_skill->name.c_str() : "?";
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 90, 255));
            ImGui::Text("spike: %s (#%u)", nm, sel_skill->skill_id);
            ImGui::PopStyleColor();
        }

        // Compact color legend — clickable chips toggle each plotted
        // layer (peak chip is non-clickable; the raw 1s line is mandatory
        // since it drives the peak value). Disabled chips render dimmed.
        {
            auto fmt = [](char* b, size_t n, float v) {
                if (v >= 1000.0f) std::snprintf(b, n, "%.1fk", v / 1000.0f);
                else              std::snprintf(b, n, "%.0f", v);
            };
            char pbuf[16], abuf[16];
            fmt(pbuf, sizeof(pbuf), peak_dps);
            fmt(abuf, sizeof(abuf), avg_dps);
            // chip(col, toggle_or_null, "fmt", args...). null toggle = no
            // click handling, no dim state.
            int chip_idx = 0;
            auto chip = [&](ImU32 col, bool* toggle, const char* fmt_str,
                            auto&&... args) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), fmt_str, args...);
                bool enabled = toggle ? *toggle : true;

                float h   = ImGui::GetTextLineHeight();
                float sz  = h * 0.55f;
                float gap = 4.0f;
                ImVec2 ts = ImGui::CalcTextSize(buf);
                float chip_w = sz + gap + ts.x;

                ImVec2 p = ImGui::GetCursorScreenPos();
                bool hovered = false;
                if (toggle) {
                    // Stable numeric ID per chip slot — using the formatted
                    // text would collide when two chips compute the same
                    // string (e.g. peak == avg) and would also break
                    // hover/click state every frame as the value changes.
                    ImGui::PushID(chip_idx);
                    if (ImGui::InvisibleButton("##chip",
                                               ImVec2(chip_w, h))) {
                        *toggle = !*toggle;
                    }
                    hovered = ImGui::IsItemHovered();
                    ImGui::PopID();
                } else {
                    ImGui::Dummy(ImVec2(chip_w, h));
                }
                ++chip_idx;

                auto* dl = ImGui::GetWindowDrawList();
                if (hovered) {
                    dl->AddRectFilled({p.x - 2, p.y},
                                      {p.x + chip_w + 2, p.y + h},
                                      IM_COL32(255, 255, 255, 28), 2.0f);
                }
                ImU32 sw = enabled
                         ? col
                         : ((col & 0x00FFFFFFu) | (0x40u << 24));
                float y0 = p.y + (h - sz) * 0.5f;
                dl->AddRectFilled({p.x, y0}, {p.x + sz, y0 + sz}, sw);
                ImU32 tc = enabled
                         ? IM_COL32(220, 220, 220, 255)
                         : IM_COL32(120, 120, 120, 255);
                dl->AddText({p.x + sz + gap, p.y}, tc, buf);

                ImGui::SameLine(0, 12);
            };
            chip(IM_COL32(110, 180, 255, 255), nullptr,         "peak %s", pbuf);
            chip(IM_COL32(255, 180,  90, 255), &s.chart_smooth, "smooth");
            chip(IM_COL32( 90, 220, 140, 255), &s.chart_cum,    "cum");
            chip(IM_COL32(200, 200, 200, 255), &s.chart_avg,    "avg %s", abuf);
            chip(IM_COL32(255, 200,  90, 255), &s.chart_burst,  "burst");
            ImGui::NewLine();
            // Breathing room so the legend's "peak %s" chip doesn't crowd
            // the Y-axis peak label sitting right below at graph top.
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        }

        const float axis_w   = 50.0f;
        const float graph_h  = 120.0f;
        const float xaxis_h  = 14.0f; // reserved strip below graph for time labels
        float total_w = ImGui::GetContentRegionAvail().x;
        float graph_w = total_w - axis_w;
        if (!samples.empty() && peak_dps > 0.0f && graph_w > 20.0f) {
            ImVec2 origin   = ImGui::GetCursorScreenPos();
            ImVec2 graph_p0 = ImVec2(origin.x + axis_w, origin.y);
            ImVec2 graph_p1 = ImVec2(graph_p0.x + graph_w, graph_p0.y + graph_h);
            auto* dl = ImGui::GetWindowDrawList();

            // InvisibleButton first so visuals render on top of the hit rect.
            // Extra xaxis_h reserves space below the plot for time labels.
            ImGui::InvisibleButton("##graph", ImVec2(total_w, graph_h + xaxis_h));
            bool hovered = ImGui::IsItemHovered();

            dl->AddRectFilled(graph_p0, graph_p1, IM_COL32(20, 20, 20, 180));
            dl->AddRect      (graph_p0, graph_p1, IM_COL32(80, 80, 80, 255));

            const int n = static_cast<int>(samples.size());

            // Burst-window shading: faint tint over contiguous spans of
            // samples in the top-25% DPS band. Drawn before grids/lines so
            // it sits in the background.
            if (s.chart_burst && burst_threshold > 0.0f && n > 1) {
                const ImU32 burst_col = IM_COL32(255, 200, 90, 26);
                int span_start = -1;
                auto flush = [&](int span_end) {
                    float t0 = static_cast<float>(span_start) /
                               static_cast<float>(n - 1);
                    float t1 = static_cast<float>(span_end) /
                               static_cast<float>(n - 1);
                    dl->AddRectFilled(
                        {graph_p0.x + t0 * graph_w, graph_p0.y},
                        {graph_p0.x + t1 * graph_w, graph_p1.y},
                        burst_col);
                    span_start = -1;
                };
                for (int i = 0; i < n; ++i) {
                    bool above = samples[i] >= burst_threshold;
                    if (above && span_start < 0) span_start = i;
                    else if (!above && span_start >= 0) flush(i - 1);
                }
                if (span_start >= 0) flush(n - 1);
            }

            // Vertical grid + bottom time labels. Step snaps to a "nice"
            // value (1s/2s/5s/10s/30s/1m/5m) targeting ~8 ticks across the
            // fight duration so long pulls don't crowd labels.
            if (d.history.size() >= 2) {
                uint64_t t_start = d.history.front().wall_ms;
                uint64_t t_end   = d.history.back().wall_ms;
                uint64_t span_ms = t_end > t_start ? t_end - t_start : 0;
                if (span_ms > 0) {
                    const ImU32 vgrid_col = IM_COL32(60, 60, 60, 180);
                    const ImU32 xlbl_col  = IM_COL32(170, 170, 170, 255);
                    constexpr uint64_t snaps[] = {
                        1000, 2000, 5000, 10000, 30000, 60000, 300000
                    };
                    uint64_t target = span_ms / 8;
                    if (target < 1000) target = 1000;
                    uint64_t step = snaps[sizeof(snaps)/sizeof(snaps[0]) - 1];
                    for (uint64_t snap : snaps) {
                        if (snap >= target) { step = snap; break; }
                    }
                    for (uint64_t t_ms = step; t_ms < span_ms; t_ms += step) {
                        float t = static_cast<float>(t_ms) /
                                  static_cast<float>(span_ms);
                        float x = graph_p0.x + t * graph_w;
                        dl->AddLine({x, graph_p0.y}, {x, graph_p1.y},
                                    vgrid_col, 1.0f);
                        char tlbl[16];
                        if (t_ms >= 60000)
                            std::snprintf(tlbl, sizeof(tlbl), "%llum%llus",
                                static_cast<unsigned long long>(t_ms / 60000),
                                static_cast<unsigned long long>((t_ms % 60000) / 1000));
                        else
                            std::snprintf(tlbl, sizeof(tlbl), "%llus",
                                static_cast<unsigned long long>(t_ms / 1000));
                        ImVec2 ts = ImGui::CalcTextSize(tlbl);
                        dl->AddText({x - ts.x * 0.5f, graph_p1.y + 2.0f},
                                    xlbl_col, tlbl);
                    }
                }
            }

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

            // Average-DPS dashed horizontal line + right-anchored label.
            if (s.chart_avg && avg_dps > 0.0f && avg_dps < peak_dps) {
                float y = graph_p1.y - (avg_dps / peak_dps) * graph_h;
                const ImU32 avg_col = IM_COL32(200, 200, 200, 160);
                const float dash = 6.0f, gap = 4.0f;
                for (float x = graph_p0.x; x < graph_p1.x; x += dash + gap) {
                    float x2 = x + dash;
                    if (x2 > graph_p1.x) x2 = graph_p1.x;
                    dl->AddLine({x, y}, {x2, y}, avg_col, 1.0f);
                }
                char abuf[16];
                if (avg_dps >= 1000.0f)
                    std::snprintf(abuf, sizeof(abuf), "avg %.1fk", avg_dps / 1000.0f);
                else
                    std::snprintf(abuf, sizeof(abuf), "avg %.0f", avg_dps);
                ImVec2 ats = ImGui::CalcTextSize(abuf);
                dl->AddText({graph_p1.x - ats.x - 4.0f, y - ats.y - 2.0f},
                            avg_col, abuf);
            }

            // Area fill under the raw DPS line — flat alpha tint, two
            // triangles per segment.
            if (n > 1) {
                const ImU32 fill_col = IM_COL32(110, 180, 255, 40);
                for (int i = 0; i < n - 1; ++i) {
                    float t0 = static_cast<float>(i)     / static_cast<float>(n - 1);
                    float t1 = static_cast<float>(i + 1) / static_cast<float>(n - 1);
                    float x0 = graph_p0.x + t0 * graph_w;
                    float x1 = graph_p0.x + t1 * graph_w;
                    float y0 = graph_p1.y - (samples[i]     / peak_dps) * graph_h;
                    float y1 = graph_p1.y - (samples[i + 1] / peak_dps) * graph_h;
                    dl->AddTriangleFilled({x0, y0}, {x1, y1}, {x1, graph_p1.y}, fill_col);
                    dl->AddTriangleFilled({x0, y0}, {x1, graph_p1.y}, {x0, graph_p1.y}, fill_col);
                }
            }

            // Cumulative damage curve — back layer, green. Scaled to its
            // own peak (total damage in the rendered span), independent
            // of the DPS Y-axis. Plotted against wall-time so the curve
            // ramps from 0 at history[0] up to total_dmg at history.back(),
            // covering the initial rise that an index-based mapping would
            // skip when sample 0 starts mid-fight.
            if (s.chart_cum && d.history.size() >= 2 && total_dmg > 0) {
                const ImU32 cum_col = IM_COL32(90, 220, 140, 160);
                uint64_t base    = d.history.front().damage_total;
                uint64_t t_start = d.history.front().wall_ms;
                uint64_t span_ms = d.history.back().wall_ms > t_start
                                 ? d.history.back().wall_ms - t_start : 0;
                if (span_ms > 0) {
                    size_t hn = d.history.size();
                    for (size_t i = 1; i < hn; ++i) {
                        float t0 = static_cast<float>(d.history[i - 1].wall_ms - t_start) /
                                   static_cast<float>(span_ms);
                        float t1 = static_cast<float>(d.history[i    ].wall_ms - t_start) /
                                   static_cast<float>(span_ms);
                        float v0 = static_cast<float>(d.history[i - 1].damage_total - base) /
                                   static_cast<float>(total_dmg);
                        float v1 = static_cast<float>(d.history[i    ].damage_total - base) /
                                   static_cast<float>(total_dmg);
                        float y0 = graph_p1.y - v0 * graph_h;
                        float y1 = graph_p1.y - v1 * graph_h;
                        dl->AddLine({graph_p0.x + t0 * graph_w, y0},
                                    {graph_p0.x + t1 * graph_w, y1},
                                    cum_col, 1.0f);
                    }
                }
            }

            // EMA smoothed companion line — mid layer, orange. Provides
            // arc-like bell shape while raw line below still shows bursts.
            if (s.chart_smooth && n > 1) {
                const ImU32 ema_col = IM_COL32(255, 180, 90, 200);
                for (int i = 0; i < n - 1; ++i) {
                    float t0 = static_cast<float>(i)     / static_cast<float>(n - 1);
                    float t1 = static_cast<float>(i + 1) / static_cast<float>(n - 1);
                    float y0 = graph_p1.y - (ema_samples[i]     / peak_dps) * graph_h;
                    float y1 = graph_p1.y - (ema_samples[i + 1] / peak_dps) * graph_h;
                    dl->AddLine({graph_p0.x + t0 * graph_w, y0},
                                {graph_p0.x + t1 * graph_w, y1},
                                ema_col, 1.5f);
                }
            }

            // Raw 1s DPS line — front layer, blue. Drives the peak label.
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

                anchor_cursor_tooltip();
                ImGui::BeginTooltip();
                ImGui::Text("t = %s", tbuf);
                if (v >= 1000.0f) ImGui::Text("DPS:    %.1fk", v / 1000.0f);
                else              ImGui::Text("DPS:    %.0f",  v);
                ImGui::Text("Total:  %s", cbuf);
                ImGui::EndTooltip();
            }
        } else {
            ImGui::Dummy(ImVec2(total_w, graph_h));
            // Distinguish "no data" from "window too narrow to plot" so the
            // user knows whether to widen the detail window or wait for
            // damage events.
            const char* reason =
                (samples.empty() || peak_dps <= 0.0f)
                    ? "(not enough data yet)"
                    : "(window too narrow)";
            ImGui::TextDisabled("%s", reason);
        }

        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGuiTableFlags sk_flags =
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

        if (ImGui::BeginTable("skills", 7, sk_flags)) {
            ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 22.0f);
            ImGui::TableSetupColumn("Skill",  ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Damage", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("%",      ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("DPS",    ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Hits",   ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Crit",   ImGuiTableColumnFlags_WidthFixed, 40.0f);
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
                // Per-hit stats tooltip. All values are already in the
                // local SkillDetail copy — no tracker lock needed.
                if (ImGui::IsItemHovered() && sk.hits > 0) {
                    char minb[16], maxb[16], avgb[16];
                    format_count(minb, sizeof(minb), sk.min_hit);
                    format_count(maxb, sizeof(maxb), sk.max_hit);
                    format_count(avgb, sizeof(avgb), sk.damage / sk.hits);
                    anchor_cursor_tooltip();
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(!sk.name.empty() ? sk.name.c_str()
                                                            : "(unknown)");
                    ImGui::Separator();
                    ImGui::Text("hit avg %s   min %s   max %s",
                                avgb, minb, maxb);
                    if (sk.strike_hits > 0) {
                        ImGui::Text("crit %u / %u (%.0f%%)",
                                    sk.crits, sk.strike_hits,
                                    100.0f * static_cast<float>(sk.crits) /
                                    static_cast<float>(sk.strike_hits));
                    } else {
                        ImGui::TextDisabled("condition damage (no crits)");
                    }
                    ImGui::TextDisabled("click row to toggle spike overlay");
                    ImGui::EndTooltip();
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
                ImGui::TableNextColumn();
                // Crit% over strike hits only — condi ticks can't crit, so
                // pure condition skills read "-" instead of a fake 0%.
                if (sk.strike_hits > 0) {
                    ImGui::Text("%.0f%%",
                                100.0f * static_cast<float>(sk.crits) /
                                static_cast<float>(sk.strike_hits));
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    s.detail_open = open;
    publish_visible();
}

} // namespace idps
