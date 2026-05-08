#pragma once

#include <cstdint>
#include <vector>

#include "tracker.h"

namespace idps {

void draw_support_window(const char* title, bool* open,
                         const std::vector<Snapshot>& rows,
                         uint32_t Snapshot::*field);

void draw_downs_window(bool* open, const std::vector<Snapshot>& rows);

} // namespace idps
