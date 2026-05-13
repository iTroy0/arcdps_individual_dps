#pragma once

namespace idps {

// Returns a wchar_t* HTTPS URL pointing to the newest release DLL when a
// version newer than `current_version` is published on GitHub, or nullptr
// when up-to-date or unreachable. The returned buffer is statically owned
// and remains valid until the next call. Synchronous; bounded by ~2s.
const wchar_t* check_for_update(const char* current_version);

// Compares dotted-numeric version strings (e.g. "0.6.9" vs "0.7.0").
// Returns > 0 if a > b, 0 if equal, < 0 if a < b. Leading "v" tolerated.
// Non-numeric tails compare as zero. Missing trailing components default
// to zero, so "0.4" == "0.4.0".
int compare_semver(const char* a, const char* b);

} // namespace idps
