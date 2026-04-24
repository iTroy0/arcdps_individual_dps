# ArcDPS Individual Combat Timer Plugin — Build Spec

## Context

ArcDPS is the combat metrics addon for Guild Wars 2 by deltaconnected, loaded as a d3d11.dll shim. In the **April 14, 2026** update, arcdps removed per-agent individual combat time — all current fight stats now use a single group clock. The practical problem: when a player personally exits combat but other squad members remain engaged, that player's DPS number keeps diluting because the denominator advances while their damage total is frozen.

**Goal:** build an arcdps extension DLL that restores the old per-agent behavior. Each player's combat clock pauses on `CBTS_EXITCOMBAT` and resumes on `CBTS_ENTERCOMBAT`. Damage totals stay on screen until a new fight begins — nothing is zeroed when a player goes OOC mid-fight.

## Scope

**Phase 1 (this spec):** core tracking logic + minimal debug UI.

- Per-agent state map with pause/resume semantics.
- Correct fight-reset cue.
- Damage accumulation with pet/minion master resolution.
- Minimal ImGui window sufficient to verify the numbers are right — single table, one row per squad member, columns: name, DPS, damage, combat time.

**Phase 2 (later, separate spec):** full stats overlay — multi-window layout, column customization, profession coloring, skill/boon breakdowns, evtc logging for Elite Insights, etc. **Do not build this now.** Keep the phase 1 window bare and functional; the polished overlay comes after the tracker is proven correct.

## arcdps Extension API Cheat Sheet

Canonical reference: `deltaconnected.com/arcdps/api/arcdps_combatdemo.cpp`.

```cpp
// Required DLL exports
extern "C" __declspec(dllexport) void* get_init_addr(
    char* arcversion, ImGuiContext* imguictx, void* id3dptr,
    HANDLE arcdll, void* mallocfn, void* freefn, uint32_t d3dversion);
extern "C" __declspec(dllexport) void* get_release_addr();

// Exports table
typedef struct arcdps_exports {
    uintptr_t size;
    uint32_t  sig;         // unique 32-bit id, must not collide with other plugins
    uint32_t  imguivers;   // IMGUI_VERSION_NUM
    const char* out_name;
    const char* out_build;
    void* wnd_nofilter;
    void* combat;          // area combat events (squad scope) — our main feed
    void* imgui;           // per-frame render callback
    void* options_end;
    void* combat_local;    // local-only events — not needed for DPS
    void* wnd_filter;
    void* options_windows;
} arcdps_exports;

// Combat callback
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst,
                     char* skillname, uint64_t id, uint64_t revision);
```

Agent struct (subset):
```cpp
typedef struct ag {
    char*       name;
    uintptr_t   id;      // key our state map on this
    uint32_t    prof;
    uint32_t    elite;
    uint32_t    self;    // 1 if local player
    uint16_t    team;
} ag;
```

Statechange values we care about: `CBTS_ENTERCOMBAT`, `CBTS_EXITCOMBAT`, `CBTS_CHANGEDEAD`, `CBTS_LOGSTART`, `CBTS_LOGEND`. Verify numeric values against the current header — the April 14, 2026 update removed `CBTS_STATRESET` and other values may have shifted.

## Behavior Specification

### Per-agent state
One entry per squad member, keyed by `ag->id`:

- `accumulated_ms: u64` — total combat time this fight
- `in_combat_since: Option<u64>` — timestamp of latest ENTERCOMBAT; `None` if OOC
- `damage_total: u64` — strike + condition damage to active target(s)
- `name`, `prof`, `elite` — for display

### Event handling

**`CBTS_ENTERCOMBAT`:**
- Create state entry if new to current fight.
- Set `in_combat_since = ev->time`.
- Do NOT reset accumulated_ms or damage_total.

**`CBTS_EXITCOMBAT`:**
- If `in_combat_since` is Some, `accumulated_ms += ev->time - in_combat_since`.
- Clear `in_combat_since`.
- Leave damage_total unchanged — row stays visible and static.

**`CBTS_CHANGEDEAD`:** treat as forced EXITCOMBAT for accumulation.

**Damage events** (`ev != null && ev->is_statechange == 0`):
- Strike: `ev->is_buff == 0` → use `ev->value`.
- Condi tick: `ev->is_buff == 1 && ev->buff_dmg > 0` → use `ev->buff_dmg`.
- Attribute to `src->id`. If `ev->src_master_instid != 0`, credit the master agent (pets/minions/clones credit owner).
- Only count damage to active target(s) — mirror arc's target-selection cue.

