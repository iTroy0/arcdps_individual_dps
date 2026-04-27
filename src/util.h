#pragma once

#include <windows.h>

namespace idps {

inline HMODULE self_module() {
    HMODULE self = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&self_module), &self);
    return self;
}

} // namespace idps
