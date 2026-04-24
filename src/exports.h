#pragma once

#include <cstdint>

#include "arcdps_api.h"

struct ImGuiContext;

namespace idps {

arcdps_exports* mod_init();
uintptr_t mod_release();

} // namespace idps
