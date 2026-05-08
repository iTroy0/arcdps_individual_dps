#pragma once

#include <cstdint>

namespace idps {

void      set_selected_agent(uintptr_t id);
uintptr_t selected_agent();

void draw_detail_window();

// Called from the wndproc callback (mod_wnd_filter) when ESC is pressed.
// Closes the detail window if it's currently open and returns true so
// the wndproc can consume the message (preventing GW2's game menu from
// also opening on the same press). Returns false otherwise so ESC
// propagates normally to the game.
bool consume_esc_for_detail();

} // namespace idps
