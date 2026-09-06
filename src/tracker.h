#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "arcdps_api.h"

namespace idps {

struct SkillHit {
    uint64_t wall_ms;
    uint64_t damage;
};

struct SkillEntry {
    uint64_t damage          = 0;
    uint32_t hits            = 0;
    // Strike-only counters: condi ticks can't crit, so crit% is computed
    // against strike_hits, not hits.
    uint32_t strike_hits     = 0;
    uint32_t crits           = 0;
    uint64_t min_hit         = 0;
    uint64_t max_hit         = 0;
    uint64_t first_hit_wall  = 0;
    uint64_t last_hit_wall   = 0;
    // Bounded; oldest entries pruned first.
    std::deque<SkillHit> hits_history;
};

struct DamagePoint {
    uint64_t wall_ms;
    uint64_t damage_total;
};

struct AgentState {
    uintptr_t             id              = 0;
    std::string           name;
    // GW2 account ("Account.1234"), captured from dst->name on tracking-add
    // (arc prefixes ':'; stored without it). Empty until a tracking-add
    // carrying it arrives. Identity — not cleared on fight reset.
    std::string           account;
    uint32_t              prof            = 0;
    uint32_t              elite           = 0;
    uint16_t              instid          = 0;
    // Squad subgroup (1..15), 0 = unknown. arc delivers it as dst->team on a
    // player tracking-add and as dst_agent on CBTS_ENTERCOMBAT / EXITCOMBAT.
    uint16_t              subgroup        = 0;
    // World/team id. src->team on tracking-add, refreshed by CBTS_TEAMCHANGE.
    uint16_t              team            = 0;
    bool                  is_self         = false;
    bool                  is_player       = false;
    bool                  present         = true;

    // Wall-clock ms (GetTickCount64). Arc event times are non-monotonic.
    uint64_t              accumulated_ms  = 0;
    std::optional<uint64_t> in_combat_wall;
    uint64_t              damage_total    = 0;
    bool                  alive           = true;

    // Deferred fight reset (smart fight boundaries). Set on ENTERCOMBAT
    // when the agent's last action is older than the fight gap: the row
    // keeps its previous-fight stats until the FIRST credited action
    // (damage or cleanse/strip) of the new fight, so passive combat entry
    // (NPC aggro, stray AoE) never wipes a row. Cleared on the deferred
    // reset, on combat exit, and on every global fight boundary.
    bool                  fight_armed     = false;
    // Last credited action (damage dealt or cleanse/strip). Drives the
    // resume-vs-new-fight decision on combat entry.
    uint64_t              last_activity_wall = 0;

    // Active-damage window matches arc's Damage panel.
    uint64_t              first_damage_wall = 0;
    uint64_t              last_damage_wall  = 0;

    // Wall-clock of the last arc event referencing this agent. Drives
    // the snapshot() staleness filter so squadmates from a prior map
    // (no events firing in your range) drop after kStaleAgentMs of
    // silence — arc has no realtime CBTS_MAPCHANGE / DESPAWN signal we
    // can hook for the local player.
    uint64_t              last_seen_wall    = 0;

    uint32_t              strip_count   = 0;
    uint32_t              cleanse_count = 0;

    uint64_t              damage_to_downed   = 0;
    uint32_t              downs_contributed  = 0;
    // Killing blows landed on enemy players (CBTR_KILLINGBLOW).
    uint32_t              kills_contributed  = 0;
    // CBTR_INTERRUPT: this agent's skill interrupted the target's action.
    uint32_t              interrupts         = 0;
    // CBTR_CROWDCONTROL, whose `value` is the applied disable in ms. Both
    // the event count and the summed duration are kept — a single long
    // stun and five short dazes are very different contributions.
    uint32_t              cc_count           = 0;
    uint64_t              cc_duration_ms     = 0;

