#include "tracker.h"

#include <algorithm>
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

    // Skill id -> name, populated from the skillname arg arc passes on each
    // event. Guarded by the tracker mutex via accessors.
    std::unordered_map<uint32_t, std::string>& skill_names() {
        static std::unordered_map<uint32_t, std::string> m;
        return m;
    }

    bool is_boon(uint32_t id) {
        switch (id) {
            case   717: // Protection
            case   718: // Regeneration
            case   719: // Swiftness
            case   725: // Fury
            case   726: // Vigor
            case   740: // Might
            case   743: // Aegis
            case   873: // Retaliation/Resolution
            case  1122: // Stability
            case  1187: // Quickness
            case 26980: // Resistance
            case 30328: // Alacrity
                return true;
            default: return false;
        }
    }

    bool is_condition(uint32_t id) {
        switch (id) {
            case   720: // Blind
            case   721: // Crippled
            case   722: // Chilled
            case   723: // Poison
            case   727: // Immobile
            case   736: // Bleeding
            case   737: // Burning
            case   738: // Vulnerability
            case   742: // Weakness
            case   791: // Fear
            case   861: // Confusion
            case 19426: // Torment
            case 26766: // Slow
            case 27705: // Taunt
                return true;
            default: return false;
        }
    }

    uint64_t compute_display_ms(const AgentState& s) {
        uint64_t live = s.in_combat_wall
            ? (wall_now() > *s.in_combat_wall ? wall_now() - *s.in_combat_wall : 0)
            : 0;
        return s.accumulated_ms + live;
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
}

void Tracker::on_combat_local(cbtevent* /*ev*/, ag* /*src*/, ag* /*dst*/,
                              const char* /*skillname*/, uint64_t /*id*/, uint64_t /*revision*/) {
    // combat_local is a strict duplicate of combat for self events — arc
    // fires the same payload on both callbacks. Processing here would
    // double-count damage and combat time. Do nothing.
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
        case CBTS_LOGSTART:
            reset_fight();
            break;
        case CBTS_LOGEND:
            for (auto& [id, s] : agents_) {
                if (s.in_combat_wall) {
                    uint64_t w = wall_now();
                    if (w > *s.in_combat_wall) s.accumulated_ms += w - *s.in_combat_wall;
                    s.in_combat_wall.reset();
                }
            }
            any_in_combat_ = false;
            break;
        default:
            break;
    }
    (void)dst;
}

