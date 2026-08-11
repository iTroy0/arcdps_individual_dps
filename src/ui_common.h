#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <imgui.h>

#include "tracker.h"

namespace idps {

// Profession colours. When arcdps exposes its own tables via e5 (and the
// user hasn't opted out) these return arc's live values, so the plugin's
// rows are the same colour as arc's — including any recolour the user
// applies in arc's options mid-session. Otherwise the canonical GW2 palette
// is used. `highlight` is arc's lighter companion shade, used for fills.
ImU32       prof_color(uint32_t prof);
ImU32       prof_color_highlight(uint32_t prof);
ImU32       subgroup_color(uint32_t subgroup);

const char* prof_short(uint32_t prof);
const char* prof_name (uint32_t prof);
// Elite spec name, or nullptr when the id isn't known.
const char* elite_name(uint32_t elite);
// Best available label: elite spec name when known, else the profession.
void        format_spec(char* out, size_t n, uint32_t prof, uint32_t elite);

void format_time (char* out, size_t n, uint64_t ms);
void format_count(char* out, size_t n, uint64_t v);

// Local clock "21:34" from unix seconds. Empty string when unix_s == 0.
void format_clock(char* out, size_t n, uint64_t unix_s);
// Relative "42s ago" / "5m ago" / "2h ago" from unix seconds. Empty
// string when unix_s == 0.
void format_ago  (char* out, size_t n, uint64_t unix_s);

// Out-of-combat shading: darkens RGB and leaves alpha alone. Colours here
// are composited over a translucent window, so dimming via alpha would let
// the game world through the glyphs and wash the colour out instead of
// darkening it.
ImU32 dim_color(ImU32 col);

// Replace a colour's alpha with `a` (0..1), keeping RGB.
ImU32 with_alpha(ImU32 col, float a);

void sort_rows(std::vector<Snapshot>& rows, int mode);

void pin_self_to_top(std::vector<Snapshot>& rows);
void pin_self_to_top(std::vector<size_t>& idx,
                     const std::vector<Snapshot>& rows);

void item_tooltip(const char* text);

// Position the next tooltip up-and-left of the cursor with an opaque bg.
// Call immediately before BeginTooltip.
void anchor_cursor_tooltip();

// Hover tooltip showing a player's account name. No-op when account is
// empty. Attach immediately after rendering the row's name item.
void account_tooltip(const std::string& account);

void align_icon_to_text();

void apply_window_pos  (float abs_x, float abs_y);
void capture_window_pos(const ImVec2& pos, float& abs_x, float& abs_y);

} // namespace idps
