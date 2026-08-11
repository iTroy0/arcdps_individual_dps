# arcdps_individual_dps

ArcDPS extension. Restores per-player combat clocks after the **April 14, 2026**
arcdps update collapsed every fight stat onto a single squad-wide clock.
Each row pauses on `CBTS_EXITCOMBAT`, resumes on `CBTS_ENTERCOMBAT`.

![preview](docs/screenshot.png)

## Download

[![Download latest release](https://img.shields.io/badge/Download-latest%20release-2ea44f?style=for-the-badge)](https://github.com/iTroy0/arcdps_individual_dps/releases/latest)

New to GitHub? Click the green button above, then download
`arcdps_individual_dps.dll` from the **Assets** list on the release page.

After the first install it updates itself: the plugin checks GitHub Releases on
load and arcdps downloads + reloads the DLL whenever a newer version exists.

## Features

- Per-player damage / DPS table — EMA-smoothed live DPS column, active-damage-
  window denominator (matches arcdps `Damage (excl)`), click headers to sort
- Per-self fight boundaries — squadmates entering / leaving combat don't reset
  your row
- Smart fight boundaries (toggleable, gap 1-60 s, default 5) — re-entering
  combat within the gap resumes the same fight; beyond it your row resets
  only on your first action, so NPC aggro or stray AoE never wipes stats;
  fights shorter than the gap skip history unless they scored a down / kill
- Fight history — a fight closes and is stored once the squad has been out
  of combat for the fight gap, so WvW skirmishes each get their own entry
  instead of the whole session becoming one. Instanced content also closes
  on arcdps's own `CBTS_SQCOMBATEND`. The last 5 fights are kept, newest
  first (Current, then -1, -2, ...), each stamped
  with its end time (clock + "Xm ago"), duration, squad damage, player
  count, and the encounter name when arcdps logged a boss; browse them from
  the window right-click menu or a player row's context menu — Cleanses /
  Strips / Downs windows label themselves with the viewed fight
  and offer a one-click Live return
- Profession colors read live from arcdps's own palette, so both overlays
  match exactly and follow any recolor you make in arcdps options
- Colored per-player damage bars, profession icons, elite-spec name on icon
  hover, optional subgroup column, gold self-name and pin-self-to-top
- Player names always render in the default text color, matching the metric
  columns beside them, and are never dimmed out of combat — the icon and the
  damage bar already carry combat state, so shading the names too only made
  the table read as inconsistent
- Click a row → detail window: DPS-over-time graph with smoothed / cumulative /
  average / burst overlay layers (toggle each from the legend), crosshair
  tooltip, and a per-skill breakdown sorted by damage with crit % and
  per-hit min / avg / max stats on hover
- Squad totals line (Σ damage, Σ DPS, player count) + right-click →
  "Copy summary" puts the visible table on the clipboard as plain text
- Cleanses / Strips side windows
- Down-contribution window — damage to enemy players up to the moment they go
  down, the count of downs you personally landed the finishing hit on, and
  your killing blows on enemy players
- Post-update banner — one-shot notice after arcdps swaps in a new build
- Column headers toggle — hide the header row on the overlay tables for a
  compact, chrome-free look; sorting moves to each window's right-click menu
  so nothing becomes unreachable. The detail window keeps its headers
- Layout — responsive columns, lockable windows, separate window
  (0.10 – 1.00) and damage-bar (0.15 – 1.00) opacity sliders.
  Text and icons are always fully opaque: the windows are translucent over
  the game world, so any alpha below 1 on a glyph lets terrain through and
  reads as a washed-out color rather than a softer one. Out-of-combat rows
  are shaded by darkening RGB, not by dropping alpha, for the same reason
- NPC / Gadget exclusion filters
- Windows appear in arcdps's own window list, plus a right-click menu and an
  arcdps options-panel tab; settings persist to
  `arcdps_individual_dps.ini` (auto-saved every ~15 s while playing, so a
  game crash doesn't lose your layout)

## Install

1. Build (below) or grab a release.
2. Drop `arcdps_individual_dps.dll` next to `d3d11.dll` in `<GW2>/addons/arcdps/`.
3. Verify under arcdps options → Extensions.

PNG icons are embedded. File-fallback path: `<GW2>/addons/individual_dps_icons/`.

## Build

MSVC x64, CMake 3.20+, VS 2022 Build Tools.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output: `build/Release/arcdps_individual_dps.dll`.

`FetchContent` pulls Dear ImGui `v1.92.7` to match arcdps. Bump the tag in
`CMakeLists.txt` if arcdps moves — mismatched ImGui versions corrupt shared
context. Local tree alternative:

```bat
cmake -S . -B build -A x64 -DIMGUI_DIR=C:/path/to/imgui
```

MSVC runtime is statically linked — no VC++ redistributable needed on the
target machine.

## Behavior

- **Combat time** = first credited damage → last credited damage (extended to
  now while in combat). Not combat-flag duration.
- **Implicit-enter** — if arc skips `CBTS_ENTERCOMBAT`, the first self-source
  strike opens a fight. Pets / minions / condi ticks extend an active fight
  but cannot cold-start one. Idle >5 s outside an encounter log counts as a
  fight boundary.
- **Damage filter** — strikes with `BLOCK/EVADE/INTERRUPT/ABSORB/BLIND` are
  dropped. Otherwise `ev->value > 0` only. `CBTR_KILLINGBLOW` and
  `CBTR_DOWNED` are exempt from the `> 0` gate — overkill and barrier
  absorption routinely put those at or below zero — and are credited after
  fight-boundary handling so a reset they triggered cannot wipe them.
- **Interrupts / CC** — `CBTR_INTERRUPT` counts one per interrupted action;
  `CBTR_CROWDCONTROL` counts one per disable and sums `ev->value` as the
  applied duration in ms. Neither writes to the damage totals, and neither
  can claim down credit — only real damage records a pre-down attacker.
- **Down contribution** — per-target tally accumulates per attacker while
  target is up. On `CBTR_DOWNED` (or first `is_offcycle == 1` event), the
  tally drains into `damage_to_downed` for every attacker, and the down
  itself (`downs_contributed`) is credited to exactly one player — the
  owner of the event that flipped the target into downstate (the finisher
  for `CBTR_DOWNED`; for the offcycle fallback, almost always the downing
  strike/tick's owner). `CBTS_CHANGEDOWN` carries no attacker identity and
  credits no down. Cleave-on-downed is excluded. Player targets only
  (`elite != 0xFFFFFFFF`).
- **Strips / Cleanses** — counted from buff-removal events with
  `is_buffremove` of `CBTB_ALL` or `CBTB_SINGLE`, filtered by hardcoded
  boon / condition ID lists. `CBTB_MANUAL` is skipped: arcdps synthesizes one
  of those per stack on an all-remove, so counting it would multiply the
  tally by the stack size. A removal is only credited when it took real
  duration off (`value` / `buff_dmg` ≥ 50 ms) — the server fires the same
  event when a buff merely runs out, and counting those scored a cleanse
  every time a condition expired on its own.
  Strips count the 12 boons only (Aegis, Alacrity, Fury, Might, Protection,
  Quickness, Regeneration, Resistance, Resolution, Stability, Swiftness,
  Vigor); crowd control is not a boon and is never counted as a strip.
- **Subgroups** — read from `dst->team` on a player tracking-add and from
  `dst_agent` on `CBTS_ENTERCOMBAT` / `EXITCOMBAT`. Those two statechanges
  also carry the agent's profession (`value`) and elite spec (`buff_dmg`),
  which is the authoritative source after a build swap — the `ag` struct is
  not populated for state changes.
- **combat_local** is null — arc fires identical payloads on both `combat`
  and `combat_local`; routing both double-counts.

## arcdps API usage

Helper exports are resolved by name from the `arcdll` handle passed to
`get_init_addr`, and every one degrades to a built-in default if absent:

| Export | Used for |
|---|---|
| `e0` | arcdps.ini location |
| `e3` | load / diagnostic lines into `arcdps.log` |
| `e5` | live profession + subgroup color tables |
| `e6` | the "close windows with ESC" preference |

`options_windows` contributes this plugin's windows to arcdps's own window
list. `wnd_nofilter` consumes ESC for the detail window only when arcdps
reports that the user wants ESC to close windows.

This plugin's windows are governed solely by its own toggles. arcdps's hide
state (`e6` bit 0) and the `hide_if_combat_or_ooc` flag passed to `imgui` are
both deliberately ignored — arc's out-of-combat auto-hide would clear the
meter exactly when you sit down to read the fight you just finished.

`include/arcdps_api.h` is transcribed from the upstream evtc and api READMEs;
the `cbtstatechange` enum is load-bearing (arcdps sends raw integers), so
re-sync it by diffing the whole block rather than appending names.

## Network

- `winhttp` GET to `api.github.com/repos/iTroy0/arcdps_individual_dps/releases/latest`
  on plugin load (synchronous, ~2 s timeout). No telemetry, no other endpoints.
- arcdps performs the actual update download when `get_update_url` returns a URL.

## License

Unlicensed personal project. Not affiliated with arcdps, deltaconnected, or
ArenaNet. Follows arcdps's "don't be a dick" — no network beyond the
update-check above, no file I/O outside the debug log + settings ini.
