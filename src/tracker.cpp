#include "tracker.h"

#include <algorithm>
#include <cstring>
#include <ctime>
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

    // Sorted ascending for binary_search.
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

    // Sorted ascending for binary_search.
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
        // arcdps populates dst->self for the local player's tracking-add.
        if (dst && dst->self) return true;
        // Remote player tracking-add: dst->name is an account starting with
        // ':' (e.g. ":Troy.4370"). Minion/effect events have null dst.
        if (dst && dst->name && dst->name[0] == ':') return true;
        return false;
    }

    // For state-only events (no dst). Player profs are 1..9 and elite !=
    // 0xFFFFFFFF (which marks NPCs).
    bool prof_looks_like_player(const ag* a) {
        if (!a) return false;
        return a->prof >= 1 && a->prof <= 9 && a->elite != 0xFFFFFFFFu;
    }

    // Target-type classification per deltaconnected evtc README:
    //   Player: elite != 0xFFFFFFFF
    //   Gadget: elite == 0xFFFFFFFF AND (prof & 0xFFFF0000) == 0xFFFF0000
    //   NPC:    elite == 0xFFFFFFFF AND (prof & 0xFFFF0000) != 0xFFFF0000
    enum class TargetType { Player, Npc, Gadget };

    TargetType classify_target(const ag* dst) {
        if (dst->elite != 0xFFFFFFFFu) return TargetType::Player;
        if ((dst->prof & 0xFFFF0000u) == 0xFFFF0000u) return TargetType::Gadget;
        return TargetType::Npc;
    }

    // On tracking-add arc puts player class info in dst, not src.
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
               s.strip_count > 0 || s.cleanse_count > 0 ||
               s.damage_to_downed > 0 || s.downs_contributed > 0 ||
               s.kills_contributed > 0 || s.interrupts > 0 ||
               s.cc_count > 0;
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
        s.kills_contributed   = 0;
        s.interrupts          = 0;
        s.cc_count            = 0;
        s.cc_duration_ms      = 0;
        s.alive               = true;
        s.skills.clear();
        s.history.clear();
    }

    // Build the UI-facing row for an agent. Centralised so the live and the
    // two past-fight readouts can never drift apart as Snapshot grows.
    Snapshot make_snapshot(const AgentState& s, uint64_t combat_ms,
                           bool in_combat) {
        // 500ms floor on the denominator — the first hit reads as
        // damage / 500ms, then settles to the running average.
        uint64_t denom = combat_ms < 500 ? 500 : combat_ms;
        Snapshot out;
        out.id                = s.id;
        out.name              = s.name;
        out.account           = s.account;
        out.prof              = s.prof;
        out.elite             = s.elite;
        out.subgroup          = s.subgroup;
        out.combat_ms         = combat_ms;
        out.damage_total      = s.damage_total;
        out.dps               = s.damage_total * 1000ull / denom;
        out.strip_count       = s.strip_count;
        out.cleanse_count     = s.cleanse_count;
        out.damage_to_downed  = s.damage_to_downed;
        out.downs_contributed = s.downs_contributed;
        out.kills_contributed = s.kills_contributed;
        out.interrupts        = s.interrupts;
        out.cc_count          = s.cc_count;
        out.cc_duration_ms    = s.cc_duration_ms;
        out.in_combat         = in_combat;
        out.is_self           = s.is_self;
        return out;
    }

    // Damage window for a stored fight: first credited hit to last.
    uint64_t stored_combat_ms(const AgentState& s) {
        if (s.first_damage_wall != 0 && s.last_damage_wall > s.first_damage_wall)
            return s.last_damage_wall - s.first_damage_wall;
        return 0;
    }
}

// Log-target species ids, transcribed from the evtc README's `characters[]`
// table (the npcs arcdps starts logs on). Only the ids the README names are
// listed; anything else falls through to nullptr and the UI shows no
// encounter label rather than a wrong one.
const char* boss_name_for(uint32_t id) {
    switch (id) {
        // Raids
        case 15438: return "Vale Guardian";
        case 15429: return "Gorseval";
        case 15375: return "Sabetha";
        case 16123: return "Slothasor";
        case 16088: return "Bandit Trio";
        case 16115: return "Matthias";
        case 16253: return "Escort";
        case 16235: return "Keep Construct";
        case 16247: return "Twisted Castle";
        case 16246: return "Xera";
        case 17194: return "Cairn";
        case 17172: return "Mursaat Overseer";
        case 17188: return "Samarog";
        case 17154: return "Deimos";
        case 19767: return "Soulless Horror";
        case 19828: return "River of Souls";
        case 19691: return "Broken King";
        case 19536: return "Eater of Souls";
        case 19844: return "Eye of Fate";
        case 19450: return "Dhuum";
        case 21105: return "Nikare";
        case 20934: return "Qadim";
        case 21964: return "Sabir";
        case 22006: return "Adina";
        case 22000: return "Qadim the Peerless";
        case 26725: return "Greer";
        case 26774: return "Decima";
        case 26867: return "Decima CM";
        case 26712: return "Ura";
        // Special
        case 21333: return "Freezie";
        // Fractals
        case 17021: return "MAMA";
        case 17028: return "Siax";
        case 16948: return "Ensolyss";
        case 17632: return "Skorvald";
        case 17949: return "Artsariiv";
        case 17759: return "Arkk";
        case 23254: return "Sorrowful Spellcaster";
        case 25577: return "Kanaxai";
        case 26231: return "Eparch";
        case 27010: return "Whispering Shadow";
        // Strike missions
        case 22154: return "Icebrood Construct";
        case 22343: return "Kodan Brothers";
        case 22492: return "Fraenir of Jormag";
        case 22521: return "Boneskinner";
        case 22711: return "Whisper of Jormag";
        case 24033: return "Mai Trin";
        case 23957: return "Ankka";
        case 24485: return "Minister Li";
        case 24266: return "Minister Li CM";
        case 25413: return "Old Lion's Court";
        case 25414: return "Old Lion's Court CM";
        case 25705: return "Dagda";
        case 25989: return "Cerus";
        // Golems / benchmark
        case 16199: case 16177: case 16198: case 16178: case 16202:
        case 16169: case 16176: case 16174: case 19645: case 19676:
            return "Training Golem";
        default: return nullptr;
    }
}

