#include "ui.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <shellapi.h>

#include "exports.h"
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
    // 1 = most recent past fight, 2 = older, etc. Anchored to the
    // selected FightSnapshot by start_wall, not by relative index, so a
    // new fight closing mid-view doesn't silently shift the user onto a
    // different fight. If the anchored fight falls off the FIFO, snap
    // back to live.
    int hist_n = tracker().history_size();
    static int      viewed_fight      = 0;
    static uint64_t viewed_start_wall = 0;
    if (viewed_fight > 0 && viewed_start_wall != 0) {
        int relocated = 0;
        for (int back = 1; back <= hist_n; ++back) {
            FightSummary fsum;
            if (tracker().fight_summary_at(hist_n - back, fsum) &&
                fsum.start_wall == viewed_start_wall) {
                relocated = back;
                break;
            }
        }
        viewed_fight = relocated;
        if (viewed_fight == 0) viewed_start_wall = 0;
    }
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
    uint64_t total_dps    = 0;
    for (const auto& r : g_rows) {
        total_damage += r.damage_total;
        total_dps    += r.dps;
    }

    bool open = s.window_open;
    ImGuiWindowFlags lock_flags = s.lock_windows
        ? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)
        : 0;
    if (ImGui::Begin("Damage", &open, lock_flags)) {
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
            ImGui::SameLine();
            // Plain-text snapshot of the visible table (current sort order)
            // for pasting into squad/guild chat or Discord.
            if (ImGui::Button("Copy summary")) {
                uint64_t max_ms = 0;
                for (const auto& r : g_rows) {
                    if (r.combat_ms > max_ms) max_ms = r.combat_ms;
                }
                char tbuf[32], line[160];
                format_time(tbuf, sizeof(tbuf), max_ms);
                std::string out;
                out.reserve(64 + g_rows.size() * 48);
                char dmgbuf[16], dpsbuf[16];
                format_count(dmgbuf, sizeof(dmgbuf), total_damage);
                std::snprintf(line, sizeof(line),
                              "Damage summary (%s, %s total)\n", tbuf, dmgbuf);
                out += line;
                int rank = 0;
                for (const auto& r : g_rows) {
                    ++rank;
                    format_count(dmgbuf, sizeof(dmgbuf), r.damage_total);
                    format_count(dpsbuf, sizeof(dpsbuf), r.dps);
                    double pct = total_damage > 0
                               ? 100.0 * static_cast<double>(r.damage_total) /
                                 static_cast<double>(total_damage)
                               : 0.0;
                    std::snprintf(line, sizeof(line),
                                  "%2d. %-22s %8s  %7s dps  %3.0f%%\n",
                                  rank, r.name.c_str(), dmgbuf, dpsbuf, pct);
                    out += line;
                }
                ImGui::SetClipboardText(out.c_str());
                ImGui::CloseCurrentPopup();
            }
            // Fight history is reachable here too — the per-row menu
            // needs a row to right-click, which an empty table doesn't
            // have. Newest first (back = 1 is the most recent).
            ImGui::Separator();
            ImGui::TextDisabled("Fight history");
            if (viewed_fight != 0) {
                if (ImGui::MenuItem("Current (live)")) {
                    viewed_fight      = 0;
                    viewed_start_wall = 0;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (hist_n == 0) {
                ImGui::TextDisabled("(no past fights yet)");
            }
            for (int back = 1; back <= hist_n; ++back) {
                FightSummary fsum;
                if (!tracker().fight_summary_at(hist_n - back, fsum))
                    continue;
                uint64_t dur = fsum.end_wall > fsum.start_wall
                             ? fsum.end_wall - fsum.start_wall : 0;
                char dbuf[32], cbuf[16], dmgbuf[16], label[128];
                format_time (dbuf, sizeof(dbuf), dur);
                format_clock(cbuf, sizeof(cbuf), fsum.end_clock);
                format_count(dmgbuf, sizeof(dmgbuf), fsum.total_damage);
                snprintf(label, sizeof(label),
                         "-%d  %s  (%s)   %s dmg   %d players",
                         back, cbuf[0] ? cbuf : "--:--", dbuf,
                         dmgbuf, fsum.players);
                bool selected = (viewed_fight == back);
                if (ImGui::MenuItem(label, nullptr, selected)) {
                    viewed_fight      = back;
                    viewed_start_wall = fsum.start_wall;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        // One-shot "Updated to vX" banner shown the first launch after
        // arc auto-applies a newer release. Dismissed by the user; the
        // ini baseline is already persisted at detection time so it does
        // not re-fire for the same upgrade.
        if (update_banner_visible()) {
            const char* prev = update_banner_prev_version();
            if (prev && *prev) {
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                                   "Updated to v%s (was v%s)",
                                   version(), prev);
            } else {
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                                   "Updated to v%s", version());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Notes##update_notes")) {
                char url[256];
                std::snprintf(url, sizeof(url),
                              "https://github.com/iTroy0/arcdps_individual_dps/releases/tag/v%s",
                              version());
                ShellExecuteA(nullptr, "open", url,
                              nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x##update_dismiss")) {
                update_banner_dismiss();
            }
            ImGui::Separator();
        }

        // Past-fight state line. Only renders when viewing history so live
        // view stays uncluttered. Nav happens via right-click on a row -> Fight
        // history menu; no arrows.
        if (viewing_history) {
            FightSummary fsum;
            int storage_idx = hist_n - viewed_fight;
            tracker().fight_summary_at(storage_idx, fsum);
            uint64_t dur_ms = fsum.end_wall > fsum.start_wall
                            ? fsum.end_wall - fsum.start_wall : 0;
            char dbuf[32], cbuf[16], agobuf[24];
            format_time (dbuf,   sizeof(dbuf),   dur_ms);
            format_clock(cbuf,   sizeof(cbuf),   fsum.end_clock);
            format_ago  (agobuf, sizeof(agobuf), fsum.end_clock);
            if (cbuf[0]) {
                ImGui::Text("Viewing Fight -%d  %s (%s)  ended %s",
                            viewed_fight, cbuf, dbuf, agobuf);
            } else {
                ImGui::Text("Viewing Fight -%d  (%s)", viewed_fight, dbuf);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Live")) {
                viewed_fight      = 0;
                viewed_start_wall = 0;
            }
            ImGui::Separator();
        }

        // Squad totals strip. Reflects whichever fight is on screen (live
        // or past) because it sums the same rows the table renders.
        if (s.show_totals && !g_rows.empty()) {
            char dmgbuf[16], dpsbuf[16];
            format_count(dmgbuf, sizeof(dmgbuf), total_damage);
            format_count(dpsbuf, sizeof(dpsbuf), total_dps);
            ImGui::TextColored(ImVec4(0.72f, 0.78f, 0.85f, 1.0f),
                               "Squad  %s dmg   %s dps   %d players",
                               dmgbuf, dpsbuf,
                               static_cast<int>(g_rows.size()));
            ImGui::Separator();
        }

        // Capture available width before BeginTable so responsive-column
        // logic below can drop low-priority columns when the window shrinks.
        float table_avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        ImGuiTableFlags table_flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY;
        if (!s.body_borders) table_flags |= ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("idps", 6, table_flags)) {
            // Seed the header sort arrow from the persisted sort_mode so the
            // indicator matches the actual row order on launch (DefaultSort
            // only applies when ImGui has no saved table state — harmless
            // otherwise).
            auto def_sort = [&](int mode) {
                return s.sort_mode == mode
                     ? ImGuiTableColumnFlags_DefaultSort
                     : ImGuiTableColumnFlags_None;
            };
            ImGui::TableSetupColumn("Prof",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_NoSort, 22.0f);
            ImGui::TableSetupColumn("Name",
                                    ImGuiTableColumnFlags_WidthStretch |
                                    def_sort(2));
            // Numeric columns prefer descending on first click (high-to-low
            // is the natural read for DPS / damage / combat / share).
            ImGui::TableSetupColumn("DPS",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                    def_sort(1),
                                    56.0f);
            ImGui::TableSetupColumn("Damage",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                    def_sort(0),
                                    64.0f);
            ImGui::TableSetupColumn("Time",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_PreferSortDescending |
                                    def_sort(3),
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
                // The first dirty pass is table setup (DefaultSort / restored
                // ImGui state), not a user click. Consuming it without
                // applying keeps the ini-persisted sort_mode + sort_reverse
                // authoritative across sessions.
                static bool sort_seeded = false;
                if (specs->SpecsDirty && !sort_seeded) {
                    sort_seeded = true;
                    specs->SpecsDirty = false;
                }
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
                        ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x80u << 24);
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
                            ImU32 bar_col = (prof_col & 0x00FFFFFFu) | (0x80u << 24);
                            ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bar_col);
                        }
                    }

                    ImGui::PushStyleColor(ImGuiCol_Text, text_col);
                    // Pointer overload hashes the full 64-bit agent id so
                    // two agents whose low 32 bits collide can't alias
                    // popup / selectable state.
                    ImGui::PushID(reinterpret_cast<const void*>(r.id));
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
                    // stays trivial while hovering. Cached for 250 ms so a
                    // resting cursor doesn't re-lock the combat-thread
                    // mutex every render frame.
                    if (ImGui::IsItemHovered()) {
                        struct TipCache {
                            uintptr_t agent   = 0;
                            int       fight   = -1;
                            uint64_t  last_ms = 0;
                            bool      ok      = false;
                            std::vector<SkillDetail> data;
                        };
                        static TipCache tip_cache;
                        // Skills only meaningful for rows with damage; account
                        // ID shows on every player row regardless.
                        if (r.damage_total > 0) {
                            uint64_t now_ms = GetTickCount64();
                            if (tip_cache.agent != r.id ||
                                tip_cache.fight != viewed_fight ||
                                now_ms - tip_cache.last_ms > 250) {
                                tip_cache.ok = viewing_history
                                    ? tracker().top_skills_at(hist_n - viewed_fight,
                                                              r.id, 3, tip_cache.data)
                                    : tracker().top_skills(r.id, 3, tip_cache.data);
                                tip_cache.agent   = r.id;
                                tip_cache.fight   = viewed_fight;
                                tip_cache.last_ms = now_ms;
                            }
                        } else {
                            tip_cache.ok = false;
                        }
                        bool has_skills = tip_cache.ok &&
                                          tip_cache.agent == r.id &&
                                          !tip_cache.data.empty();
                        if (!r.account.empty() || has_skills) {
                            anchor_cursor_tooltip();
                            ImGui::BeginTooltip();
                            if (!r.account.empty())
                                ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.50f, 1.0f),
                                                   "%s", r.account.c_str());
                            if (has_skills) {
                                if (!r.account.empty()) ImGui::Separator();
                                ImGui::Text("Top skills - %s", r.name.c_str());
                                ImGui::Separator();
                                for (const auto& sd : tip_cache.data) {
                                    char dmgbuf[16];
                                    format_count(dmgbuf, sizeof(dmgbuf), sd.damage);
                                    const char* nm = sd.name.empty() ? "(unknown)"
                                                                      : sd.name.c_str();
                                    ImGui::Text("%s : %s  (%u hits)",
                                                nm, dmgbuf, sd.hits);
                                }
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
                                viewed_fight      = 0;
                                viewed_start_wall = 0;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        int n_hist = tracker().history_size();
                        if (n_hist == 0) {
                            ImGui::TextDisabled("(no past fights yet)");
                        }
                        // back = 1 is the most recent fight — list is
                        // newest-first by construction.
                        for (int back = 1; back <= n_hist; ++back) {
                            int storage_idx = n_hist - back;
                            FightSummary fsum;
                            if (!tracker().fight_summary_at(storage_idx, fsum))
                                continue;
                            uint64_t dur = fsum.end_wall > fsum.start_wall
                                         ? fsum.end_wall - fsum.start_wall : 0;
                            char dbuf[32], cbuf[16];
                            format_time (dbuf, sizeof(dbuf), dur);
                            format_clock(cbuf, sizeof(cbuf), fsum.end_clock);
                            const char* clock = cbuf[0] ? cbuf : "--:--";
                            Snapshot ag{};
                            char label[160];
                            if (tracker().agent_snapshot_at(storage_idx, r.id, ag)) {
                                char dpsbuf[16], dmgbuf[16];
                                format_count(dpsbuf, sizeof(dpsbuf), ag.dps);
                                format_count(dmgbuf, sizeof(dmgbuf), ag.damage_total);
                                snprintf(label, sizeof(label),
                                         "-%d  %s  (%s)   %s DPS   %s dmg",
                                         back, clock, dbuf, dpsbuf, dmgbuf);
                            } else {
                                snprintf(label, sizeof(label),
                                         "-%d  %s  (%s)   (not present)",
                                         back, clock, dbuf);
                            }
                            bool selected = (viewed_fight == back);
                            if (ImGui::MenuItem(label, nullptr, selected)) {
                                viewed_fight      = back;
                                viewed_start_wall = fsum.start_wall;
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
                // Past fights are static — smoothing them would EMA-blend
                // stale values into the live cache and lag the displayed
                // number behind the stored result.
                format_count(buf, sizeof(buf),
                             viewing_history ? r.dps : smooth_dps(r.id, r.dps));
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

    // While viewing history g_rows holds past-fight agents; pruning against
    // them would evict every live agent's EMA state.
    if (!viewing_history) prune_dps_cache(g_rows);

    // Crash resilience: flush settings periodically (no-op when unchanged)
    // so a game crash doesn't revert the session's layout/options to the
    // last clean exit.
    {
        static uint64_t last_autosave = GetTickCount64();
        uint64_t now = GetTickCount64();
        if (now - last_autosave >= 15000) {
            last_autosave = now;
            settings_autosave();
        }
    }
    return 0;
}

} // namespace idps
