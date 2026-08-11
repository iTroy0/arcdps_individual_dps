#pragma once

#include <cstdint>

namespace idps {

void      set_selected_agent(uintptr_t id);
uintptr_t selected_agent();

// viewed_history_idx: 0 = current fight, N>0 = N-th most recent past fight,
// used only for the window label. viewed_start_wall is what actually selects
// the stored fight — a FIFO index would be invalidated by any push landing
// between the caller reading the list and this call.
void draw_detail_window(int viewed_history_idx, uint64_t viewed_start_wall);

// Called from the wndproc on ESC. Returns true if it consumed the press
// (so the wndproc swallows it and GW2's game menu doesn't open); false
// when already closed so ESC propagates to the game.
bool consume_esc_for_detail();

} // namespace idps
