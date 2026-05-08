#pragma once

#include <cstdint>

struct arcdps_exports;

namespace idps {

arcdps_exports* mod_init();
uintptr_t       mod_release();

const char* version();

} // namespace idps