void Tracker::on_combat(cbtevent* ev, ag* src, ag* dst,
                        const char* skillname, uint64_t /*id*/, uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ev) {
        if (src && src->id) {
            // arc api README: ev==null with src->prof truthy = tracking-add;
            // src->prof == 0 = tracking-remove. (src->elite == 1 is a
            // target-change for the active target window — not used here.)
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
                s.last_seen_wall = wall_now();
                // Account name ("Account.1234") arrives in dst->name on a
                // player tracking-add, arc-prefixed with ':'. Strip it. Only
                // assign when present so a later add lacking it (some self /
                // lazy-register paths pass the target in dst) can't wipe a
                // known account.
                if (dst && dst->name && dst->name[0] == ':' && dst->name[1])
                    s.account = dst->name + 1;
                // Per the api README's tracking-add layout: src->team is the
                // world/team id, dst->team the squad subgroup. Only assign
                // when non-zero — arc sends 0 for "not in a subgroup", and a
                // later add that omits it must not erase a known value.
                if (src->team != 0) s.team = src->team;
                if (dst && dst->team != 0) s.subgroup = dst->team;
                if (dst && dst->id) {
                    uint16_t new_instid = static_cast<uint16_t>(dst->id & 0xFFFFu);
                    auto existing = instid_to_id_.find(new_instid);
                    if (existing != instid_to_id_.end() &&
                        existing->second != src->id) {
                        log_line("tracker: instid %u rebinding from agent "
                                 "%llx to %llx",
                                 static_cast<unsigned>(new_instid),
                                 static_cast<unsigned long long>(existing->second),
                                 static_cast<unsigned long long>(src->id));
                        // Clear the prior owner's instid field. Without this,
                        // that agent's later tracking-remove erases the map
                        // via its stale instid (== new_instid) and drops THIS
                        // agent's fresh mapping; the find_by_instid linear
                        // fallback could likewise still match the old binding.
                        // find() does not insert, so the `s` reference above
                        // stays valid.
                        auto prev = agents_.find(existing->second);
                        if (prev != agents_.end() &&
                            prev->second.instid == new_instid) {
                            prev->second.instid = 0;
                        }
                    }
                    s.instid = new_instid;
                    instid_to_id_[new_instid] = src->id;
                }
            } else if (src->prof == 0) {
                // Tracking-remove. Keep accumulated stats until fight reset
                // so the user can still see their row, but drop the instid
                // mapping immediately so a new instid can rebind to a
                // different agent without aliasing. The per-agent instid
                // field must clear too: find_by_instid's linear fallback
                // scans agents_ directly, so a removed agent holding a
                // recycled instid would otherwise soak up a new player's
                // pet/minion damage arriving before their tracking-add.
                auto it = agents_.find(src->id);
                if (it != agents_.end()) {
                    instid_to_id_.erase(it->second.instid);
                    it->second.instid  = 0;
                    it->second.present = false;
                }
            }
            // prof != 0 && elite == 0xFFFFFFFFu is an NPC/gadget
            // tracking-add — intentionally ignored, tracker only carries
            // players in agents_.
        }
        return;
    }

    // Arc doesn't always re-fire tracking-add after a build/spec swap, but
    // damage and state-change events usually carry current prof/elite.
    // Same loop touches last_seen_wall so the staleness filter in
    // snapshot() knows the agent is still receiving events.
    uint64_t now_seen = wall_now();
    if (src && src->id) {
        auto it = agents_.find(src->id);
        if (it != agents_.end()) {
            it->second.last_seen_wall = now_seen;
            if (it->second.is_player && src->prof >= 1 && src->prof <= 9) {
                it->second.prof  = src->prof;
                it->second.elite = src->elite;
            }
        }
    }
    if (dst && dst->id) {
        auto it = agents_.find(dst->id);
        if (it != agents_.end()) {
            it->second.last_seen_wall = now_seen;
        }
    }

    // Buff removal (cleanse / strip) is delivered, per the evtc spec, as a
    // NORMAL combat event (is_statechange == CBTS_COMBAT) carrying
    // is_buffremove != 0 — src lost the buff, dst removed it. It must be
    // intercepted here: on_damage would drop it (buff set, no buff_dmg) and
    // the strip/cleanse counters would barely move. Some arc builds also
    // surface the dedicated CBTS_BUFFREMOVE_ALL/SINGLE statechanges; handle
    // both forms in one place.
    // The in-band form additionally requires ev->buff ("skill is a buff")
    // and no buff damage. The current evtc README documents is_buffremove
    // only under the dedicated CBTS_BUFFREMOVE_* statechanges, so on a plain
    // CBTS_COMBAT event that byte is not guaranteed to mean anything —
    // without these two guards a condition tick carrying a stray value could
    // be read as a removal and counted as a cleanse.
    bool is_buff_remove =
        (ev->is_statechange == CBTS_COMBAT && ev->is_buffremove != 0 &&
         ev->buff != 0 && ev->buff_dmg == 0) ||
        ev->is_statechange == CBTS_BUFFREMOVE_ALL ||
        ev->is_statechange == CBTS_BUFFREMOVE_SINGLE;
    if (is_buff_remove) {
        on_buff_remove(ev, src, dst);
        return;
    }

    if (ev->is_statechange != CBTS_COMBAT) {
        on_statechange(ev, src, dst);
        return;
    }

    on_damage(ev, src, dst, skillname, revision);
}

