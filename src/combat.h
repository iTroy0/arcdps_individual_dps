#pragma once

#include <cstdint>

#include "arcdps_api.h"

namespace idps {

uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst,
                     const char* skillname, uint64_t id, uint64_t revision);
uintptr_t mod_combat_local(cbtevent* ev, ag* src, ag* dst,
                           const char* skillname, uint64_t id, uint64_t revision);

} // namespace idps
