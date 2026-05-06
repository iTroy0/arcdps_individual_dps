#include "tracker.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <windows.h>

#include "log.h"

namespace idps {

Tracker& tracker() {
    static Tracker t;
    return t;
}

Options& options() {
    static Options o;
    return o;
}

namespace {
    uint64_t wall_now() { return GetTickCount64(); }

    // Boons we count for "strips". Sorted ascending → binary_search.
    constexpr uint32_t kBoonIds[] = {
          717, // Protection
          718, // Regeneration
          719, // Swiftness
          725, // Fury
          726, // Vigor
          740, // Might
          743, // Aegis
          873, // Retaliation/Resolution
         1122, // Stability
         1187, // Quickness
        26980, // Resistance
        30328, // Alacrity
    };

    // Conditions we count for "cleanses". Sorted ascending → binary_search.
    constexpr uint32_t kConditionIds[] = {
          720, // Blind
          721, // Crippled
          722, // Chilled
          723, // Poison
          727, // Immobile
          736, // Bleeding
          737, // Burning
          738, // Vulnerability
          742, // Weakness
          791, // Fear
          861, // Confusion
        19426, // Torment
        26766, // Slow
        27705, // Taunt
    };

    bool is_boon(uint32_t id) {
        return std::binary_search(std::begin(kBoonIds), std::end(kBoonIds), id);
    }

    bool is_condition(uint32_t id) {
        return std::binary_search(std::begin(kConditionIds), std::end(kConditionIds), id);
    }

    bool looks_like_player(const ag* a, const ag* dst = nullptr) {
        if (!a) return false;
        if (a->self) return true;
        // arcdps populates dst->self for local player tracking-add.
        if (dst && dst->self) return true;
        // Remote player tracking-add: dst->name is an account name starting
        // with ':' (e.g. ":Troy.4370"). Minion/effect events have null dst.
        if (dst && dst->name && dst->name[0] == ':') return true;
        return false;
    }

    // Heuristic for state-only events (no dst) where we still want to
    // recognize a squadmate. Player profs are 1..9 and elite != 0xFFFFFFFF
    // (which marks NPCs).
    bool prof_looks_like_player(const ag* a) {
        if (!a) return false;
        return a->prof >= 1 && a->prof <= 9 && a->elite != 0xFFFFFFFFu;
    }

    // Target-type classification per deltaconnected evtc README:
    //   Player: elite != 0xFFFFFFFF
    //   NPC:    elite == 0xFFFFFFFF AND (prof & 0xFFFF0000) == 0xFFFF0000
    //   Gadget: elite == 0xFFFFFFFF AND (prof & 0xFFFF0000) != 0xFFFF0000
    enum class TargetType { Player, Npc, Gadget };

    TargetType classify_target(const ag* dst) {
        if (dst->elite != 0xFFFFFFFFu) return TargetType::Player;
        if ((dst->prof & 0xFFFF0000u) == 0xFFFF0000u) return TargetType::Npc;
        return TargetType::Gadget;
    }

    // Special Forces Training Area golems are classified as Gadgets by
    // arc, so "Exclude Gadgets" silently filters golem damage and breaks
    // build testing. Whitelist by name so the toggle stays useful for
    // real WvW gadgets (siege etc.) without dropping golem damage.
    bool is_training_golem(const ag* a) {
        if (!a || !a->name) return false;
        return std::strstr(a->name, "Kitty Golem") != nullptr;
    }

    // Arc stores player class info in dst on tracking-add, not src.
    void resolve_prof_elite(const ag* src, const ag* dst, uint32_t& prof, uint32_t& elite) {
        prof = 0; elite = 0;
        if (dst && dst->prof >= 1 && dst->prof <= 9) {
            prof = dst->prof; elite = dst->elite;
            return;
        }
        if (src && src->prof >= 1 && src->prof <= 9) {
            prof = src->prof; elite = src->elite;
        }
    }

