#pragma once

// Access to the helper functions arcdps exports from its own module (e0,
// e3, e5, e6, e7). They are resolved by name from the arcdps HMODULE that
// arrives as the `arcdll` parameter of get_init_addr — the api README lists
// them under "arcdps module exports".
//
// Every accessor degrades gracefully: if a symbol is missing (older arcdps,
// or the handle never arrived) the query reports unavailable and the caller
// keeps its own fallback. Nothing here is required for the plugin to run.

#include <cstdint>
#include <string>

#include <imgui.h>

namespace idps {

// Resolve the exports. Call once from get_init_addr with its `arcdll`
// argument. Repeat calls with the same handle are no-ops.
void arc_bind(void* arcdll);

// e3 — append a line to arcdps.log. Silently drops when unresolved.
void arc_log(const char* fmt, ...);

// e6 — current arcdps UI settings as a bit mask (see n_uisettings).
// Returns 0 when unresolved, which reads as "nothing hidden, nothing
// locked" and so leaves plugin behaviour unchanged.
//
// Note that this plugin deliberately does NOT follow arc's UI_HIDDEN bit:
// its windows are governed only by its own toggles. See mod_imgui.
uint64_t arc_ui_flags();

// User asked arcdps to close windows with ESC.
bool arc_ui_close_with_esc();

// e5 — arcdps's own colour tables. `out` is written and true returned only
// when arcdps supplied the table and the index is in range; otherwise `out`
// is untouched so callers can fall back to a built-in palette.
//
// The pointers arcdps hands back stay valid for its lifetime and are
// updated in place when the user edits colours in arc's options, so these
// are cheap to call every frame and track arc's settings live.
bool arc_core_color(uint32_t idx, ImVec4& out);
bool arc_prof_color(uint32_t prof, bool highlight, ImVec4& out);
bool arc_subgroup_color(uint32_t subgroup, bool highlight, ImVec4& out);

// True once e5 has handed us usable tables.
bool arc_colors_available();

// e0 — full path to arcdps.ini. Empty when unresolved.
std::wstring arc_ini_path();

} // namespace idps
