#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <imgui.h>

#include "tracker.h"

namespace idps {

ImU32       prof_color(uint32_t prof);
const char* prof_short(uint32_t prof);

void format_time (char* out, size_t n, uint64_t ms);
void format_count(char* out, size_t n, uint64_t v);

ImU32 dim_alpha(ImU32 col);

void sort_rows(std::vector<Snapshot>& rows, int mode);

void item_tooltip(const char* text);

void align_icon_to_text();

void apply_window_pos  (float abs_x, float abs_y, float rx, float ry, bool relative,
                        bool& prev_relative, ImVec2& prev_ds);
void capture_window_pos(const ImVec2& pos, float& abs_x, float& abs_y,
                        float& rx, float& ry);

} // namespace idps
