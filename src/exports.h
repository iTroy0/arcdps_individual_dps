#pragma once

#include <cstdint>

struct arcdps_exports;

namespace idps {

arcdps_exports* mod_init();
uintptr_t       mod_release();

// Centralized version string — used by exports table AND options panel.
const char* version();

} // namespace idps
