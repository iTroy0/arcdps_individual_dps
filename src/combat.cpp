#include "combat.h"

#include "tracker.h"

namespace idps {

uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst,
                     const char* skillname, uint64_t id, uint64_t revision) {
    tracker().on_combat(ev, src, dst, skillname, id, revision);
    return 0;
}

uintptr_t mod_combat_local(cbtevent* ev, ag* src, ag* dst,
                           const char* skillname, uint64_t id, uint64_t revision) {
    tracker().on_combat_local(ev, src, dst, skillname, id, revision);
    return 0;
}

} // namespace idps