    // deque so FIFO cap is O(1) — vector::erase(begin()) would shift 4095
    // entries on every sample once full.
    std::unordered_map<uint32_t, SkillEntry> skills;
    std::deque<DamagePoint>                  history;
};

struct SkillDetail {
    uint32_t                skill_id;
    std::string             name;
    uint64_t                damage;
    uint32_t                hits;
    uint32_t                strike_hits;
    uint32_t                crits;
    uint64_t                min_hit;
    uint64_t                max_hit;
    uint64_t                first_hit_wall;
    uint64_t                last_hit_wall;
    std::vector<SkillHit>   hits_history;
};

struct AgentDetail {
    std::string              name;
    std::string              account;
    uint32_t                 prof     = 0;
    uint32_t                 elite    = 0;
    uint16_t                 subgroup = 0;
    std::vector<DamagePoint> history;
    uint64_t                 history_start_wall = 0;
    std::vector<SkillDetail> skills;
};

// Stored in Tracker::history_ when a fight closes (idle auto-close,
// SQCOMBATEND, map change, or manual reset). Keeps full agent state minus
// per-skill hits_history, which is cleared before the push — that per-hit
// timeline is the only structure that grows without a useful bound.
//
// What remains still scales with squad size: the per-agent DamagePoint
// deque is capped at 4096 entries of 16 bytes, so roughly 64 KB per agent
// plus its skill table. A 50-player squad is therefore on the order of
// 3-4 MB per stored fight, and ~15-20 MB across all kHistoryMax slots.
//
// Because hits_history is gone, the detail graph's per-skill spike overlay
// cannot render for a past fight — the UI disables that control while
// viewing history rather than leaving a dead one on screen. Everything else
// (DPS curve, skills table totals, sort, support windows) works.
struct FightSnapshot {
    uint64_t                                  start_wall = 0;
    uint64_t                                  end_wall   = 0;
    // Unix seconds captured at fight close. start/end_wall are
    // GetTickCount64 ms (monotonic since boot) and cannot be mapped to
    // clock time after the fact, so the close moment is stamped here.
    // Preferred source is arc's own CBTS_SQCOMBATEND timestamp; where that
    // never fires (WvW) it is derived from the last credited hit.
    uint64_t                                  end_clock  = 0;
    // Boss species id from CBTS_LOGNPCUPDATE, 0 when the fight had no
    // log target (open world, WvW). Resolved to a name for display.
    uint32_t                                  boss_species = 0;
    std::unordered_map<uintptr_t, AgentState> agents;
};

// Lightweight per-fight header for history menus: no agent copies, safe
// to call every frame while a popup is open.
struct FightSummary {
    uint64_t start_wall   = 0;
    uint64_t end_wall     = 0;
    uint64_t end_clock    = 0; // unix seconds; 0 = unknown
    uint64_t total_damage = 0;
    int      players      = 0;
    // Encounter name from the fight's boss species id, empty when the
    // fight had no log target or the species isn't in the known table.
    const char* boss_name = nullptr;
};

struct Snapshot {
    uintptr_t   id                = 0;
    std::string name;
    std::string account;
    uint32_t    prof              = 0;
    uint32_t    elite             = 0;
    uint16_t    subgroup          = 0;
    uint64_t    combat_ms         = 0;
    uint64_t    damage_total      = 0;
    uint64_t    dps               = 0;
    uint32_t    strip_count       = 0;
    uint32_t    cleanse_count     = 0;
    uint64_t    damage_to_downed  = 0;
    uint32_t    downs_contributed = 0;
    uint32_t    kills_contributed = 0;
    uint32_t    interrupts        = 0;
    uint32_t    cc_count          = 0;
    uint64_t    cc_duration_ms    = 0;
    bool        in_combat         = false;
    bool        is_self           = false;
};

// Encounter name for a CBTS_LOGNPCUPDATE species id, or nullptr when the
// id isn't one of the documented log targets.
const char* boss_name_for(uint32_t species_id);

class Tracker {
public:
    void on_combat(cbtevent* ev, ag* src, ag* dst,
                   const char* skillname, uint64_t id, uint64_t revision);

    // Caller-owned output buffers avoid per-frame heap allocations.
    // spike_skill: per-hit history (hits_history) is copied only for this
    // skill id — the spike overlay reads exactly one skill's timeline and
    // copying the other ~50 deques per frame was the detail window's
    // dominant cost under the combat mutex. 0 = copy none.
    void snapshot(std::vector<Snapshot>& out) const;
    void detail(uintptr_t id, AgentDetail& out, uint32_t spike_skill = 0) const;
    void reset_fight();

    // Called from the render thread every frame or so. Closes a fight that
    // has gone quiet: arcdps only signals a fight boundary in instanced
    // content (CBTS_SQCOMBATEND), so in WvW nothing would ever end a fight
    // and the whole session accumulated into one history entry. Combat
    // events stop arriving once combat ends, so the tracker cannot notice
    // the gap on its own — it has to be driven from outside.
    void tick();

    // Past-fight history.
    //
    // Fights are addressed by their start_wall, never by position. An index
    // into the FIFO is only valid until the next push: at the cap, a push
    // pops the front and every index shifts by one. The UI reads the size
    // and the data in separate locked calls, so a push landing between them
    // would have silently renamed the fight being displayed. start_wall is
    // assigned once at fight open and never reused, so it stays correct.
    //
    // See FightSnapshot for memory caveats.
    //
    // Newest first: index 0 is the most recent past fight.
    void fight_summaries(std::vector<FightSummary>& out) const;

    bool snapshot_for(uint64_t start_wall, std::vector<Snapshot>& out) const;
    bool detail_for(uint64_t start_wall, uintptr_t agent_id, AgentDetail& out,
                    uint32_t spike_skill = 0) const;
    // Single-agent past-fight readout for the per-row "fight history"
    // context menu. Cheaper than snapshot_for + scanning.
    bool agent_snapshot_for(uint64_t start_wall, uintptr_t agent_id,
                            Snapshot& out) const;
    // Top-N skills by damage. Cheap (no DamagePoint history copy) so safe
    // to call every frame from a hover tooltip.
    bool top_skills(uintptr_t agent_id, int n,
                    std::vector<SkillDetail>& out) const;
    bool top_skills_for(uint64_t start_wall, uintptr_t agent_id, int n,
                        std::vector<SkillDetail>& out) const;

private:
    void on_statechange(cbtevent* ev, ag* src, ag* dst);
    void on_buff_remove(cbtevent* ev, ag* src, ag* dst);
    void on_damage(cbtevent* ev, ag* src, ag* dst,
                   const char* skillname, uint64_t revision);

