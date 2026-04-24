#pragma once

#include <cstdint>

struct ImGuiContext;

namespace idps {

void ui_init(ImGuiContext* ctx);
uintptr_t mod_imgui(uint32_t not_charsel_or_loading);
uintptr_t mod_wnd_nofilter(void* hwnd, uint32_t umsg, uintptr_t wparam, intptr_t lparam);
uintptr_t mod_options_end();

} // namespace idps