void Tracker::on_statechange(cbtevent* ev, ag* src, ag* dst) {
    switch (ev->is_statechange) {
        case CBTS_ENTERCOMBAT:
            enter_combat(ev, src);
            break;
        case CBTS_EXITCOMBAT:
            exit_combat(ev, src);
            break;
        case CBTS_CHANGEUP:
            // Agent is alive again — a rally out of downstate, or a revive.
            // Clearing the per-target down state matters: without it the
            // sticky downed_ flag makes the next down on this target look
            // like a continuation and no one is credited for it.
            if (src && src->id) {
                auto it = agents_.find(src->id);
                if (it != agents_.end()) it->second.alive = true;
                downed_.erase(src->id);
                last_attacker_.erase(src->id);
                target_dmg_.erase(src->id);
            }
            break;
        case CBTS_TEAMCHANGE:
            // dst_agent is the new team id. Relevant in WvW, where a player
            // can flip sides across a borderland transfer.
            if (src && src->id) {
                auto it = agents_.find(src->id);
                if (it != agents_.end())
                    it->second.team = static_cast<uint16_t>(ev->dst_agent & 0xFFFFu);
            }
            break;
        case CBTS_LOGNPCUPDATE:
            // src_agent is the species id of the encounter arc is logging.
            // Recorded so the closed fight can be labelled in history.
            current_boss_species_ = static_cast<uint32_t>(ev->src_agent & 0xFFFFFFFFu);
            break;
        case CBTS_CHANGEDEAD:
            force_exit(src);
            if (src && src->id) {
                auto it = agents_.find(src->id);
                if (it != agents_.end()) it->second.alive = false;
                // NPCs die without going through downstate; without this
                // drop, target_dmg_ leaks across long WvW sessions where
                // reset_fight() never runs.
                target_dmg_.erase(src->id);
                downed_.erase(src->id);
                last_attacker_.erase(src->id);
            }
            break;
        case CBTS_CHANGEDOWN:
            // Drains any pending per-attacker damage into damage_to_downed
            // for the rare squad-self-down case (friendly fire) where
            // on_damage did not already drain it. Does NOT credit a down:
            // CHANGEDOWN's src_agent is the downed agent only — the evtc
            // feed carries no finisher identity here. downs_contributed is
            // credited solely to the CBTR_DOWNED hit's attacker in on_damage.
            if (src && src->id) {
                auto tit = target_dmg_.find(src->id);
                if (tit != target_dmg_.end()) {
                    for (const auto& [aid, dmg] : tit->second) {
                        auto ait = agents_.find(aid);
                        if (ait != agents_.end()) {
                            ait->second.damage_to_downed += dmg;
                        }
                    }
                    target_dmg_.erase(tit);
                }
            }
            break;
        case CBTS_SQCOMBATSTART:
            // Per-self semantics: do not wipe squadmates here. Each player
            // resets via their own CBTS_ENTERCOMBAT (or implicit-enter on
            // first credited damage), so a squadmate's pull cannot reset
            // rows for people not yet in combat.
            in_encounter_ = true;
            // A new encounter supersedes whatever log target was in effect;
            // arc re-sends CBTS_LOGNPCUPDATE for this one.
            current_boss_species_ = 0;
            sq_end_clock_         = 0;
            break;
        case CBTS_MAPCHANGE:
            // Realtime signal that the local player changed maps. Squadmates
            // from the previous map are no longer in range and arc won't
            // re-fire tracking-remove for them. Mark all non-self agents
            // absent so snapshot() drops them next frame instead of waiting
            // for the 60s staleness fallback. push_to_history captures any
            // partial fight in progress and (importantly) clears target_dmg_
            // / downed_ so leftover attribution can't bleed into the next
            // map's first pull.
            push_to_history();
            for (auto& [id, s] : agents_) {
                // Close every open combat clock, self included — a map move
                // always drops combat in GW2. Leaving self's clock open here
                // (while any_in_combat_ / current_fight_start_wall_ reset)
                // made the next map's first fight skip implicit-enter, so it
                // never opened a history window: the fight went unrecorded
                // and push_to_history's early-return kept stale target_dmg_
                // alive into the new map's down counts.
                if (s.in_combat_wall) {
                    uint64_t w = wall_now();
                    if (w > *s.in_combat_wall)
                        s.accumulated_ms += w - *s.in_combat_wall;
                    s.in_combat_wall.reset();
                }
                s.fight_armed = false;
                if (s.is_self) continue;
                s.present = false;
                // Clear instid alongside the instid_to_id_ wipe below so the
                // find_by_instid linear fallback can't attribute next-map
                // damage to a now-absent prior-map squadmate once instids
                // recycle. Self keeps its instid (re-seeded lazily on its
                // next damage event).
                s.instid = 0;
            }
            instid_to_id_.clear();
            any_in_combat_ = false;
            in_encounter_  = false;
            break;
        case CBTS_SQCOMBATEND:
            // buff_dmg carries the local unix timestamp of the close (value
            // carries the server's). Using arc's own stamp makes the history
            // clock exact instead of a back-dated estimate from wall ticks.
            sq_end_clock_ = static_cast<uint32_t>(ev->buff_dmg);
            for (auto& [id, s] : agents_) {
                if (s.in_combat_wall) {
                    uint64_t w = wall_now();
                    if (w > *s.in_combat_wall) s.accumulated_ms += w - *s.in_combat_wall;
                    s.in_combat_wall.reset();
                }
                s.fight_armed = false;
            }
            any_in_combat_ = false;
            in_encounter_ = false;
            // Capture the just-closed fight to history. Fires for instance
            // content (raids/strikes) where SQCOMBATEND is reliable. WvW
            // doesn't fire it; manual Reset is the path there.
            push_to_history();
            break;
        default:
            break;
    }
    (void)dst;
}

void Tracker::on_buff_remove(cbtevent* ev, ag* src, ag* dst) {
    // Count one per server-authoritative removal: CBTB_ALL (last/all stacks
    // of a buff cleared) or CBTB_SINGLE (one stack). Skip CBTB_MANUAL — arc
    // synthesizes one of those per stack on an all-remove, so counting it
    // would multiply the tally by the stack size.
    //
    // The kind lives in is_buffremove in both delivery forms: the classic
    // in-band one (is_statechange == CBTS_COMBAT) and the dedicated
    // CBTS_BUFFREMOVE_* statechanges, which the evtc README documents as
    // also carrying "is_buffremove: of enum cbtbuffremove". Reading the
    // statechange alone would accept arc's synthesized CBTB_MANUAL events,
    // which arrive as CBTS_BUFFREMOVE_SINGLE — so gate on the field.
    uint8_t kind = ev->is_buffremove;
    // Should a build leave the field unset on a dedicated statechange, the
    // statechange names the kind itself and is taken as authoritative. This
    // never resurrects CBTB_MANUAL, which is an explicit non-zero value.
    if (kind == CBTB_NONE) {
        if (ev->is_statechange == CBTS_BUFFREMOVE_ALL)         kind = CBTB_ALL;
        else if (ev->is_statechange == CBTS_BUFFREMOVE_SINGLE) kind = CBTB_SINGLE;
    }
    bool is_all    = kind == CBTB_ALL;
    bool is_single = kind == CBTB_SINGLE;
    if (!is_all && !is_single) return;

    // The server sends a removal event when a buff simply RUNS OUT, not only
    // when a skill takes it off. Both look identical apart from one field:
    // the evtc README documents `value` as "ms duration removed" (and, on an
    // all-remove, `buff_dmg` as the same figure computed as intensity, which
    // is the meaningful one for intensity-stacking conditions like Bleeding
    // or Vulnerability). A natural expiry removes nothing — the buff already
    // ran to zero — so that figure is ~0. A real cleanse or strip takes
    // measurable remaining duration off.
    //
    // Without this, every Bleeding, Cripple and Vulnerability that ticked out
    // on a squad member scored that member a cleanse, which is what made the
    // numbers read high. Strips were affected far less, because a foe's boons
    // expiring have the foe as both source and remover, and a foe is not a
    // tracked player — so those were already being dropped below.
    //
    // The floor absorbs server tick granularity (~40ms); a cleanse of a
    // condition with under 50ms left is worth nothing anyway.
    constexpr int32_t kMinRemovedMs = 50;
    int32_t removed_ms = ev->value > ev->buff_dmg ? ev->value : ev->buff_dmg;
    if (removed_ms < kMinRemovedMs) return;

    // src = agent that lost the buff, dst = remover (the player we credit).
    if (!dst || !dst->id) return;
    auto it = agents_.find(dst->id);
    if (it == agents_.end() || !it->second.is_player) return;

    if (src) {
        auto tt = classify_target(src);
        if (options().exclude_gadgets.load(std::memory_order_relaxed) &&
            tt == TargetType::Gadget) return;
        if (options().exclude_npcs.load(std::memory_order_relaxed) &&
            tt == TargetType::Npc) return;
    }

    // iff is from src's (the buff holder's) perspective: a stripped foe
    // sees the remover as IFF_FOE; a cleansed ally sees IFF_FRIEND. This
    // also splits boon-vs-condition correctly — a removed boon on a friend
    // (e.g. a boon-rip on an ally) or a condition on a foe won't be counted.
    bool is_strip   = ev->iff == IFF_FOE    && is_boon(ev->skillid);
    bool is_cleanse = ev->iff == IFF_FRIEND && is_condition(ev->skillid);
    if (!is_strip && !is_cleanse) return;

    auto& agent = it->second;
    // Support actions count as fight-starting activity: a healer opening
    // with a cleanse must reset their armed row first, or the new fight's
    // support piles onto the previous fight's stats.
    if (agent.fight_armed) {
        reset_for_new_fight(agent);
        agent.fight_armed = false;
    }
    if (is_strip) agent.strip_count++;
    else          agent.cleanse_count++;
    agent.last_activity_wall = wall_now();
}

