#pragma once

#include <cstdint>

namespace idps {

void      set_selected_agent(uintptr_t id);
uintptr_t selected_agent();

void draw_detail_window();

} // namespace idps
