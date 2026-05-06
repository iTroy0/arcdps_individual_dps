#pragma once

namespace idps {

// Returns a wchar_t* HTTPS URL pointing to the newest release DLL when a
// version newer than `current_version` is published on GitHub, or nullptr
// when up-to-date or unreachable. The returned buffer is statically owned
// and remains valid until the next call. Synchronous; bounded by ~2s.
const wchar_t* check_for_update(const char* current_version);

} // namespace idps