// CBTS_ENTERCOMBAT / EXITCOMBAT carry the agent's identity in the event
// body rather than in `ag`: value = prof id, buff_dmg = elite spec id,
// dst_agent = subgroup. Apply whatever of that is present.
static void apply_combat_identity(AgentState& s, const cbtevent* ev) {
    uint32_t prof  = static_cast<uint32_t>(ev->value);
    uint32_t elite = static_cast<uint32_t>(ev->buff_dmg);
    if (prof >= 1 && prof <= 9) {
        s.prof  = prof;
        s.elite = elite;
    }
    uint16_t sub = static_cast<uint16_t>(ev->dst_agent & 0xFFFFu);
    if (sub != 0 && sub <= 15) s.subgroup = sub;
}

void Tracker::enter_combat(cbtevent* ev, ag* src) {
    if (!src || !src->id) return;
    auto* s = touch_agent(src);
    if (!s) {
        // Squadmate ENTERCOMBAT before tracking-add — lazy-register. `ag`
        // is not populated with prof/elite for state changes, so the
        // event body is the authoritative source here.
        uint32_t ev_prof = static_cast<uint32_t>(ev->value);
        if (prof_looks_like_player(src) || (ev_prof >= 1 && ev_prof <= 9)) {
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
    apply_combat_identity(*s, ev);

    if (!s->in_combat_wall) {
        uint64_t now = wall_now();
        if (options().fight_gap_enabled.load(std::memory_order_relaxed)) {
            // Smart boundaries: within the gap of the last action this is
            // a combat-drop blip — resume the same fight untouched. Beyond
            // it, arm a deferred reset: the row keeps its previous-fight
            // stats until the first action of the new fight, so passive
            // combat entry (NPC aggro) never wipes anything.
            uint64_t gap = options().fight_gap_ms.load(std::memory_order_relaxed);
            bool recent = s->last_activity_wall != 0 &&
                          now - s->last_activity_wall <= gap;
            if (!recent && has_fight_state(*s)) s->fight_armed = true;
        } else {
            // Legacy: per-self reset at combat entry.
            if (has_fight_state(*s)) reset_for_new_fight(*s);
        }
        s->in_combat_wall = now;
    }

    any_in_combat_ = std::any_of(agents_.begin(), agents_.end(),
        [](const auto& p) { return p.second.in_combat_wall.has_value(); });
    if (any_in_combat_ && current_fight_start_wall_ == 0) {
        current_fight_start_wall_ = wall_now();
    }
}

void Tracker::exit_combat(cbtevent* ev, ag* src) {
    if (!src || !src->id) return;
    auto it = agents_.find(src->id);
    if (it == agents_.end()) return;
    auto& s = it->second;
    // EXITCOMBAT carries the same identity payload as ENTERCOMBAT; a build
    // swap made while out of combat surfaces here first.
    if (ev) apply_combat_identity(s, ev);
    if (s.in_combat_wall) {
        uint64_t w = wall_now();
        if (w > *s.in_combat_wall) s.accumulated_ms += w - *s.in_combat_wall;
        s.in_combat_wall.reset();
    }
    // Armed but exited without acting — the "fight" never started. Disarm
    // so the untouched stats simply remain on the row.
    s.fight_armed = false;
    any_in_combat_ = std::any_of(agents_.begin(), agents_.end(),
        [](const auto& p) { return p.second.in_combat_wall.has_value(); });
}

void Tracker::force_exit(ag* src) {
    exit_combat(nullptr, src);
}

void Tracker::on_damage(cbtevent* ev, ag* src, ag* dst,
                        const char* skillname, uint64_t revision) {
    if (!src) return;

    if (skillname && *skillname && ev->skillid != 0) {
        auto it = skill_names_.find(ev->skillid);
        if (it == skill_names_.end()) skill_names_.emplace(ev->skillid, skillname);
    }

    if (ev->iff == IFF_FRIEND) return;

    if (dst) {
        auto tt = classify_target(dst);
        if (options().exclude_gadgets.load(std::memory_order_relaxed) &&
            tt == TargetType::Gadget) return;
        if (options().exclude_npcs.load(std::memory_order_relaxed) &&
            tt == TargetType::Npc) return;
    }

    // Lazy-register self on first damage — in solo play arc may never fire
    // tracking-add on `combat`, only on `combat_local` damage events with
    // src->self == 1. Seeds the instid map so pet/minion attribution via
    // src_master_instid resolves on the first hit. Prof-guarded so a
    // self-marked NPC (boss-disguise, gizmo, mount) cannot create a
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
        // Strike. Whitelist real HP damage results. arc's panel excludes
        // defiance bar damage (CBTR_DEFIANCE_DAMAGENORMAL), skillcast
        // metadata (CBTR_SKILLCAST), CC duration in ms (CBTR_CROWDCONTROL
        // — surfaces as the "Generic Lockout" skill row), and invert
        // (CBTR_INVERT) because `value` carries non-HP payloads there.
        // Also drops negatives from arc's post-2026-04-14 feed which
        // encodes barrier-absorbed / invulnerable hits as < 0.
        switch (ev->result) {
            case CBTR_STRIKE_DAMAGENORMAL:
            case CBTR_STRIKE_DAMAGECRIT:
            case CBTR_STRIKE_DAMAGEGLANCE:
            case CBTR_KILLINGBLOW:
            case CBTR_DOWNED:
                if (ev->value > 0) delta = static_cast<uint64_t>(ev->value);
                break;
            default:
                break;
        }
    } else {
        // Condition tick. Skip buff-application/removal events.
        if (ev->buff_dmg > 0 && ev->is_buffremove == 0) {
            delta = static_cast<uint64_t>(ev->buff_dmg);
        }
        (void)revision;
    }

    // Four event kinds must survive the delta gate below, because all of
    // them routinely carry no damage while still being real contributions.
    // Each is credited at the very end of this function, AFTER fight-boundary
    // management, so it lands on the correct fight and a deferred or implicit
    // reset cannot wipe it — a killing blow in particular is commonly a
    // player's first action after a gap, running up to finish a downed foe.
    //
    // A killing blow frequently arrives with value <= 0: overkill past 0 HP,
    // a barrier-absorbed lethal hit, or the post-2026-04-14 feed's negative
    // encodings. Enemy players only; NPC/gadget deaths flow through
    // CBTS_CHANGEDEAD.
    bool is_killing_blow = ev->buff == 0 && ev->result == CBTR_KILLINGBLOW &&
                           dst && dst->id && dst->elite != 0xFFFFFFFFu;

    // The hit that puts a target into downstate is likewise usually the one
    // that overkills their remaining health, so `value` lands at or below
    // zero and the event would be dropped before the down state machine ever
    // saw it. The is_offcycle discovery path recovers some of these, but only
    // when a later event happens to observe the target; letting the explicit
    // CBTR_DOWNED through credits the finisher directly.
    bool is_downing_blow = ev->result == CBTR_DOWNED &&
                           dst && dst->id && dst->elite != 0xFFFFFFFFu;

    // Control events carry no damage at all: CBTR_INTERRUPT reports a
    // cancelled action, CBTR_CROWDCONTROL puts the applied disable duration
    // (ms) in `value`. Both are contributions worth attributing, and both
    // are invisible to a damage-only gate.
    bool is_interrupt = ev->result == CBTR_INTERRUPT;
    bool is_cc        = ev->result == CBTR_CROWDCONTROL;

    bool credited_nondamage = is_killing_blow || is_downing_blow ||
                              is_interrupt || is_cc;
    if (delta == 0 && !credited_nondamage) return;

    uint64_t now = wall_now();
    bool     gap_enabled = options().fight_gap_enabled.load(std::memory_order_relaxed);
    uint64_t gap_ms      = gap_enabled
                         ? options().fight_gap_ms.load(std::memory_order_relaxed)
                         : 5000;
    // Deferred reset armed at ENTERCOMBAT fires on the fight's first
    // credited damage — this is where the new fight actually begins.
    if (owner->fight_armed) {
        reset_for_new_fight(*owner);
        owner->fight_armed = false;
        if (!owner->in_combat_wall) owner->in_combat_wall = now;
    } else if (!owner->in_combat_wall) {
        // Implicit-enter when arc skipped CBTS_ENTERCOMBAT. Cold-start
        // requires (a) self as literal source — pets/minions have
        // src->self == 0 even when attributed to the master, so their OOC
        // autos cannot restart a fight; and (b) a strike (ev->buff == 0) —
        // condi tail ticks landing after EXITCOMBAT would otherwise look
        // like a fresh fight. Pets/condi may still enter for their owner
        // if the squad is already fighting.
        bool from_self = src->self != 0;
        bool is_strike = ev->buff == 0;
        bool can_start = from_self && is_strike;
        if (!can_start && !any_in_combat_) return;
        // Same resume rule as enter_combat: action within the gap of the
        // last action continues the previous fight instead of resetting.
        bool recent = gap_enabled && owner->last_activity_wall != 0 &&
                      now - owner->last_activity_wall <= gap_ms;
        if (!recent && has_fight_state(*owner)) reset_for_new_fight(*owner);
        owner->in_combat_wall = now;
        bool was_idle = !any_in_combat_;
        any_in_combat_ = true;
        if (was_idle && current_fight_start_wall_ == 0) {
            current_fight_start_wall_ = now;
        }
    } else if (!in_encounter_ && owner->last_damage_wall != 0) {
        // GW2 keeps the player in combat for ~4s after the last hit. If arc
        // never sent EXITCOMBAT, treat a long idle gap as a fight boundary
        // so old damage doesn't leak into the next pull.
        uint64_t idle = now > owner->last_damage_wall
                      ? now - owner->last_damage_wall : 0;
        if (idle > gap_ms) {
            reset_for_new_fight(*owner);
            owner->in_combat_wall = now;
            // Idle gap is a fight boundary for this owner. Drop their
            // pending per-target attribution so damage from the previous
            // skirmish can't be credited into the next one's downs. Other
            // owners may still be mid-fight on these targets, so erase only
            // this owner's entries, not the whole map. (current_fight_start_
            // wall_ is left as-is — it's the global history clock, refreshed
            // by push_to_history; in WvW a multi-skirmish history entry can
            // therefore read long, an accepted limitation without a clean
            // realtime fight-boundary signal.)
            for (auto& target : target_dmg_) {
                target.second.erase(owner->id);
            }
            // Drop stale finisher candidacy too: target_dmg_ and
            // last_attacker_ are documented to populate together, and a
            // surviving last_attacker_ entry would credit this owner for a
            // discovery-down in the NEXT skirmish off pre-idle damage.
            for (auto la = last_attacker_.begin();
                 la != last_attacker_.end(); ) {
                if (la->second == owner->id) la = last_attacker_.erase(la);
                else                         ++la;
            }
        }
    }

    // Activity + fight-window bookkeeping applies to every credited event,
    // including a zero-damage killing blow — so the kill opens a history
    // window and counts as fight-starting activity.
    owner->last_activity_wall = now;
    // Defensive: any credited event opens the history window when none is
    // open. Belt-and-suspenders for paths where an agent's combat clock
    // survived a global reset and implicit-enter is skipped.
    if (current_fight_start_wall_ == 0) current_fight_start_wall_ = now;

    // Damage + per-skill stats only for events that actually dealt HP
    // damage. A zero-value killing blow reaches here (to credit the kill
    // below) but must not add a phantom 0-damage skill hit.
    if (delta > 0) {
        if (owner->first_damage_wall == 0) owner->first_damage_wall = now;
        owner->last_damage_wall = now;
        owner->damage_total += delta;

        auto& sk = owner->skills[ev->skillid];
        sk.damage += delta;
        sk.hits   += 1;
        if (ev->buff == 0) {
            sk.strike_hits += 1;
            if (ev->result == CBTR_STRIKE_DAMAGECRIT) sk.crits += 1;
        }
        if (sk.min_hit == 0 || delta < sk.min_hit) sk.min_hit = delta;
        if (delta > sk.max_hit)                    sk.max_hit = delta;
        if (sk.first_hit_wall == 0) sk.first_hit_wall = now;
        sk.last_hit_wall = now;
        // Cap per-skill so heavy condi tickers (Burning, Bleed) can't blow
        // memory on long fights.
        sk.hits_history.push_back({now, delta});
        if (sk.hits_history.size() > 1024) sk.hits_history.pop_front();
    }

    // Per-target damage attribution drains on the downing hit
    // (ev->result == CBTR_DOWNED) into damage_to_downed. arc's realtime
    // feed delivers CHANGEDOWN/CHANGEDEAD only for squad members per the
    // evtc spec, so enemy-player downs in WvW never surface as state-
    // changes — the result-code / is_offcycle paths below are the only
    // realtime signals for non-squad targets.
    //
    // Player-vs-player only. NPCs (pets, minions, clones, jade mech)
    // classify with elite == 0xFFFFFFFFu and some go through real
    // downstate; counting them would inflate down counts. Players have
    // elite != 0xFFFFFFFF per the evtc spec.
    // Killing blows are handled above (kill credit + cleanup) and must NOT
    // re-enter the down state machine: the target is already gone, and a
    // single GW2 hit can never both down and kill (downstate has its own HP
    // pool), so there is no down for this event to discover.
    bool dst_is_player = dst && dst->id &&
                         dst->elite != 0xFFFFFFFFu &&
                         ev->result != CBTR_KILLINGBLOW;
    if (dst_is_player) {
        // Per evtc spec (references/README_EVTC.md, CBTS_COMBAT):
        // is_offcycle == 1 means "dst was downed at the START of the
        // event" — so a regular damage event whose owner just downed the
        // target reports is_offcycle == 0 (target was still up). The
        // FIRST event observing is_offcycle == 1 is therefore a post-down
        // event whose owner is a cleaver, not the finisher. Crediting
        // them is wrong; track the most recent pre-down attacker and
        // credit that one instead.
        bool was_down = downed_[dst->id];
        bool is_down  = (ev->is_offcycle != 0);

        // Only accumulate while target is up (pre-down hits). The
        // discovery event (is_down=1, was_down=0) and cleave-on-downed
        // events are post-down and don't count toward contribution.
        //
        // delta > 0 is required: zero-damage control events now reach this
        // point, and letting one write last_attacker_ would hand the down
        // credit to whoever last dazed the target instead of whoever
        // actually brought their health down.
        if (delta > 0 && !is_down && !was_down) {
            target_dmg_[dst->id][owner->id] += delta;
            last_attacker_[dst->id]         = owner->id;
        }

        // Two paths flip the target into downstate for our purposes:
        //   explicit_down  — arc tagged result=CBTR_DOWNED (this event's
        //                    owner is the finisher by definition; valid
        //                    for both strike and condi).
        //   discovery_down — is_offcycle 0->1 transition with no
        //                    CBTR_DOWNED (the only realtime signal for
        //                    most non-squad foe downs in WvW). Credit
        //                    last_attacker_ — the actual downer — rather
        //                    than the discovery event's post-down owner.
        bool explicit_down  = (ev->result == CBTR_DOWNED) && !was_down;
        bool discovery_down = is_down && !was_down && !explicit_down;
        if (explicit_down || discovery_down) {
            // discovery_down credits the last pre-down attacker, not the
            // discovery event's owner (a post-down cleaver). If we never
            // observed a pre-down hit — target entered our view already
            // downed — last_attacker_ is empty and there is no one to
            // credit. target_dmg_ and last_attacker_ populate together, so
            // the drain below is a no-op in that case; skip the down credit
            // rather than defaulting to owner->id and blaming the cleaver.
            bool      have_finisher = true;
            uintptr_t finisher_id   = owner->id;
            if (discovery_down) {
                auto la = last_attacker_.find(dst->id);
                if (la != last_attacker_.end()) finisher_id = la->second;
                else                            have_finisher = false;
            }
            auto tit = target_dmg_.find(dst->id);
            if (tit != target_dmg_.end()) {
                for (const auto& [aid, dmg] : tit->second) {
                    auto ait = agents_.find(aid);
                    if (ait != agents_.end()) {
                        ait->second.damage_to_downed += dmg;
                    }
                }
                target_dmg_.erase(tit);
            }
            if (have_finisher) {
                auto fit = agents_.find(finisher_id);
                if (fit != agents_.end()) {
                    fit->second.downs_contributed += 1;
                }
            }
            // Sticky the down flag so subsequent cleave-on-downed events
            // hit the was_down guard. Needed when arc fires CBTR_DOWNED
            // with is_offcycle=0 — the next-event view would otherwise
            // see was_down=0 and re-trigger the discovery branch.
            downed_[dst->id] = true;
            last_attacker_.erase(dst->id);
        } else {
            // Normal hit, cleave-on-downed, or rally. is_down already
            // reflects the post-event state per spec.
            downed_[dst->id] = is_down;
        }
    }

    // Kill credit, taken AFTER fight-boundary management so it lands on the
    // current fight and can't be wiped by a deferred/implicit reset above.
    // A killing blow is always a finish on an already-downed or gibbed
    // target (a single GW2 hit can't both down and kill — downstate has its
    // own HP pool), so drop the target's fight-scoped state here so a
    // trailing cleave is not re-attributed.
    if (is_killing_blow) {
        owner->kills_contributed += 1;
        target_dmg_.erase(dst->id);
        downed_.erase(dst->id);
        last_attacker_.erase(dst->id);
    }

    // Control credit, taken alongside the kill for the same reason: after
    // fight-boundary management, so an interrupt or a stun landing as the
    // first action of a new fight isn't wiped by the reset it triggered.
    if (is_interrupt) owner->interrupts += 1;
    if (is_cc) {
        owner->cc_count += 1;
        if (ev->value > 0)
            owner->cc_duration_ms += static_cast<uint64_t>(ev->value);
    }

    // Sample cumulative damage every ~500ms. deque pop_front is O(1) —
    // vector::erase(begin) was O(n) and stalled the combat thread on long
    // fights once the cap was reached. Skipped for a zero-damage killing
    // blow: damage_total is unchanged, so there's nothing new to sample.
    if (delta > 0) {
        if (owner->history.empty() ||
            now - owner->history.back().wall_ms >= 500) {
            owner->history.push_back({now, owner->damage_total});
            if (owner->history.size() > 4096) {
                owner->history.pop_front();
            }
        } else {
            owner->history.back().damage_total = owner->damage_total;
        }
    }
    (void)dst;
}

AgentState* Tracker::touch_agent(ag* src) {
    if (!src || !src->id) return nullptr;
    auto it = agents_.find(src->id);
    if (it != agents_.end()) return &it->second;
    // Only self can be lazy-created here; squad members must come via
    // tracking-add. Bosses/NPCs are rejected.
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
    // Linear fallback: after reset_fight() clears instid_to_id_, agents
    // still retain their instid until tracking-add or lazy-register rebinds.
    for (auto& [id, s] : agents_) {
        if (s.instid == instid) return &s;
    }
    return nullptr;
}

void Tracker::reset_fight() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Capture the in-progress fight to history before wiping so the user
    // doesn't lose just-completed stats by hitting Reset.
    push_to_history();
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
            s.kills_contributed   = 0;
            s.interrupts          = 0;
            s.cc_count            = 0;
            s.cc_duration_ms      = 0;
            s.alive               = true;
            s.fight_armed         = false;
            s.skills.clear();
            s.history.clear();
            // Clear per-agent instid alongside instid_to_id_ so the linear
            // fallback in find_by_instid() can't match a stale binding
            // after instids are recycled between fights.
            s.instid              = 0;
            ++it;
        }
    }
    instid_to_id_.clear();
    target_dmg_.clear();
    downed_.clear();
    last_attacker_.clear();
    any_in_combat_ = false;
}

