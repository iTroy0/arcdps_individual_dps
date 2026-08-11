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

// hide_if_combat_or_ooc reports arcdps's combat / out-of-combat auto-hide
// state, and arc's global hide toggle is readable via its e6 export. Neither
// is acted on: this plugin's windows are governed solely by its own toggles.
// Following arc would hide the meter exactly when it is most useful — arc's
// out-of-combat auto-hide fires right as you sit down to read the fight you
// just finished.
uintptr_t mod_imgui(uint32_t not_charsel_or_loading,
                    uint32_t /*hide_if_combat_or_ooc*/) {
    if (!not_charsel_or_loading) return 0;
    icons_ensure_loaded();
    auto& s = settings();

    // Close a fight that has gone quiet, so WvW builds history at all rather
    // than accumulating the whole session into one entry. Deliberately ahead
    // of the window_open check: history has to keep advancing while the
    // overlay is hidden, or reopening it would show one entry spanning
    // everything since it was closed. Throttled — the check takes the combat
    // mutex and nothing it looks at moves faster than the fight gap.
    {
        static uint64_t last_tick = 0;
        uint64_t now_ms = GetTickCount64();
        if (now_ms - last_tick >= 250) {
            last_tick = now_ms;
            tracker().tick();
        }
    }

    if (!s.window_open) return 0;

    apply_window_pos(s.window_x, s.window_y);
    ImGui::SetNextWindowSize(ImVec2(s.window_w, s.window_h),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(s.window_alpha);

    // Fight-history navigation. viewed_start_wall is the source of truth —
    // it names one specific stored fight for as long as that fight exists.
    // viewed_fight is only the display ordinal (1 = most recent past fight)
    // derived from it each frame, so a fight closing mid-view renumbers the
    // label without moving the user onto different data. 0 = live.
    //
    // Every read below works off this one summaries vector, fetched under a
    // single lock. Reading a count and then indexing in a separate call let
    // a push land in between and shift the FIFO under us.
    static std::vector<FightSummary> hist;
    tracker().fight_summaries(hist);            // newest first
    int hist_n = static_cast<int>(hist.size());

    static uint64_t viewed_start_wall = 0;
    int viewed_fight = 0;
    if (viewed_start_wall != 0) {
        for (int i = 0; i < hist_n; ++i) {
            if (hist[i].start_wall == viewed_start_wall) {
                viewed_fight = i + 1;
                break;
            }
        }
        // Fell off the end of the FIFO — snap back to live.
        if (viewed_fight == 0) viewed_start_wall = 0;
    }

    bool viewing_history = viewed_fight > 0;
    if (viewing_history) {
        if (!tracker().snapshot_for(viewed_start_wall, g_rows)) {
            viewed_start_wall = 0;
            viewed_fight      = 0;
            viewing_history   = false;
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
        capture_window_pos(pos, s.window_x, s.window_y);
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
            // Sort control. Clicking a column header is the usual route,
            // but that row is optional — this menu is the path that keeps
            // working with headers hidden, and it drives the same persisted
            // sort_mode / sort_reverse the header writes to.
            ImGui::Separator();
            ImGui::TextDisabled("Sort by");
            {
                struct SortChoice { const char* label; int mode; };
                static constexpr SortChoice kSorts[] = {
                    {"Damage",   0},
                    {"DPS",      1},
                    {"Name",     2},
                    {"Time",     3},
                    {"Subgroup", 4},
                };
                for (const auto& c : kSorts) {
                    if (ImGui::MenuItem(c.label, nullptr, s.sort_mode == c.mode)) {
                        // Re-picking the active column flips direction, the
                        // same as clicking its header twice.
                        if (s.sort_mode == c.mode) s.sort_reverse = !s.sort_reverse;
                        else                       s.sort_mode    = c.mode;
                    }
                }
                bool rev = s.sort_reverse;
                if (ImGui::MenuItem("Reverse order", nullptr, rev))
                    s.sort_reverse = !rev;
            }

            // Fight history is reachable here too — the per-row menu
            // needs a row to right-click, which an empty table doesn't
            // have. Newest first (back = 1 is the most recent).
            ImGui::Separator();
            ImGui::TextDisabled("Fight history");
            // Current first, then newest past fight downwards. Current is
            // always listed, checked when active, so the menu always shows
            // where you are rather than starting at -1 with no anchor.
            if (ImGui::MenuItem("Current (live)", nullptr, viewed_fight == 0)) {
                viewed_start_wall = 0;
                ImGui::CloseCurrentPopup();
            }
            if (hist_n == 0) {
                ImGui::TextDisabled("(no past fights yet)");
            }
            // hist is newest-first, so index 0 is "-1".
            for (int i = 0; i < hist_n; ++i) {
                const FightSummary& fsum = hist[i];
                int back = i + 1;
                uint64_t dur = fsum.end_wall > fsum.start_wall
                             ? fsum.end_wall - fsum.start_wall : 0;
                char dbuf[32], cbuf[16], dmgbuf[16], label[192];
                format_time (dbuf, sizeof(dbuf), dur);
                format_clock(cbuf, sizeof(cbuf), fsum.end_clock);
                format_count(dmgbuf, sizeof(dmgbuf), fsum.total_damage);
                // Encounter name when arc logged a boss for this fight;
                // WvW and open-world pulls simply have none.
                snprintf(label, sizeof(label),
                         "-%d  %s  (%s)   %s dmg   %d players%s%s",
                         back, cbuf[0] ? cbuf : "--:--", dbuf,
                         dmgbuf, fsum.players,
                         fsum.boss_name ? "   - " : "",
                         fsum.boss_name ? fsum.boss_name : "");
                if (ImGui::MenuItem(label, nullptr, viewed_fight == back)) {
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
            const FightSummary& fsum = hist[viewed_fight - 1];
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
            if (fsum.boss_name) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.50f, 1.0f),
                                   "- %s", fsum.boss_name);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Live")) viewed_start_wall = 0;
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
        // Column count is fixed at 7 so ImGui's persisted per-column state
        // (width, order, visibility) keeps its identity; the optional
        // Subgroup column is enabled/disabled below rather than added and
        // removed, which would invalidate that state every toggle.
        if (ImGui::BeginTable("idps", 7, table_flags)) {
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
            ImGui::TableSetupColumn("Sub",
                                    ImGuiTableColumnFlags_WidthFixed |
                                    def_sort(4), 26.0f);
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
            if (s.show_headers) ImGui::TableHeadersRow();

            // Column visibility. Both rules write IsUserEnabledNextFrame
            // only when their input actually changed — writing it every
            // frame would stomp the user's own header-menu toggles and make
            // manually hiding a column impossible.
            if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                if (tbl->ColumnsCount >= 7) {
                    // Subgroup follows its setting outright: it is the
                    // switch for that column, so it wins over the menu.
                    static bool prev_sub = false;
                    static bool sub_init = false;
                    if (!sub_init || s.show_subgroup != prev_sub) {
                        tbl->Columns[1].IsUserEnabledNextFrame = s.show_subgroup;
                        prev_sub = s.show_subgroup;
                        sub_init = true;
                    }

                    // Drop low-priority columns as the window narrows.
                    // Sacrifice order: % -> Time -> Damage -> DPS. Prof and
                    // Name always stay.
                    if (s.responsive_columns) {
                        bool show_pct    = table_avail_w > 320.0f;
                        bool show_combat = table_avail_w > 270.0f;
                        bool show_dmg    = table_avail_w > 220.0f;
                        bool show_dps    = table_avail_w > 170.0f;
                        static bool prev_pct = true, prev_combat = true,
                                    prev_dmg = true, prev_dps = true;
                        static bool init = false;
                        if (!init || show_dps    != prev_dps)
                            tbl->Columns[3].IsUserEnabledNextFrame = show_dps;
                        if (!init || show_dmg    != prev_dmg)
                            tbl->Columns[4].IsUserEnabledNextFrame = show_dmg;
                        if (!init || show_combat != prev_combat)
                            tbl->Columns[5].IsUserEnabledNextFrame = show_combat;
                        if (!init || show_pct    != prev_pct)
                            tbl->Columns[6].IsUserEnabledNextFrame = show_pct;
                        prev_dps    = show_dps;
                        prev_dmg    = show_dmg;
                        prev_combat = show_combat;
                        prev_pct    = show_pct;
                        init = true;
                    }
                }
            }

            // Header-driven sorting. Skipped entirely when the header row is
            // hidden: the table still reports its DefaultSort spec even with
            // no header to click, and acting on it would overwrite whatever
            // the user picked from the right-click Sort by menu.
            ImGuiTableSortSpecs* specs = s.show_headers
                                       ? ImGui::TableGetSortSpecs() : nullptr;
            if (specs) {
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
                        // Column index -> sort_mode. Damage and % share a
                        // mode because % is derived from damage, so sorting
                        // by either produces the same order.
                        switch (col_idx) {
                            case 1: // Sub
                                s.sort_mode = 4;
                                reverse = !ascending;
                                break;
                            case 2: // Name
                                s.sort_mode = 2;
                                reverse = !ascending;
                                break;
                            case 3: // DPS
                                s.sort_mode = 1;
                                reverse = ascending;
                                break;
                            case 4: // Damage
                                s.sort_mode = 0;
                                reverse = ascending;
                                break;
                            case 5: // Time
                                s.sort_mode = 3;
                                reverse = ascending;
                                break;
                            case 6: // %
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
                // render on top of the bar in the same drawlist. Text uses
                // arc's base profession shade, the bar its highlight shade —
                // the same split arcdps itself draws with.
                ImU32 prof_col = prof_color(r.prof);
                ImU32 bar_base = prof_color_highlight(r.prof);
                if (!r.in_combat) {
                    prof_col = dim_color(prof_col);
                    bar_base = dim_color(bar_base);
                }

                if (max_damage > 0 && r.damage_total > 0) {
                    if (ImGuiTable* tbl = ImGui::GetCurrentContext()->CurrentTable) {
                        float frac = static_cast<float>(r.damage_total) /
                                     static_cast<float>(max_damage);
                        float bar_x0 = tbl->WorkRect.Min.x;
                        float bar_x1 = tbl->WorkRect.Max.x;
                        float row_h  = ImGui::GetTextLineHeight();
                        ImVec2 p0(bar_x0, ImGui::GetCursorScreenPos().y);
                        ImVec2 p1(bar_x0 + (bar_x1 - bar_x0) * frac, p0.y + row_h);
                        ImU32 bar_col = with_alpha(bar_base, s.bar_alpha);
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
                    ImGui::PushStyleColor(ImGuiCol_Text, prof_col);
                    ImGui::TextUnformatted(prof_short(r.prof));
                    ImGui::PopStyleColor();
                }
                // The icon alone doesn't say which elite spec it is at 14px.
                {
                    char spec[32];
                    format_spec(spec, sizeof(spec), r.prof, r.elite);
                    item_tooltip(spec);
                }

                ImGui::TableNextColumn();
                if (r.subgroup != 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, subgroup_color(r.subgroup));
                    ImGui::Text("%u", static_cast<unsigned>(r.subgroup));
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextDisabled("-");
                }

                ImGui::TableNextColumn();
                {
                    // Names render in the default text colour, so they match
                    // the metric columns beside them exactly rather than
                    // approximating white — arc's style owns that colour.
                    // They are also deliberately never dimmed out of combat:
                    // the profession icon and the damage bar already carry
                    // the combat state, and shading the names as well left
                    // the table looking inconsistent between rows.
                    bool tint_name = r.is_self && s.self_name_gold;
                    if (tint_name)
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              IM_COL32(255, 200, 60, 255));
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
                                    ? tracker().top_skills_for(viewed_start_wall,
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
                        // Current first, then newest downwards — same order
                        // as the window menu.
                        if (ImGui::MenuItem("Current (live)", nullptr,
                                            viewed_fight == 0)) {
                            viewed_start_wall = 0;
                            ImGui::CloseCurrentPopup();
                        }
                        if (hist_n == 0) {
                            ImGui::TextDisabled("(no past fights yet)");
                        }
                        // hist is newest-first, so index 0 is "-1".
                        for (int i = 0; i < hist_n; ++i) {
                            const FightSummary& fsum = hist[i];
                            int back = i + 1;
                            uint64_t dur = fsum.end_wall > fsum.start_wall
                                         ? fsum.end_wall - fsum.start_wall : 0;
                            char dbuf[32], cbuf[16];
                            format_time (dbuf, sizeof(dbuf), dur);
                            format_clock(cbuf, sizeof(cbuf), fsum.end_clock);
                            const char* clock = cbuf[0] ? cbuf : "--:--";
                            const char* boss  = fsum.boss_name;
                            Snapshot ag{};
                            char label[224];
                            if (tracker().agent_snapshot_for(fsum.start_wall,
                                                             r.id, ag)) {
                                char dpsbuf[16], dmgbuf[16];
                                format_count(dpsbuf, sizeof(dpsbuf), ag.dps);
                                format_count(dmgbuf, sizeof(dmgbuf), ag.damage_total);
                                snprintf(label, sizeof(label),
                                         "-%d  %s  (%s)   %s DPS   %s dmg%s%s",
                                         back, clock, dbuf, dpsbuf, dmgbuf,
                                         boss ? "   - " : "", boss ? boss : "");
                            } else {
                                snprintf(label, sizeof(label),
                                         "-%d  %s  (%s)   (not present)%s%s",
                                         back, clock, dbuf,
                                         boss ? "   - " : "", boss ? boss : "");
                            }
                            if (ImGui::MenuItem(label, nullptr,
                                                viewed_fight == back)) {
                                viewed_start_wall = fsum.start_wall;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    if (tint_name) ImGui::PopStyleColor();
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

    bool go_live = false;
    if (s.cleanses_open || s.strips_open) {
        draw_support_window("Cleanses", &s.cleanses_open, g_rows,
                            &Snapshot::cleanse_count, viewed_fight, &go_live);
        draw_support_window("Strips",   &s.strips_open,   g_rows,
                            &Snapshot::strip_count,   viewed_fight, &go_live);
    }
    if (s.downs_open)
        draw_downs_window(&s.downs_open, g_rows, viewed_fight, &go_live);
    draw_detail_window(viewed_fight, viewed_start_wall);
    if (go_live) viewed_start_wall = 0;

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
