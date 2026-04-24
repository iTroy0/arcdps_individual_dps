#pragma once

#include <cstdint>
#include <string>

struct ID3D11Device;

namespace idps {

// Call once after get_init_addr with arc's ID3D11Device*.
// Loads 001.png..009.png + e101.png..e904.png from the icons directory.
// icons_dir defaults to "<DLL folder>/individual_dps_icons".
void icons_init(ID3D11Device* device, const std::string& icons_dir = "");

void icons_shutdown();

// Returns an ImGui-compatible texture id (ID3D11ShaderResourceView*) or 0
// if the icon isn't loaded. Falls back through elite -> prof.
uint64_t icon_for(uint32_t prof, uint32_t elite);

} // namespace idps