void Tracker::push_to_history() {
    // Caller holds mutex_.
    //
    // Fight-scoped per-target state drops on every fight boundary, even
    // when no history entry gets recorded (start wall 0). Without this,
    // target_dmg_ entries from un-downed/un-killed targets (instance adds
    // at SQCOMBATEND, ungibbed enemies at MAPCHANGE) leak into the next
    // pull and inflate damage_to_downed / downs_contributed beyond the
    // new fight's damage_total. reset_fight() also calls this path; the
    // redundant clear there is harmless.
    target_dmg_.clear();
    downed_.clear();
    last_attacker_.clear();

    // No fight opened since the last push — nothing to record. The
    // encounter identity still drops, so a log target seen without a fight
    // (or one left over from a fight already pushed) cannot label the next.
    if (current_fight_start_wall_ == 0) {
        current_boss_species_ = 0;
        sq_end_clock_         = 0;
        return;
    }

    // End the fight at the last credited hit, not at push time: in WvW the
    // push runs at manual Reset or MAPCHANGE, often minutes after combat,
    // which inflated durations and skewed the history timestamps.
    uint64_t now_wall = wall_now();
    uint64_t end_wall = 0;
    for (const auto& [id, s] : agents_) {
        if (!s.is_player) continue;
        if (s.last_damage_wall > end_wall) end_wall = s.last_damage_wall;
    }
    if (end_wall <= current_fight_start_wall_ || end_wall > now_wall) {
        end_wall = now_wall;
    }

    FightSnapshot fs;
    fs.start_wall   = current_fight_start_wall_;
    fs.end_wall     = end_wall;
    fs.boss_species = current_boss_species_;
    if (sq_end_clock_ != 0) {
        // arc told us exactly when the encounter closed (CBTS_SQCOMBATEND).
        fs.end_clock = sq_end_clock_;
    } else {
        // No SQCOMBATEND — WvW, or a manual reset. Back-date the clock stamp
        // by the wall gap so "21:34 (5m ago)" refers to when the fighting
        // stopped, not when the user hit Reset.
        fs.end_clock = static_cast<uint64_t>(std::time(nullptr))
                     - (now_wall - end_wall) / 1000;
    }

    bool has_data  = false;
    bool any_score = false; // any down or kill — short fights that scored stay
    for (const auto& [id, s] : agents_) {
        if (!s.is_player) continue;
        // Contribution only — deliberately not has_fight_state(), which also
        // counts time spent in combat. A squad standing in combat doing
        // nothing must not manufacture a history entry.
        if (s.damage_total == 0 && s.strip_count == 0 &&
            s.cleanse_count == 0 && s.damage_to_downed == 0 &&
            s.downs_contributed == 0 && s.kills_contributed == 0 &&
            s.interrupts == 0 && s.cc_count == 0) continue;
        if (s.downs_contributed > 0 || s.kills_contributed > 0)
            any_score = true;
        // Clone, then drop per-skill hits_history to keep memory bounded.
        // Spike overlay won't render for past fights; everything else
        // (DPS curve, skills table, sort) still works because totals,
        // first/last hit timestamps, and the DamagePoint history deque
        // are preserved.
        AgentState clone = s;
        for (auto& [skid, sk] : clone.skills) {
            std::deque<SkillHit> empty;
            sk.hits_history.swap(empty);
        }
        fs.agents.emplace(id, std::move(clone));
        has_data = true;
    }

    current_fight_start_wall_ = 0;
    current_boss_species_     = 0;
    sq_end_clock_             = 0;

    if (!has_data) return;

    // Skip junk entries: a fight shorter than the gap (poked a sentry,
    // accidental aggro) isn't worth a history slot — UNLESS someone scored
    // a down or kill, which makes even a 4-second gank reviewable.
    if (options().fight_gap_enabled.load(std::memory_order_relaxed)) {
        uint64_t gap_ms = options().fight_gap_ms.load(std::memory_order_relaxed);
        uint64_t dur    = fs.end_wall > fs.start_wall
                        ? fs.end_wall - fs.start_wall : 0;
        if (dur < gap_ms && !any_score) return;
    }

    history_.push_back(std::move(fs));
    while (static_cast<int>(history_.size()) > kHistoryMax) {
        history_.pop_front();
    }
}

