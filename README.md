# arcdps_individual_dps

ArcDPS extension. Restores per-player combat clocks after the **April 14, 2026**
arcdps update collapsed every fight stat onto a single squad-wide clock.
Each row pauses on `CBTS_EXITCOMBAT`, resumes on `CBTS_ENTERCOMBAT`.

![preview](docs/screenshot.png)

## Download

[Latest release](https://github.com/iTroy0/arcdps_individual_dps/releases/latest)

Auto-updates via arcdps's `get_update_url`: plugin queries GitHub Releases on
load, returns a URL when a newer tag exists, arcdps downloads + reloads the DLL.

## Features

- Per-player damage / DPS table, EMA-smoothed live DPS column
- Active-damage-window denominator (matches arcdps `Damage (excl)`)
- Per-self fight boundaries — squadmates don't reset your row
- Profession icons + colored names
- Click row → detail window: DPS-over-time graph with crosshair tooltip,
  per-skill breakdown sorted by damage
- Cleanses / Strips side windows
- Down-contribution window: damage to enemy players up to the moment they go
  down, plus count of distinct downs you contributed to
- Right-click context menu + arcdps options panel
- Window opacity slider (0.10 – 1.00)
- NPC / Gadget exclusion filters
- Settings persisted to `arcdps_individual_dps.ini`

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
  target is up. On `CBTR_DOWNED` (or first `is_offcycle == 1` event for
  non-squad foes), tally drains: `damage_to_downed += sum`,
  `downs_contributed += 1` per attacker. Cleave-on-downed is excluded.
  Player targets only (`elite != 0xFFFFFFFF`).
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
