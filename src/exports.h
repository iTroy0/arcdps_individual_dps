#pragma once

#include <cstdint>

struct arcdps_exports;

namespace idps {

arcdps_exports* mod_init();
uintptr_t       mod_release();

const char* version();

// One-shot post-update banner. Set true on the first launch where the
// kVersion baked into this DLL is strictly newer than the version
// persisted in the ini, i.e. arc just swapped us in via get_update_url.
// The banner clears on dismiss; the ini baseline is already persisted at
// detection time so it does not fire again for the same upgrade.
bool        update_banner_visible();
const char* update_banner_prev_version();
void        update_banner_dismiss();

} // namespace idps