void Tracker::tick() {
    // Auto-close a fight that has gone quiet. Without this, WvW never
    // produces a history entry on its own: CBTS_SQCOMBATEND is instanced
    // content only, and a map change or a manual Reset are the only other
    // things that push. The whole session ended up as one entry.
    //
    // Disabled alongside smart fight boundaries — with those off the user
    // has asked for the legacy immediate-reset behaviour and no gap concept.
    if (!options().fight_gap_enabled.load(std::memory_order_relaxed)) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (current_fight_start_wall_ == 0) return; // nothing open to close
    if (any_in_combat_) return;                 // still fighting

    // Newest credited action across the squad. Combat having dropped is not
    // enough on its own: GW2 leaves combat ~4s after the last hit, and a
    // re-engage inside the gap is the same fight by this plugin's own rule.
    uint64_t last = 0;
    for (const auto& [id, s] : agents_) {
        if (!s.is_player) continue;
        if (s.last_activity_wall > last) last = s.last_activity_wall;
    }
    // A fight that opened but never scored anything has no activity stamp;
    // measure from its start so it cannot linger open forever. push_to_history
    // drops it as empty and clears the open-fight state either way.
    uint64_t ref = last != 0 ? last : current_fight_start_wall_;
    uint64_t now = wall_now();
    if (now <= ref) return;
    if (now - ref < options().fight_gap_ms.load(std::memory_order_relaxed))
        return;

    // Rows keep their numbers — this closes the fight for history only. The
    // existing deferred-reset rule wipes them on the first action of the
    // next fight, so the last fight stays readable while out of combat.
    push_to_history();
}

