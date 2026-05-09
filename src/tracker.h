#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    uint32_t              prof            = 0;
    uint32_t              elite           = 0;
    uint16_t              instid          = 0;
    bool                  is_self         = false;
    bool                  is_player       = false;
    bool                  present         = true;

    // Wall-clock ms (GetTickCount64). Arc event times are non-monotonic.
    uint64_t              accumulated_ms  = 0;
    std::optional<uint64_t> in_combat_wall;
    uint64_t              damage_total    = 0;
    bool                  alive           = true;

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
    uint64_t                first_hit_wall;
    uint64_t                last_hit_wall;
    std::vector<SkillHit>   hits_history;
};

struct AgentDetail {
    std::string              name;
    uint32_t                 prof;
    uint32_t                 elite;
    std::vector<DamagePoint> history;
    uint64_t                 history_start_wall;
    std::vector<SkillDetail> skills;
};

// Stored in Tracker::history_ when a fight closes (SQCOMBATEND or manual
// reset). Keeps full agent state minus per-skill hits_history (cleared
// before push) — the per-hit timeline is the only structure that grows
// unbounded, so dropping it keeps each past fight under ~1 MB. Spike
// overlay on the detail graph will be empty for past fights; everything
// else (DPS curve, skills table totals, sort, support windows) works.
struct FightSnapshot {
    uint64_t                                  start_wall = 0;
    uint64_t                                  end_wall   = 0;
    std::unordered_map<uintptr_t, AgentState> agents;
};

struct Snapshot {
    uintptr_t   id;
    std::string name;
    uint32_t    prof;
    uint32_t    elite;
    uint64_t    combat_ms;
    uint64_t    damage_total;
    uint64_t    dps;
    uint32_t    strip_count;
    uint32_t    cleanse_count;
    uint64_t    damage_to_downed;
    uint32_t    downs_contributed;
    bool        in_combat;
    bool        is_self;
};

class Tracker {
public:
    void on_combat(cbtevent* ev, ag* src, ag* dst,
                   const char* skillname, uint64_t id, uint64_t revision);

    // Caller-owned output buffers avoid per-frame heap allocations.
    void snapshot(std::vector<Snapshot>& out) const;
    void detail(uintptr_t id, AgentDetail& out) const;
    void reset_fight();

    // Past-fight history (B+C design). Index 0 = oldest stored fight,
    // history_size()-1 = most recent past fight. Negative indices are
    // not used; the UI translates "Fight -1, -2..." into these forward
    // indices. See FightSnapshot for memory caveats.
    int  history_size() const;
    bool snapshot_at(int idx, std::vector<Snapshot>& out) const;
    bool detail_at(int idx, uintptr_t agent_id, AgentDetail& out) const;
    bool fight_times_at(int idx, uint64_t& start_wall, uint64_t& end_wall) const;
    // Single-agent past-fight readout for the per-row "fight history"
    // context menu. Cheaper than calling snapshot_at + scanning.
    bool agent_snapshot_at(int idx, uintptr_t agent_id, Snapshot& out) const;
    // Top-N skills by damage. Cheap (no DamagePoint history copy) so safe
    // to call every frame from a hover tooltip.
    bool top_skills(uintptr_t agent_id, int n,
                    std::vector<SkillDetail>& out) const;
    bool top_skills_at(int idx, uintptr_t agent_id, int n,
                       std::vector<SkillDetail>& out) const;

private:
    void on_statechange(cbtevent* ev, ag* src, ag* dst);
    void on_damage(cbtevent* ev, ag* src, ag* dst,
                   const char* skillname, uint64_t revision);

    void enter_combat(ag* src, uint64_t time);
    void exit_combat(ag* src, uint64_t time);
    void force_exit(ag* src, uint64_t time);

    AgentState* touch_agent(ag* src);
    AgentState* find_by_instid(uint16_t instid);

    // mutex_ guards agents_/instid_to_id_/skill_names_/target_dmg_/downed_/
    // logged_targets_.
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
    // Cleared on reset_fight so a new pull re-logs target classification.
    std::unordered_set<uintptr_t>                   logged_targets_;
    bool                                            any_in_combat_   = false;
    bool                                            in_encounter_    = false;

    // FIFO of past fights, capped at kHistoryMax. push_to_history()
    // clones the current agents_ (clearing per-skill hits_history to
    // bound memory) and appends. Read by the UI via snapshot_at /
    // detail_at when the user navigates Fight -1, -2, ... in the main
    // Damage window header.
    static constexpr int                            kHistoryMax = 5;
    std::deque<FightSnapshot>                       history_;
    // Used as start_wall when pushing — set when the first agent enters
    // combat and we haven't recorded the current fight yet. Reset to 0
    // after push_to_history() runs so the next entry-into-combat
    // captures a fresh start.
    uint64_t                                        current_fight_start_wall_ = 0;

    void push_to_history();
};

Tracker& tracker();

struct Options {
    std::atomic<bool> exclude_npcs{false};
    std::atomic<bool> exclude_gadgets{false};
};
Options& options();

} // namespace idps