void Tracker::enter_combat(ag* src, uint64_t time) {
    if (!src || !src->id) return;
    auto* s = touch_agent(src);
    if (!s) return;

    if (!s->in_combat_wall) {
        // Per-self fight reset: squadmate combat state does not dilute
        // this player's stats.
        if (s->accumulated_ms > 0 || s->damage_total > 0 ||
            s->strip_count > 0 || s->cleanse_count > 0) {
            s->accumulated_ms = 0;
            s->damage_total   = 0;
            s->first_damage_wall = 0;
            s->last_damage_wall  = 0;
            s->strip_count    = 0;
            s->cleanse_count  = 0;
            s->skills.clear();
            s->history.clear();
        }
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
        auto& names = skill_names();
        auto it = names.find(ev->skillid);
        if (it == names.end()) names.emplace(ev->skillid, skillname);
    }

    // Strips/cleanses run before the iff==0 filter since cleanses are
    // friend-on-friend events that the damage path drops.
    if (ev->buff != 0 && ev->is_buffremove != 0 && src->id) {
        auto it = agents_.find(src->id);
        if (it != agents_.end() && it->second.is_player) {
            bool filtered = false;
            if (dst) {
                bool dst_is_player = dst->elite != 0xFFFFFFFFu;
                bool dst_is_npc    = !dst_is_player &&
                                     (dst->prof & 0xFFFF0000u) == 0xFFFF0000u;
                bool dst_is_gadget = !dst_is_player && !dst_is_npc;
                if (options().exclude_gadgets.load(std::memory_order_relaxed) && dst_is_gadget) filtered = true;
                if (options().exclude_npcs.load(std::memory_order_relaxed) && dst_is_npc) filtered = true;
            }
            if (!filtered) {
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

    // Target-type classification per deltaconnected evtc README:
    //   Player: elite != 0xFFFFFFFF
    //   NPC:    elite == 0xFFFFFFFF AND (prof & 0xFFFF0000) == 0xFFFF0000
    //   Gadget: elite == 0xFFFFFFFF AND (prof & 0xFFFF0000) != 0xFFFF0000
    if (dst) {
        bool dst_is_player = dst->elite != 0xFFFFFFFFu;
        bool dst_is_npc    = !dst_is_player &&
                             (dst->prof & 0xFFFF0000u) == 0xFFFF0000u;
        bool dst_is_gadget = !dst_is_player && !dst_is_npc;
        if (options().exclude_gadgets.load(std::memory_order_relaxed) && dst_is_gadget) return;
        if (options().exclude_npcs.load(std::memory_order_relaxed) && dst_is_npc) return;
    }

    // Lazy-register self on first damage — in solo play arc may never fire
    // a tracking-add for the local player on `combat`, only on `combat_local`
    // damage events where src->self == 1.
    if (src->self && src->id) {
        auto& s = agents_[src->id];
        if (s.id == 0) {
            s.id        = src->id;
            s.name      = src->name ? src->name : "";
            s.prof      = src->prof;
            s.elite     = src->elite;
            s.is_self   = true;
            s.is_player = true;
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
    // Strict fight boundary: only count damage while arc has flagged the
    // owner in combat. Prevents stray OOC ticks from resetting the fight
    // via implicit-enter.
    if (!owner->in_combat_wall) return;

    uint64_t now = wall_now();
    if (owner->first_damage_wall == 0) owner->first_damage_wall = now;
    owner->last_damage_wall = now;
    owner->damage_total += delta;

    auto& sk = owner->skills[ev->skillid];
    sk.damage += delta;
    sk.hits   += 1;

    // Sample cumulative damage every ~500ms for the detail-window graph.
    if (owner->history.empty() ||
        now - owner->history.back().wall_ms >= 500) {
        owner->history.push_back({now, owner->damage_total});
        if (owner->history.size() > 4096) {
            owner->history.erase(owner->history.begin());
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
    if (it == instid_to_id_.end()) return nullptr;
    auto sit = agents_.find(it->second);
    if (sit == agents_.end()) return nullptr;
    return &sit->second;
}

void Tracker::reset_fight() {
    for (auto& [id, s] : agents_) {
        s.accumulated_ms = 0;
        s.damage_total   = 0;
        s.in_combat_wall.reset();
        s.first_damage_wall = 0;
        s.last_damage_wall  = 0;
        s.strip_count      = 0;
        s.cleanse_count    = 0;
        s.skills.clear();
        s.history.clear();
    }
    any_in_combat_ = false;
}

AgentDetail Tracker::detail(uintptr_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    AgentDetail d{};
    auto it = agents_.find(id);
    if (it == agents_.end()) return d;
    const auto& s = it->second;
    d.name  = s.name;
    d.prof  = s.prof;
    d.elite = s.elite;
    d.history = s.history;
    d.history_start_wall = s.first_damage_wall;
    d.skills.reserve(s.skills.size());
    const auto& names = skill_names();
    for (const auto& [sid, entry] : s.skills) {
        SkillDetail sd;
        sd.skill_id = sid;
        auto nit = names.find(sid);
        sd.name    = nit != names.end() ? nit->second : std::string();
        sd.damage  = entry.damage;
        sd.hits    = entry.hits;
        d.skills.push_back(std::move(sd));
    }
    std::sort(d.skills.begin(), d.skills.end(),
        [](const SkillDetail& a, const SkillDetail& b) { return a.damage > b.damage; });
    return d;
}

std::vector<Snapshot> Tracker::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Snapshot> out;
    out.reserve(agents_.size());
    for (const auto& [id, s] : agents_) {
        if (!s.is_player) continue;
        // Combat time = active-damage window (first -> last damage event),
        // extending to now while the agent is still in combat. Matches
        // arc's Damage panel denominator.
        uint64_t ms = 0;
        if (s.first_damage_wall != 0) {
            uint64_t end = s.in_combat_wall ? wall_now() : s.last_damage_wall;
            if (end > s.first_damage_wall) ms = end - s.first_damage_wall;
        }
        uint64_t denom = ms < 1000 ? 1000 : ms;
        uint64_t dps = s.damage_total * 1000ull / denom;
        out.push_back(Snapshot{
            s.id, s.name, s.prof, s.elite,
            ms, s.damage_total, dps,
            s.strip_count, s.cleanse_count,
            s.in_combat_wall.has_value(),
        });
    }
    return out;
}

} // namespace idps