const FightSnapshot* Tracker::find_fight(uint64_t start_wall) const {
    // Caller holds mutex_.
    if (start_wall == 0) return nullptr;
    for (const auto& fs : history_) {
        if (fs.start_wall == start_wall) return &fs;
    }
    return nullptr;
}

void Tracker::fight_summaries(std::vector<FightSummary>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    out.reserve(history_.size());
    // history_ is oldest-first; emit newest-first so index 0 is "Fight -1"
    // and the UI never has to invert anything.
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        const FightSnapshot& fs = *it;
        FightSummary sum;
        sum.start_wall   = fs.start_wall;
        sum.end_wall     = fs.end_wall;
        sum.end_clock    = fs.end_clock;
        sum.boss_name    = boss_name_for(fs.boss_species);
        sum.total_damage = 0;
        sum.players      = 0;
        for (const auto& [id, s] : fs.agents) {
            if (!s.is_player) continue;
            sum.total_damage += s.damage_total;
            ++sum.players;
        }
        out.push_back(sum);
    }
}

bool Tracker::snapshot_for(uint64_t start_wall,
                           std::vector<Snapshot>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    const FightSnapshot* fs = find_fight(start_wall);
    if (!fs) return false;
    out.reserve(fs->agents.size());
    for (const auto& [id, s] : fs->agents) {
        if (!s.is_player) continue;
        // Past fights are never "in combat".
        out.push_back(make_snapshot(s, stored_combat_ms(s), /*in_combat=*/false));
    }
    return true;
}

bool Tracker::agent_snapshot_for(uint64_t start_wall, uintptr_t agent_id,
                                 Snapshot& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const FightSnapshot* fs = find_fight(start_wall);
    if (!fs) return false;
    auto it = fs->agents.find(agent_id);
    if (it == fs->agents.end()) return false;
    const auto& s = it->second;
    if (!s.is_player) return false;
    out = make_snapshot(s, stored_combat_ms(s), /*in_combat=*/false);
    return true;
}