### Live display math
```
display_ms = accumulated_ms + (in_combat_since ? now - in_combat_since : 0)
dps        = damage_total * 1000 / max(display_ms, 1)
```

### Fight reset
Match arcdps's own fight boundary — reset on the first damage event to a new target NPC after all squad members have been OOC, or on `CBTS_LOGEND`. This keeps numbers consistent with teammates' arc views. Everything zeroes on reset; the next ENTERCOMBAT starts a fresh fight.

## Project Layout

Keep it small. Single-exe MSVC build, no vcpkg, no external libs beyond what arc already exposes.

```
/
  CMakeLists.txt            # or .sln — MSVC x64
  src/
    main.cpp                # DLL entry, get_init_addr / get_release_addr
    exports.cpp/.h          # arcdps_exports wiring, mod_init, mod_release
    combat.cpp/.h           # mod_combat dispatch, event routing
    tracker.cpp/.h          # per-agent state, ENTER/EXIT, damage accumulation, fight reset
    ui.cpp/.h               # minimal ImGui table window (phase 1 only)
    log.cpp/.h              # debug logging to arcdps_individual_dps.log
  include/
    arcdps_api.h            # vendored from combatdemo.cpp
  README.md
```

Output: **`arcdps_individual_dps.dll`**, x64, MSVC, C++17+. Install next to arcdps (`addons/arcdps/` or `bin64/`).

## Implementation Order

1. Minimal DLL: `get_init_addr` returns a populated `arcdps_exports` with empty handlers. Verify arc loads it — it should appear in arc's Extensions menu.
2. `mod_combat` wired up. Log statechange counts to `arcdps_individual_dps.log` to confirm dispatch.
3. `tracker`: state map, ENTER/EXIT handling, `accumulated_ms` math. Log every transition so you can eyeball the state machine.
4. Damage accumulation with master-agent resolution.
5. Fight reset logic.
6. Minimal ImGui debug window — one table, refreshed each frame, columns: name / DPS / damage / combat time. That's it. No options, no multi-window, no styling.
7. Test in-game against the scenarios below. Fix bugs.
8. **Stop here.** Phase 2 (full overlay) is a separate spec.

## Edge Cases

- **Pet/minion damage:** resolve `src_master_instid` so ranger pets, necro minions, mesmer clones, mechanist jade mech all credit the owner.
- **Phantasms/short-lived agents:** damage events may arrive after despawn — tolerate unknown `src->id` with a master-instid → owner-id cache.
- **Revision parameter:** buff damage schema has changed across evtc revisions. Branch on `revision` and handle both.
- **`CBTS_CHANGEADDED` / `CBTS_CHANGEREMOVED`:** may arrive out of order with combat events. Do not assume a damage event's `src` is already known.
- **Map change / disconnect:** arc fires `CBTS_LOGEND` and flushes state. Global reset on this.
- **Solo play:** squad is just the local player (`src->self == 1`). Same logic, one row.
- **Paused combat (e.g., Deimos hands phase):** damage drops to zero but players stay in combat — the math handles this correctly since accumulated_ms only ticks while in_combat is set, which is what we want.

## Testing

- **Special Forces golem:** engage, walk out of combat while DoTs keep ticking on the golem. Verify your DPS number **freezes** instead of decaying.
- **Duo pull:** with a squadmate, one disengages while the other keeps fighting. Disengaged row freezes; active row keeps advancing.
- **Raid boss with phases** (Gorseval split, Deimos hands): per-player combat time advances only while each player is actually in combat.
- **Cross-check against dps.report:** save the evtc, parse on dps.report, compare against Elite Insights' active-time DPS column. Should match within a few percent.

## Constraints

- C++17+, MSVC, Windows x64 only.
- ImGui version must match what arcdps ships — do not bundle a second copy; use the `ImGuiContext*` arc passes to `get_init_addr`.
- No network calls. No file I/O outside the debug log.
- Do not hook arc's internal windows or stats — read `combat` events, render our own window only.
- Follow arcdps's "don't be a dick" policy.

## References

- arcdps API docs: https://www.deltaconnected.com/arcdps/api/
- Combat demo source (canonical API reference): https://www.deltaconnected.com/arcdps/api/arcdps_combatdemo.cpp
- evtc schema: https://www.deltaconnected.com/arcdps/evtc/

## Start Point

Scaffold the DLL with exports first and confirm arc loads it before writing any tracker logic. Log every ENTERCOMBAT/EXITCOMBAT and every damage event to the debug log during development so you can eyeball the event stream. Don't build the UI until the tracker logic is verified via logs.
