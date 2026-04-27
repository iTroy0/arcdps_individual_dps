#pragma once

#include <sal.h>

namespace idps {

void log_init();
void log_shutdown();
void log_line(_Printf_format_string_ const char* fmt, ...);

} // namespace idps