    // CBTS_ENTERCOMBAT / EXITCOMBAT carry the agent's prof, elite spec and
    // subgroup in the event body (value / buff_dmg / dst_agent), which the
    // `ag` struct does not populate for state changes. Both take the event
    // so that identity can be refreshed on every combat transition.
    void enter_combat(cbtevent* ev, ag* src);
    void exit_combat(cbtevent* ev, ag* src);
    void force_exit(ag* src);

    AgentState* touch_agent(ag* src);
    AgentState* find_by_instid(uint16_t instid);

    // mutex_ guards agents_/instid_to_id_/skill_names_/target_dmg_/downed_.
    mutable std::mutex                              mutex_;
    std::unordered_map<uintptr_t, AgentState>       agents_;
    std::unordered_map<uint16_t, uintptr_t>         instid_to_id_;
    std::unordered_map<uint32_t, std::string>       skill_names_;
    // target_id -> attacker_id -> damage. Drained on the downing hit
    // (CBTR_DOWNED, or is_offcycle 0->1 transition as fallback). arc's
    // realtime feed delivers CHANGEDOWN/CHANGEDEAD only for squad members
    // per the evtc spec, so non-squad foes can't be drained that way.
    std::unordered_map<uintptr_t, std::unordered_map<uintptr_t, uint64_t>>
                                                    target_dmg_;
    // Detects the 0->1 is_offcycle transition: once a foe is downed, every
    // subsequent damage event on them carries is_offcycle == 1 per the evtc
    // spec. Realtime-safe for non-squad targets where CHANGEDOWN never fires.
    std::unordered_map<uintptr_t, bool>             downed_;
    // Most recent pre-down attacker per target. Per evtc spec is_offcycle
    // reports state at the START of the event, so the "discovery" event
    // that flips is_offcycle 0->1 fires AFTER the down — its owner is a
    // post-down cleaver, not the finisher. We credit the actual finisher
    // by recording every pre-down hit's attacker here and using that on
    // the discovery branch. Cleared anywhere target_dmg_ / downed_ are.
    std::unordered_map<uintptr_t, uintptr_t>        last_attacker_;
    bool                                            any_in_combat_   = false;
    bool                                            in_encounter_    = false;

    // FIFO of past fights, capped at kHistoryMax. push_to_history()
    // clones the current agents_ (clearing per-skill hits_history to
    // bound memory) and appends. Read by the UI via snapshot_for /
    // detail_for when the user navigates Fight -1, -2, ... in the main
    // Damage window header.
    static constexpr int                            kHistoryMax = 5;
    std::deque<FightSnapshot>                       history_;
    // Used as start_wall when pushing — set when the first agent enters
    // combat and we haven't recorded the current fight yet. Reset to 0
    // after push_to_history() runs so the next entry-into-combat
    // captures a fresh start.
    uint64_t                                        current_fight_start_wall_ = 0;
    // Boss species id for the fight in progress, from CBTS_LOGNPCUPDATE.
    uint32_t                                        current_boss_species_     = 0;
    // Unix seconds for the current fight's close, taken from arc's own
    // CBTS_SQCOMBATEND payload (buff_dmg = local unix timestamp). 0 means
    // arc never sent one — WvW, or a manual reset — and push_to_history
    // derives the stamp from the last credited hit instead.
    uint64_t                                        sq_end_clock_             = 0;

    void push_to_history();
    // Locate a stored fight by its start_wall. Caller holds mutex_.
    const FightSnapshot* find_fight(uint64_t start_wall) const;
};

Tracker& tracker();

struct Options {
    std::atomic<bool> exclude_npcs{false};
    std::atomic<bool> exclude_gadgets{false};
    // Smart fight boundaries: re-entering combat within fight_gap_ms of
    // the last action resumes the same fight; beyond it the row resets
    // lazily on the first action (see AgentState::fight_armed). Fights
    // shorter than the gap are skipped from history unless they scored a
    // down or kill. Disabled = legacy immediate reset on ENTERCOMBAT.
    std::atomic<bool>     fight_gap_enabled{true};
    std::atomic<uint32_t> fight_gap_ms{5000};
    // Idle-row reset: a player whose last credited action is older than
    // idle_reset_ms and who is not in combat reads as a zeroed row — they
    // keep their place in the list, their numbers do not. Applied by every
    // live reader (snapshot / detail / top_skills) so the row and its
    // drill-downs agree; stored-fight readers never apply it. Display only
    // — the agent's state is untouched, so a still-open fight cannot lose a
    // contributor and history is unaffected. See idle_expired() in
    // tracker.cpp for the exact rule.
    std::atomic<bool>     idle_reset_enabled{true};
    std::atomic<uint32_t> idle_reset_ms{120000};
};
Options& options();

} // namespace idps
