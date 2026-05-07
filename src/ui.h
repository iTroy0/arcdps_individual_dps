#pragma once

#include <cstdint>

struct ImGuiContext;

namespace idps {

void ui_init(ImGuiContext* ctx);
void mod_imgui(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc);
void mod_options_end();

} // namespace idps
