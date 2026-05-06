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
    // Active window for per-skill DPS — first / last credited hit.
    uint64_t first_hit_wall  = 0;
    uint64_t last_hit_wall   = 0;
    // Per-hit timeline for the detail window's spike overlay. Bounded so
    // long fights don't grow unbounded; oldest entries are pruned first.
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

    // Combat time is tracked in wall-clock ms (GetTickCount64). Arc event
    // times are non-monotonic and cause display flicker when used directly.
    uint64_t              accumulated_ms  = 0;
    std::optional<uint64_t> in_combat_wall;
    uint64_t              damage_total    = 0;
    bool                  alive           = true;

    // Active-damage window (matches arc's Damage panel): first/last wall-ms
    // at which this player landed credited damage this fight.
    uint64_t              first_damage_wall = 0;
    uint64_t              last_damage_wall  = 0;

    // Boon strips (removes from foes) + condition cleanses (removes from allies).
    uint32_t              strip_count   = 0;
    uint32_t              cleanse_count = 0;

    // Down contribution: cumulative damage this player dealt to foes who
    // subsequently went into downstate, plus the count of distinct downs
    // they contributed to. Awarded on CBTS_CHANGEDOWN of the foe.
    uint64_t              damage_to_downed   = 0;
    uint32_t              downs_contributed  = 0;

    // Per-skill damage + cumulative damage history for the detail window.
    // History is a deque so the FIFO cap is O(1) — vector::erase(begin())
    // would shift 4095 entries on every sample once full.
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

    // Caller-owned output buffers — avoids per-frame heap allocations.
    void snapshot(std::vector<Snapshot>& out) const;
    void detail(uintptr_t id, AgentDetail& out) const;
    void reset_fight();

private:
    void on_statechange(cbtevent* ev, ag* src, ag* dst);
    void on_damage(cbtevent* ev, ag* src, ag* dst,
                   const char* skillname, uint64_t revision);

    void enter_combat(ag* src, uint64_t time);
    void exit_combat(ag* src, uint64_t time);
    void force_exit(ag* src, uint64_t time);

    AgentState* touch_agent(ag* src);
    AgentState* find_by_instid(uint16_t instid);

    mutable std::mutex                              mutex_;
    std::unordered_map<uintptr_t, AgentState>       agents_;
    std::unordered_map<uint16_t, uintptr_t>         instid_to_id_;
    std::unordered_map<uint32_t, std::string>       skill_names_;
    // Per-foe damage attribution since fight start: target_id -> attacker_id
    // -> damage. Drained on CBTS_CHANGEDOWN of the foe so attackers receive
    // down-contribution credit, and re-down events don't double-count.
    std::unordered_map<uintptr_t, std::unordered_map<uintptr_t, uint64_t>>
                                                    target_dmg_;
    // Targets we've already written a classification line for this fight.
    // Cleared on reset_fight so a new pull re-logs (helps users diagnose
    // why an "Exclude Gadgets" toggle is filtering their training golem).
    std::unordered_set<uintptr_t>                   logged_targets_;
    bool                                            any_in_combat_   = false;
    bool                                            in_encounter_    = false;
};

Tracker& tracker();

struct Options {
    std::atomic<bool> exclude_npcs{false};
    std::atomic<bool> exclude_gadgets{false};
};
Options& options();

} // namespace idps