    bool has_fight_state(const AgentState& s) {
        return s.accumulated_ms > 0 || s.damage_total > 0 ||
               s.strip_count > 0 || s.cleanse_count > 0;
    }

    void reset_for_new_fight(AgentState& s) {
        s.accumulated_ms      = 0;
        s.damage_total        = 0;
        s.first_damage_wall   = 0;
        s.last_damage_wall    = 0;
        s.strip_count         = 0;
        s.cleanse_count       = 0;
        s.damage_to_downed    = 0;
        s.downs_contributed   = 0;
        s.alive               = true;
        s.skills.clear();
        s.history.clear();
    }
}

void Tracker::on_combat(cbtevent* ev, ag* src, ag* dst,
                        const char* skillname, uint64_t /*id*/, uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ev) {
        if (src && src->id) {
            if (src->prof != 0 && src->elite != 0xFFFFFFFFu) {
                if (!looks_like_player(src, dst)) return;
                uint32_t prof = 0, elite = 0;
                resolve_prof_elite(src, dst, prof, elite);
                auto& s = agents_[src->id];
                s.id        = src->id;
                s.name      = src->name ? src->name : "";
                s.prof      = prof;
                s.elite     = elite;
                s.is_self   = (src->self != 0) || (dst && dst->self != 0);
                s.is_player = true;
                s.present   = true;
                if (dst && dst->id) {
                    s.instid = static_cast<uint16_t>(dst->id & 0xFFFFu);
                    instid_to_id_[s.instid] = src->id;
                }
            } else if (src->elite == 0xFFFFFFFFu) {
                // Agent removed — keep state for display until fight reset,
                // but drop the instid mapping so new instids can rebind.
                auto it = agents_.find(src->id);
                if (it != agents_.end()) {
                    instid_to_id_.erase(it->second.instid);
                    it->second.present = false;
                }
            }
        }
        return;
    }

    // Refresh prof/elite on any event carrying updated class info. Arc does
    // not always re-fire tracking-add after a build/spec swap, but damage
    // and state-change events usually carry the current prof/elite.
    if (src && src->id) {
        auto it = agents_.find(src->id);
        if (it != agents_.end() && it->second.is_player) {
            if (src->prof >= 1 && src->prof <= 9) {
                it->second.prof  = src->prof;
                it->second.elite = src->elite;
            }
        }
    }

    if (ev->is_statechange != CBTS_NONE) {
        on_statechange(ev, src, dst);
        return;
    }

    on_damage(ev, src, dst, skillname, revision);
}

void Tracker::on_statechange(cbtevent* ev, ag* src, ag* dst) {
    switch (ev->is_statechange) {
        case CBTS_ENTERCOMBAT:
            enter_combat(src, ev->time);
            break;
        case CBTS_EXITCOMBAT:
            exit_combat(src, ev->time);
            break;
        case CBTS_CHANGEDEAD:
            force_exit(src, ev->time);
            if (src && src->id) {
                auto it = agents_.find(src->id);
                if (it != agents_.end()) it->second.alive = false;
            }
            break;
        case CBTS_CHANGEDOWN:
            // Foe just went into downstate — credit every player who damaged
            // it during this fight. damage_to_downed accumulates the dealt
            // damage (matches arc's "down contribution"); downs_contributed
            // counts distinct downs the player touched. Drained so a re-down
            // of the same target doesn't re-credit prior damage.
            if (src && src->id) {
                auto tit = target_dmg_.find(src->id);
                if (tit != target_dmg_.end()) {
                    for (const auto& [aid, dmg] : tit->second) {
                        auto ait = agents_.find(aid);
                        if (ait != agents_.end()) {
                            ait->second.damage_to_downed   += dmg;
                            ait->second.downs_contributed  += 1;
                        }
                    }
                    target_dmg_.erase(tit);
                }
            }
            break;
        case CBTS_SQCOMBATSTART:
            // Per-self semantics: do not wipe squadmates here. Each
            // player resets via their own CBTS_ENTERCOMBAT (or implicit
            // enter on first credited damage). This keeps OOC players'
            // previous fight stats visible until they themselves engage,
            // so a squadmate's pull cannot reset rows for people who are
            // not yet in combat.
            in_encounter_ = true;
            break;
        case CBTS_SQCOMBATEND:
            for (auto& [id, s] : agents_) {
                if (s.in_combat_wall) {
                    uint64_t w = wall_now();
                    if (w > *s.in_combat_wall) s.accumulated_ms += w - *s.in_combat_wall;
                    s.in_combat_wall.reset();
                }
            }
            any_in_combat_ = false;
            in_encounter_ = false;
            break;
        default:
            break;
    }
    (void)dst;
}

