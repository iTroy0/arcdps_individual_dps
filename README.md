# arcdps_individual_dps

ArcDPS extension that restores individual combat time after the
**April 14, 2026** arcdps update collapsed every fight stat to a single
group clock. Each squad member has their own clock that pauses on
`CBTS_EXITCOMBAT` and resumes on `CBTS_ENTERCOMBAT`, so personal DPS
stops diluting when you drop out of combat while others stay engaged.

## Features

- **Damage / DPS per player** in a live ImGui window
- **Active-damage window** denominator (matches arcdps's Damage panel)
- **Per-self fight boundaries** — your row resets on your own next
  ENTERCOMBAT, not the squad's
- **Profession icons + colored names** — 9 core professions + 27 known
  elite specs (icon slots reserve 4 per prof for future additions)
- **Click a row** for a detail window:
  - DPS-over-time line chart (kilo-DPS scale so hover stays readable)
  - Per-skill breakdown (skill name / damage / DPS / hits), sorted by damage
- **Cleanses + Strips** side windows, toggleable
- **NPC / Gadget exclusion** filters (uses deltaconnected's authoritative
  target classification rules)
- **Settings persisted** to `arcdps_individual_dps.ini` — window
  position/size, sort column, filters, visibility
- **Self-contained DLL** — static MSVC runtime, no redistributable needed

## Install

1. Build (see below) or grab a release.
2. Drop `arcdps_individual_dps.dll` next to arcdps
   (`<GW2>/addons/arcdps_individual_dps.dll` or `<GW2>/addons/`).
3. Launch GW2. Verify via arcdps options → Extensions — should list
   `individual_dps` v0.3.0.

Icons are embedded in the DLL. If they fail to load for any reason, drop
`specs/*.png` into `<GW2>/addons/individual_dps_icons/` as a file fallback.

## Usage

- **Main window** titled `Damage`. Resize by dragging.
- **Click a player's row** → opens their detail window with damage graph
  and skill breakdown. Resizable + scrollable.
- **Right-click inside the window** → context menu with:
  - Exclude NPCs
  - Exclude Gadgets
  - Sort (Damage / DPS / Name / Combat time)
  - Cleanses window toggle
  - Strips window toggle
- **ArcDPS options → Individual DPS** — same controls plus a show/hide
  toggle for the main window.

## Build

MSVC x64, CMake 3.20+, Visual Studio 2022 Build Tools.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output: `build/Release/arcdps_individual_dps.dll`.

`FetchContent` pulls ImGui `v1.80` (matches what current arcdps ships) and
`stb_image` (for PNG decoding). If arcdps bumps its ImGui version, update
the tag in `CMakeLists.txt` — bundling a mismatched version corrupts
shared ImGui state.

To use a local ImGui source instead of FetchContent:

```bat
cmake -S . -B build -A x64 -DIMGUI_DIR=C:/path/to/imgui
```

The MSVC runtime is statically linked (`/MT`) so the DLL runs without the
Visual C++ redistributable installed on the target machine.

## Tech Stack

- C++17, MSVC x64, static CRT
- CMake 3.20+ build, FetchContent deps
- Dear ImGui 1.80 (shared arc context), stb_image for PNG decode
- Direct3D 11 textures (arc-provided device)
- arcdps extension ABI (`mod_combat`, `mod_imgui`, `options_end`, etc.)

## Layout

```
/
  CMakeLists.txt
  include/
    arcdps_api.h           # vendored arcdps types
  res/
    icons.rc               # embedded profession/elite PNGs
  specs/
    001..009.png           # core prof icons
    e101..e904.png         # elite spec icons
  src/
    main.cpp               # DLL entry, get_init_addr / get_release_addr
    exports.cpp/.h         # arcdps_exports table + version string
    combat.cpp/.h          # mod_combat / mod_combat_local dispatch
    tracker.cpp/.h         # per-agent state, damage, strips, cleanses, skills
    ui.cpp/.h              # windows: Damage, detail, Strips, Cleanses
    icons.cpp/.h           # D3D11 texture loading (resource + file)
    settings.cpp/.h        # ini load/save
    log.cpp/.h             # debug log (arcdps_individual_dps.log)
```

## Behavior notes

- **Combat time** = time between first and last credited damage event
  (extended to "now" while the player is in combat). Matches arc's
  `Damage (excl)` panel denominator, not the combat-flag duration.
- **Damage filter** — strike events with result `BLOCK / EVADE /
  INTERRUPT / ABSORB / BLIND` are dropped by result code; anything else
  counts only when `ev->value > 0`. Negative values in arc's
  post-2026-04-14 feed appear on barrier-absorbed / invulnerable hits
  and arc itself does not count them toward DPS.
- **combat_local** callback is a no-op: arc fires the same payload
  on both `combat` and `combat_local`, so processing both double-counts.
- **Per-self reset** only fires on `CBTS_ENTERCOMBAT`. Damage landing
  while a player is OOC is dropped (prevents stray ticks from wiping
  the fight via implicit enter).
- **Buff events** — strips and cleanses use arc's `is_buffremove != 0`
  flag on buff events, filtered by a hardcoded list of boons (12 ids)
  and damaging/non-damaging conditions (14 ids).
- **History sampling** — per-agent cumulative damage is sampled every
  ~500ms into a ring buffer (capped at 4096 points) for the detail-window
  graph.

## Versioning

- `0.1.0` — phase 1 tracker + minimal debug UI.
- `0.2.0` — colored names, prof icons, strips/cleanses, context-menu
  options, settings persistence, arcdps options panel entry.
- `0.3.0` — click-to-drill-down detail window with damage-over-time graph
  and per-skill breakdown.

## License

Unlicensed personal project. Not affiliated with arcdps, deltaconnected,
or ArenaNet. Follows arcdps's "don't be a dick" policy — no network
calls, no file I/O outside the debug log + settings ini.

---

Made with ❤️ by Troy
