#pragma once

#include <cstdint>
#include <string>

struct ID3D11Device;

namespace idps {

// Stash the device pointer at get_init_addr. AddRef so the device stays
// alive until icons_shutdown(). Does NOT create any d3d resources — running
// CreateTexture2D / CreateShaderResourceView this early can crash inside
// the d3d driver on some setups (NVIDIA seen). Actual SRV creation is
// deferred to the first icons_ensure_loaded() call from the render thread.
void icons_set_device(ID3D11Device* device);

// Lazily load PNG icons into d3d SRVs. Safe to call every frame; the work
// runs once and is a no-op afterwards. Must be called from the ImGui render
// thread (mod_imgui) so d3d state is fully initialized.
void icons_ensure_loaded(const std::string& icons_dir = "");

void icons_shutdown();

// Returns an ImGui-compatible texture id (ID3D11ShaderResourceView*) or 0
// if the icon isn't loaded. Falls back through elite -> prof.
uint64_t icon_for(uint32_t prof, uint32_t elite);

} // namespace idps
