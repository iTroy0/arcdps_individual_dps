#include <cstdint>
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>

#include "exports.h"
#include "icons.h"
#include "log.h"
#include "ui.h"

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
    char* /*arcversion*/,
    ImGuiContext* imguictx,
    void* id3dptr,
    HANDLE /*arcdll*/,
    void* mallocfn,
    void* freefn,
    uint32_t d3dversion) {
    idps::log_init();
    // Shared-context ImGui: set allocator BEFORE SetCurrentContext so the
    // extension's ImGui copy allocates through the same arena as arcdps.
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
    if (id3dptr && d3dversion == 11) {
        auto* unk = static_cast<IUnknown*>(id3dptr);
        ID3D11Device* device = nullptr;
        if (SUCCEEDED(unk->QueryInterface(__uuidof(ID3D11Device),
                                          reinterpret_cast<void**>(&device)))) {
            idps::icons_init(device);
            device->Release();
        } else {
            IDXGISwapChain* swap = nullptr;
            if (SUCCEEDED(unk->QueryInterface(__uuidof(IDXGISwapChain),
                                              reinterpret_cast<void**>(&swap)))) {
                if (SUCCEEDED(swap->GetDevice(__uuidof(ID3D11Device),
                                              reinterpret_cast<void**>(&device)))) {
                    idps::icons_init(device);
                    device->Release();
                }
                swap->Release();
            }
        }
    }

    return reinterpret_cast<void*>(&idps::mod_init);
}

extern "C" __declspec(dllexport) void* get_release_addr() {
    return reinterpret_cast<void*>(&idps::mod_release);
}
