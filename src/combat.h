#pragma once

#include <cstdint>

#include "arcdps_api.h"

namespace idps {

void mod_combat(cbtevent* ev, ag* src, ag* dst,
                const char* skillname, uint64_t id, uint64_t revision);

} // namespace idps
