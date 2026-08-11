#include <cstdint>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <wrl/client.h>

#include "arc_exports.h"
#include "exports.h"
#include "icons.h"
#include "log.h"
#include "ui.h"
#include "update.h"

using Microsoft::WRL::ComPtr;

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
        case DLL_PROCESS_DETACH:
        default:
            break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void* get_init_addr(
    char* arcversion,
    ImGuiContext* imguictx,
    void* id3dptr,
    HANDLE arcdll,
    void* mallocfn,
    void* freefn,
    uint32_t /*imguiversion*/) {
    idps::log_init();
    // arcdll is the arcdps module itself. Binding it unlocks arc's helper
    // exports — most importantly e5, whose profession colour tables we use
    // so rows match arcdps's own palette exactly instead of approximating it.
    idps::arc_bind(arcdll);
    idps::arc_log("individual_dps: loaded, arc build %s",
                  arcversion ? arcversion : "?");
    // Set allocator before SetCurrentContext so plugin's ImGui copy
    // allocates through arcdps's arena.
    if (mallocfn && freefn) {
        using alloc_fn = void* (*)(size_t sz, void* user_data);
        using free_fn  = void  (*)(void* ptr, void* user_data);
        ImGui::SetAllocatorFunctions(
            reinterpret_cast<alloc_fn>(mallocfn),
            reinterpret_cast<free_fn>(freefn));
    }
    idps::ui_init(imguictx);

    // arcdps passes id3dptr either as ID3D11Device* (older) or IDXGISwapChain*
    // (newer) — probe via QueryInterface.
    if (id3dptr) {
        auto* unk = static_cast<IUnknown*>(id3dptr);
        ComPtr<ID3D11Device> device;
        if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&device)))) {
            idps::icons_set_device(device.Get());
        } else {
            ComPtr<IDXGISwapChain> swap;
            if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&swap)))) {
                if (SUCCEEDED(swap->GetDevice(IID_PPV_ARGS(&device)))) {
                    idps::icons_set_device(device.Get());
                }
            }
        }
    }

    return reinterpret_cast<void*>(&idps::mod_init);
}

extern "C" __declspec(dllexport) void* get_release_addr(uint32_t /*reason*/) {
    // reason is of enum n_arcdpsextensionload (see references/README_API.md)
    // — the unload cause. Not acted on; mod_release tears down regardless.
    return reinterpret_cast<void*>(&idps::mod_release);
}

// arcdps optional export: when non-null, arc free-libraries the plugin,
// downloads the file at the returned URL (HTTPS, port 443 only), writes
// it over the existing DLL, and re-loads the new module in-process.
extern "C" __declspec(dllexport) const wchar_t* get_update_url() {
    return idps::check_for_update(idps::version());
}
