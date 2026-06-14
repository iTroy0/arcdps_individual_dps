#pragma once

#include <cstdint>
#include <vector>

#include "tracker.h"

namespace idps {

// viewed_fight: 0 = live, N>0 = viewing N-th most recent past fight; the
// window title gains a "(Fight -N)" suffix and a header line with a Live
// button. go_live is set true when the user clicks Live (the caller owns
// the view state).
void draw_support_window(const char* title, bool* open,
                         const std::vector<Snapshot>& rows,
                         uint32_t Snapshot::*field,
                         int viewed_fight, bool* go_live);

void draw_downs_window(bool* open, const std::vector<Snapshot>& rows,
                       int viewed_fight, bool* go_live);

} // namespace idps
