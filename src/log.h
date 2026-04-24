#pragma once

#include <string>

namespace idps {

void log_init();
void log_shutdown();
void log_line(const char* fmt, ...);

} // namespace idps