void Tracker::enter_combat(ag* src, uint64_t time) {
    if (!src || !src->id) return;
    auto* s = touch_agent(src);
    if (!s) {
        // Squadmate ENTERCOMBAT before tracking-add — register lazily.
        // No dst is passed for state-changes, so we fall back to a prof-only
        // heuristic. Anything that doesn't look like a player is dropped.
        if (prof_looks_like_player(src)) {
            auto& as = agents_[src->id];
            as.id        = src->id;
            as.name      = src->name ? src->name : "";
            as.prof      = src->prof;
            as.elite     = src->elite;
            as.is_player = true;
            as.present   = true;
            s = &as;
        }
    }
    if (!s) return;

    if (!s->in_combat_wall) {
        // Per-self fight reset: squadmate combat state does not dilute
        // this player's stats.
        if (has_fight_state(*s)) reset_for_new_fight(*s);
        s->in_combat_wall = wall_now();
    }
    (void)time;

    any_in_combat_ = std::any_of(agents_.begin(), agents_.end(),
        [](const auto& p) { return p.second.in_combat_wall.has_value(); });
}

void Tracker::exit_combat(ag* src, uint64_t time) {
    if (!src || !src->id) return;
    auto it = agents_.find(src->id);
    if (it == agents_.end()) return;
    auto& s = it->second;
    if (s.in_combat_wall) {
        uint64_t w = wall_now();
        if (w > *s.in_combat_wall) s.accumulated_ms += w - *s.in_combat_wall;
        s.in_combat_wall.reset();
    }
    (void)time;
    any_in_combat_ = std::any_of(agents_.begin(), agents_.end(),
        [](const auto& p) { return p.second.in_combat_wall.has_value(); });
}

void Tracker::force_exit(ag* src, uint64_t time) {
    exit_combat(src, time);
}

