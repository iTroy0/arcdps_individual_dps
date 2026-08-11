#pragma once

#include <cstdint>

namespace idps {

void draw_popup_settings();

// arcdps `options_windows` callback. arcdps walks its own window list and
// calls this once per entry, plus once with a null name — that null call is
// where an extension contributes its own windows, so they appear in arc's
// window menu next to arc's rather than only in this plugin's options tab.
uintptr_t mod_options_windows(const char* windowname);

} // namespace idps
