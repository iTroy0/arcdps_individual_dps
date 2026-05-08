#pragma once

#include <cstdint>

namespace idps {

void      set_selected_agent(uintptr_t id);
uintptr_t selected_agent();

void draw_detail_window();

// Called from the wndproc on ESC. Closes the detail window if open and
// returns true so the wndproc can swallow the press (otherwise GW2's
// game menu would also open). Returns false when already closed so ESC
// propagates to the game.
bool consume_esc_for_detail();

} // namespace idps