void Tracker::on_damage(cbtevent* ev, ag* src, ag* dst,
                        const char* skillname, uint64_t revision) {
    if (!src) return;

    if (skillname && *skillname && ev->skillid != 0) {
        auto it = skill_names_.find(ev->skillid);
        if (it == skill_names_.end()) skill_names_.emplace(ev->skillid, skillname);
    }

    // Strips/cleanses. Arcdps buff_remove semantics: src is the agent that
    // LOST the buff, dst is the agent that caused the removal. To attribute
    // the strip/cleanse to the remover (the player), we look up dst->id, not
    // src->id. is_buffremove==1 (ALL) is the actual strip/cleanse event;
    // ==2 fires per stack on natural expiry and would massively over-count.
    // Runs before the iff==0 filter since cleanses are friend-on-friend
    // events that the damage path drops.
    if (ev->buff != 0 && ev->is_buffremove == 1 && dst && dst->id) {
        auto it = agents_.find(dst->id);
        if (it != agents_.end() && it->second.is_player) {
            bool filtered = false;
            if (src) {
                auto tt = classify_target(src);
                if (options().exclude_gadgets.load(std::memory_order_relaxed) &&
                    tt == TargetType::Gadget) filtered = true;
                if (options().exclude_npcs.load(std::memory_order_relaxed) &&
                    tt == TargetType::Npc) filtered = true;
            }
            if (!filtered) {
                // iff is from src's (victim's) perspective. A foe whose boon
                // was just stripped sees the remover as a foe (iff==1); an
                // ally whose condi was just cleansed sees the remover as a
                // friend (iff==0).
                if (ev->iff == 1 && is_boon(ev->skillid)) {
                    it->second.strip_count++;
                } else if (ev->iff == 0 && is_condition(ev->skillid)) {
                    it->second.cleanse_count++;
                }
            }
        }
        return;
    }

    if (ev->iff == 0) return;

    if (dst) {
        auto tt = classify_target(dst);
        // SFTA training golems classify as Gadget per arc, but mentally
        // they're training NPCs — re-tag so Exclude Gadgets doesn't drop
        // build-test damage and Exclude NPCs is the toggle that controls
        // them.
        if (is_training_golem(dst)) tt = TargetType::Npc;
        // Log each target's classification once per fight so users can
        // see whether their test golem is being treated as Gadget or NPC
        // when "Exclude X" toggles look like they're misbehaving.
        if (dst->id && logged_targets_.insert(dst->id).second) {
            log_line("target id=%llu name=%s class=%s prof=0x%08x elite=0x%08x",
                     static_cast<unsigned long long>(dst->id),
                     dst->name ? dst->name : "?",
                     tt == TargetType::Gadget ? "Gadget" :
                     tt == TargetType::Npc    ? "NPC" : "Player",
                     dst->prof, dst->elite);
        }
        if (options().exclude_gadgets.load(std::memory_order_relaxed) &&
            tt == TargetType::Gadget) return;
        if (options().exclude_npcs.load(std::memory_order_relaxed) &&
            tt == TargetType::Npc) return;
    }

    // Lazy-register self on first damage — in solo play arc may never fire
    // a tracking-add for the local player on `combat`, only on `combat_local`
    // damage events where src->self == 1. Seed the instid mapping here too so
    // pet/minion attribution via src_master_instid resolves on first hit.
    // Guard with the same player-prof heuristic used elsewhere so a
    // self-marked NPC entity (boss-disguise, gizmo, mount) cannot create a
    // malformed AgentState with elite == 0xFFFFFFFFu.
    if (src->self && src->id && prof_looks_like_player(src)) {
        auto& s = agents_[src->id];
        if (s.id == 0) {
            s.id        = src->id;
            s.name      = src->name ? src->name : "";
            s.prof      = src->prof;
            s.elite     = src->elite;
            s.is_self   = true;
            s.is_player = true;
        }
        if (s.instid == 0 && ev->src_instid != 0) {
            s.instid = ev->src_instid;
            instid_to_id_[s.instid] = src->id;
        }
    }

    AgentState* owner = nullptr;
    if (ev->src_master_instid != 0) {
        owner = find_by_instid(ev->src_master_instid);
    }
    if (!owner && src->id) {
        auto it = agents_.find(src->id);
        if (it != agents_.end()) owner = &it->second;
    }
    if (!owner && ev->src_instid != 0) {
        owner = find_by_instid(ev->src_instid);
    }
    if (!owner || !owner->is_player) return;

    uint64_t delta = 0;
    if (ev->buff == 0) {
        // Strike damage. Positive value only — negative strikes in arc's
        // post-2026-04-14 feed represent barrier-absorbed / invulnerable
        // hits that arc itself does not count toward DPS.
        switch (ev->result) {
            case CBTR_BLOCK:
            case CBTR_EVADE:
            case CBTR_INTERRUPT:
            case CBTR_ABSORB:
            case CBTR_BLIND:
                break;
            default:
                if (ev->value > 0) delta = static_cast<uint64_t>(ev->value);
                break;
        }
    } else {
        // Buff (condition) damage tick. Skip buff-application/removal events.
        if (ev->buff_dmg > 0 && ev->is_buffremove == 0) {
            delta = static_cast<uint64_t>(ev->buff_dmg);
        }
        (void)revision;
    }

    if (delta == 0) return;

    uint64_t now = wall_now();
    // Implicit-enter only when arc skipped CBTS_ENTERCOMBAT for the local
    // player. Restricted to (a) self as the literal damage source — pets and
    // minions have src->self == 0 even when attributed to the master, so
    // their auto-attacks while OOC cannot restart a fight; and (b) STRIKE
    // events (ev->buff == 0) — condi tail ticks landing after EXITCOMBAT
    // would otherwise look like a fresh fight.
    if (!owner->in_combat_wall) {
        bool from_self = src->self != 0;
        bool is_strike = ev->buff == 0;
        bool can_start = from_self && is_strike;
        // If the squad is already fighting, allow pets/condi to enter combat
        // for their owner. Only self-strikes may cold-start a fight.
        if (!can_start && !any_in_combat_) return;
        if (has_fight_state(*owner)) reset_for_new_fight(*owner);
        owner->in_combat_wall = now;
        any_in_combat_ = true;
    } else if (!in_encounter_ && owner->last_damage_wall != 0) {
        // GW2 keeps the player in combat for ~4 s after the last hit.
        // If arc never sent EXITCOMBAT and we start a new pull, treat a
        // long idle gap as a fight boundary so old damage doesn't leak over.
        uint64_t idle = now > owner->last_damage_wall
                      ? now - owner->last_damage_wall : 0;
        if (idle > 5000) {
            reset_for_new_fight(*owner);
            owner->in_combat_wall = now;
        }
    }

    if (owner->first_damage_wall == 0) owner->first_damage_wall = now;
    owner->last_damage_wall = now;
    owner->damage_total += delta;

    auto& sk = owner->skills[ev->skillid];
    sk.damage += delta;
    sk.hits   += 1;
    if (sk.first_hit_wall == 0) sk.first_hit_wall = now;
    sk.last_hit_wall = now;
    // Per-skill hit timeline drives the spike overlay in the detail
    // graph. Capped per-skill so heavy condi tickers (Burning, Bleed)
    // can't blow memory on long fights.
    sk.hits_history.push_back({now, delta});
    if (sk.hits_history.size() > 1024) sk.hits_history.pop_front();

    // Per-target damage attribution drains on CBTS_CHANGEDOWN of the
    // target into damage_to_downed. dst is non-null in real damage events
    // — guarded anyway since condi-tail / minion ticks can fire with
    // partial event payloads.
    if (dst && dst->id) {
        target_dmg_[dst->id][owner->id] += delta;
    }

    // Sample cumulative damage every ~500ms for the detail-window graph.
    // Deque pop_front is O(1) — vector::erase(begin) was O(n) and stalled
    // the combat thread during long fights once the cap was reached.
    if (owner->history.empty() ||
        now - owner->history.back().wall_ms >= 500) {
        owner->history.push_back({now, owner->damage_total});
        if (owner->history.size() > 4096) {
            owner->history.pop_front();
        }
    } else {
        owner->history.back().damage_total = owner->damage_total;
    }
    (void)dst;
}

