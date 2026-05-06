#pragma once

#include <cstdint>
#include <vector>

#include "tracker.h"

namespace idps {

void draw_support_window(const char* title, bool* open,
                         const std::vector<Snapshot>& rows,
                         uint32_t Snapshot::*field);

// Down contribution: per-player damage dealt to foes that subsequently
// went into downstate, plus the count of distinct downs each player
// contributed to. Sorted by contribution damage descending.
void draw_downs_window(bool* open, const std::vector<Snapshot>& rows);

} // namespace idps
