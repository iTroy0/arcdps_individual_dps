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
- Fight history — the last 5 fights are kept, newest first, each stamped
  with its end time (clock + "Xm ago"), duration, squad damage, and player
  count; browse them from the window right-click menu or a player row's
  context menu — Cleanses / Strips / Downs windows label themselves with
  the viewed fight and offer a one-click Live return
- Colored per-player damage bars, profession icons, optional gold self-name and
  pin-self-to-top
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
- Layout — responsive columns, lockable windows, screen-relative positioning,
  window opacity slider (0.10 – 1.00)
- NPC / Gadget exclusion filters
- Right-click menu + arcdps options-panel tab; settings persist to
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
  dropped. Otherwise `ev->value > 0` only.
- **Down contribution** — per-target tally accumulates per attacker while
  target is up. On `CBTR_DOWNED` (or first `is_offcycle == 1` event), the
  tally drains into `damage_to_downed` for every attacker, and the down
  itself (`downs_contributed`) is credited to exactly one player — the
  owner of the event that flipped the target into downstate (the finisher
  for `CBTR_DOWNED`; for the offcycle fallback, almost always the downing
  strike/tick's owner). `CBTS_CHANGEDOWN` carries no attacker identity and
  credits no down. Cleave-on-downed is excluded. Player targets only
  (`elite != 0xFFFFFFFF`).
- **Strips / Cleanses** — counted from `CBTS_BUFFREMOVE_ALL` events
  (`is_buffremove == CBTB_ALL`), filtered by hardcoded boon / condition ID lists.
- **combat_local** is null — arc fires identical payloads on both `combat`
  and `combat_local`; routing both double-counts.

## Network

- `winhttp` GET to `api.github.com/repos/iTroy0/arcdps_individual_dps/releases/latest`
  on plugin load (synchronous, ~2 s timeout). No telemetry, no other endpoints.
- arcdps performs the actual update download when `get_update_url` returns a URL.

## License

Unlicensed personal project. Not affiliated with arcdps, deltaconnected, or
ArenaNet. Follows arcdps's "don't be a dick" — no network beyond the
update-check above, no file I/O outside the debug log + settings ini.