AgentState* Tracker::touch_agent(ag* src) {
    if (!src || !src->id) return nullptr;
    auto it = agents_.find(src->id);
    if (it != agents_.end()) return &it->second;
    // Only self can be created lazily here. Squad members must be added via
    // tracking-add; bosses/NPCs are rejected.
    if (!src->self) return nullptr;
    auto& s = agents_[src->id];
    s.id        = src->id;
    s.name      = src->name ? src->name : "";
    s.prof      = src->prof;
    s.elite     = src->elite;
    s.is_self   = true;
    s.is_player = true;
    return &s;
}

AgentState* Tracker::find_by_instid(uint16_t instid) {
    auto it = instid_to_id_.find(instid);
    if (it != instid_to_id_.end()) {
        auto sit = agents_.find(it->second);
        if (sit != agents_.end()) return &sit->second;
    }
    // Fallback: after reset_fight() clears the map, agents still retain
    // their instid until a new tracking-add or lazy-register rebinds them.
    for (auto& [id, s] : agents_) {
        if (s.instid == instid) return &s;
    }
    return nullptr;
}

void Tracker::reset_fight() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = agents_.begin(); it != agents_.end(); ) {
        auto& s = it->second;
        if (!s.present && !s.is_self) {
            it = agents_.erase(it);
        } else {
            s.accumulated_ms      = 0;
            s.damage_total        = 0;
            s.in_combat_wall.reset();
            s.first_damage_wall   = 0;
            s.last_damage_wall    = 0;
            s.strip_count         = 0;
            s.cleanse_count       = 0;
            s.damage_to_downed    = 0;
            s.downs_contributed   = 0;
            s.alive               = true;
            s.skills.clear();
            s.history.clear();
            // Drop the per-agent instid alongside the global map so
            // find_by_instid()'s linear fallback can't match a stale
            // binding after instids get recycled between fights.
            s.instid              = 0;
            ++it;
        }
    }
    instid_to_id_.clear();
    logged_targets_.clear();
    target_dmg_.clear();
    any_in_combat_ = false;
}