namespace {
void fill_top_skills(const AgentState& s,
                     const std::unordered_map<uint32_t, std::string>& names,
                     int n, std::vector<SkillDetail>& out) {
    out.clear();
    std::vector<SkillDetail> all;
    all.reserve(s.skills.size());
    for (const auto& [skid, entry] : s.skills) {
        if (entry.damage == 0) continue;
        SkillDetail sd;
        sd.skill_id       = skid;
        auto nit = names.find(skid);
        if (nit != names.end()) sd.name = nit->second;
        sd.damage         = entry.damage;
        sd.hits           = entry.hits;
        sd.strike_hits    = entry.strike_hits;
        sd.crits          = entry.crits;
        sd.min_hit        = entry.min_hit;
        sd.max_hit        = entry.max_hit;
        sd.first_hit_wall = entry.first_hit_wall;
        sd.last_hit_wall  = entry.last_hit_wall;
        all.push_back(std::move(sd));
    }
    int top = std::min<int>(n, static_cast<int>(all.size()));
    if (top == 0) return;
    std::partial_sort(all.begin(), all.begin() + top, all.end(),
        [](const SkillDetail& a, const SkillDetail& b) { return a.damage > b.damage; });
    out.assign(std::make_move_iterator(all.begin()),
               std::make_move_iterator(all.begin() + top));
}
} // namespace

bool Tracker::top_skills(uintptr_t agent_id, int n,
                         std::vector<SkillDetail>& out) const {
    out.clear();
    if (n <= 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = agents_.find(agent_id);
    if (it == agents_.end()) return false;
    fill_top_skills(it->second, skill_names_, n, out);
    return !out.empty();
}

bool Tracker::top_skills_for(uint64_t start_wall, uintptr_t agent_id, int n,
                             std::vector<SkillDetail>& out) const {
    out.clear();
    if (n <= 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const FightSnapshot* fs = find_fight(start_wall);
    if (!fs) return false;
    auto it = fs->agents.find(agent_id);
    if (it == fs->agents.end()) return false;
    fill_top_skills(it->second, skill_names_, n, out);
    return !out.empty();
}

bool Tracker::detail_for(uint64_t start_wall, uintptr_t agent_id,
                         AgentDetail& out, uint32_t spike_skill) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const FightSnapshot* fs = find_fight(start_wall);
    if (!fs) {
        out.name.clear();
        return false;
    }
    auto it = fs->agents.find(agent_id);
    if (it == fs->agents.end()) {
        out.name.clear();
        return false;
    }
    const auto& s = it->second;
    out.name  = s.name;
    out.account = s.account;
    out.prof  = s.prof;
    out.elite = s.elite;
    out.subgroup = s.subgroup;
    out.history.assign(s.history.begin(), s.history.end());
    out.history_start_wall = !out.history.empty() ? out.history.front().wall_ms : 0;
    out.skills.clear();
    out.skills.reserve(s.skills.size());
    for (const auto& [skid, entry] : s.skills) {
        SkillDetail sd;
        sd.skill_id       = skid;
        auto nit = skill_names_.find(skid);
        if (nit != skill_names_.end()) sd.name = nit->second;
        sd.damage         = entry.damage;
        sd.hits           = entry.hits;
        sd.strike_hits    = entry.strike_hits;
        sd.crits          = entry.crits;
        sd.min_hit        = entry.min_hit;
        sd.max_hit        = entry.max_hit;
        sd.first_hit_wall = entry.first_hit_wall;
        sd.last_hit_wall  = entry.last_hit_wall;
        // entry.hits_history is always empty here — push_to_history clears
        // it to bound memory — so the spike overlay cannot render for a past
        // fight and the UI hides that control while viewing one. The
        // parameter is kept only for signature symmetry with detail().
        (void)spike_skill;
        out.skills.push_back(std::move(sd));
    }
    std::sort(out.skills.begin(), out.skills.end(),
        [](const SkillDetail& a, const SkillDetail& b) { return a.damage > b.damage; });
    return true;
}

void Tracker::detail(uintptr_t id, AgentDetail& out,
                     uint32_t spike_skill) const {
    out.skills.clear();
    out.history.clear();
    out.name.clear();
    out.account.clear();
    out.prof = 0;
    out.elite = 0;
    out.subgroup = 0;
    out.history_start_wall = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = agents_.find(id);
        if (it == agents_.end()) return;
        const auto& s = it->second;
        out.name  = s.name;
        out.account = s.account;
        out.prof  = s.prof;
        out.elite = s.elite;
        out.subgroup = s.subgroup;
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
            sd.strike_hits    = entry.strike_hits;
            sd.crits          = entry.crits;
            sd.min_hit        = entry.min_hit;
            sd.max_hit        = entry.max_hit;
            sd.first_hit_wall = entry.first_hit_wall;
            sd.last_hit_wall  = entry.last_hit_wall;
            // Only the spike-overlay skill needs its per-hit timeline;
            // copying every skill's 1024-entry deque each frame was the
            // detail window's dominant cost under the combat mutex.
            if (sid == spike_skill && spike_skill != 0) {
                sd.hits_history.assign(entry.hits_history.begin(),
                                       entry.hits_history.end());
            }
            out.skills.push_back(std::move(sd));
        }
    }
    // Sort outside the lock so the combat thread isn't blocked while
    // sorting the skill table (worst case ~100 entries).
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
        // Drop squadmates arc marked as removed (left squad, changed
        // instance). Self always stays so previous-fight stats remain
        // visible OOC.
        if (!s.present && !s.is_self) continue;
        // Drop squadmates that haven't fired any arc event in the staleness
        // window. Arc has no realtime CBTS_MAPCHANGE / DESPAWN signal we can
        // hook for the local player, so old-map squadmates persist forever
        // unless a separate explicit tracking-remove fires (which arc does
        // not always do across instance ports). 60s is long enough to keep
        // OOC squadmates visible during normal play; arc forwards their
        // boon ticks, animations, and state changes well within that window
        // when they're in range.
        constexpr uint64_t kStaleAgentMs = 60000;
        if (!s.is_self && s.last_seen_wall != 0 &&
            now - s.last_seen_wall > kStaleAgentMs) continue;
        // Combat time = first->last damage event, extending to now while
        // still in combat. Matches arc's Damage panel denominator.
        uint64_t ms = 0;
        if (s.first_damage_wall != 0) {
            // An armed row shows the PREVIOUS fight frozen as-is: the
            // window must not extend to "now" or the old fight's DPS
            // would decay while the player stands in combat not acting.
            uint64_t end = (s.in_combat_wall && !s.fight_armed)
                         ? now : s.last_damage_wall;
            if (end > s.first_damage_wall) ms = end - s.first_damage_wall;
        }
        // Armed = old stats on display; render it as the paused row it is,
        // not as live in-combat numbers.
        bool in_combat = s.in_combat_wall.has_value() && !s.fight_armed;
        out.push_back(make_snapshot(s, ms, in_combat));
    }
}

} // namespace idps