void Tracker::detail(uintptr_t id, AgentDetail& out) const {
    out.skills.clear();
    out.history.clear();
    out.name.clear();
    out.prof = 0;
    out.elite = 0;
    out.history_start_wall = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = agents_.find(id);
        if (it == agents_.end()) return;
        const auto& s = it->second;
        out.name  = s.name;
        out.prof  = s.prof;
        out.elite = s.elite;
        out.history_start_wall = s.first_damage_wall;
        out.history.assign(s.history.begin(), s.history.end());
        out.skills.reserve(s.skills.size());
        for (const auto& [sid, entry] : s.skills) {
            SkillDetail sd;
            sd.skill_id       = sid;
            auto nit          = skill_names_.find(sid);
            sd.name           = nit != skill_names_.end() ? nit->second : std::string();
            sd.damage         = entry.damage;
            sd.hits           = entry.hits;
            sd.first_hit_wall = entry.first_hit_wall;
            sd.last_hit_wall  = entry.last_hit_wall;
            sd.hits_history.assign(entry.hits_history.begin(),
                                   entry.hits_history.end());
            out.skills.push_back(std::move(sd));
        }
    }
    // Sort outside the lock so combat thread isn't blocked by std::sort
    // for the duration of skill-table sorting (worst case ~100 entries).
    std::sort(out.skills.begin(), out.skills.end(),
        [](const SkillDetail& a, const SkillDetail& b) { return a.damage > b.damage; });
}

void Tracker::snapshot(std::vector<Snapshot>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    out.reserve(agents_.size());
    uint64_t now = wall_now();
    for (const auto& [id, s] : agents_) {
        if (!s.is_player) continue;
        // Drop squadmates that arc has marked as removed (e.g. left squad,
        // changed instance) so old DPS rows from a previous squad don't
        // linger after joining a new one. Self always stays so the local
        // player's previous-fight stats remain visible OOC.
        if (!s.present && !s.is_self) continue;
        // Combat time = active-damage window (first -> last damage event),
        // extending to now while the agent is still in combat. Matches
        // arc's Damage panel denominator.
        uint64_t ms = 0;
        if (s.first_damage_wall != 0) {
            uint64_t end = s.in_combat_wall ? now : s.last_damage_wall;
            if (end > s.first_damage_wall) ms = end - s.first_damage_wall;
        }
        // Fight-average DPS over the active-damage window with a 500ms
        // floor — recognizes the initial damage spike (first hit reads as
        // damage / 500ms) then settles to the running average.
        uint64_t denom = ms < 500 ? 500 : ms;
        uint64_t dps   = s.damage_total * 1000ull / denom;
        out.push_back(Snapshot{
            s.id, s.name, s.prof, s.elite,
            ms, s.damage_total, dps,
            s.strip_count, s.cleanse_count,
            s.damage_to_downed, s.downs_contributed,
            s.in_combat_wall.has_value(),
            s.is_self,
        });
    }
}

} // namespace idps
