cmake_minimum_required(VERSION 3.20)
project(arcdps_individual_dps CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static-link the MSVC runtime so the DLL doesn't require end users to
# install a matching MSVCP140 / VCRUNTIME140 redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32)
    message(FATAL_ERROR "arcdps_individual_dps is Windows-only (MSVC x64).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Must build x64 — arcdps is 64-bit only.")
endif()

# ImGui must match the version arcdps ships. Override IMGUI_DIR if you have a
# local tree; otherwise FetchContent pulls the docking branch at a pinned tag.
set(IMGUI_DIR "" CACHE PATH "Path to local Dear ImGui source (matching arcdps version)")

include(FetchContent)

if(IMGUI_DIR)
    message(STATUS "Using local ImGui at ${IMGUI_DIR}")
    set(_imgui_src "${IMGUI_DIR}")
else()
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.80
    )
    FetchContent_MakeAvailable(imgui)
    set(_imgui_src "${imgui_SOURCE_DIR}")
endif()

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
)
FetchContent_MakeAvailable(stb)

add_library(imgui_static STATIC
    ${_imgui_src}/imgui.cpp
    ${_imgui_src}/imgui_draw.cpp
    ${_imgui_src}/imgui_tables.cpp
    ${_imgui_src}/imgui_widgets.cpp
)
target_include_directories(imgui_static PUBLIC ${_imgui_src})
target_compile_definitions(imgui_static PUBLIC
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

add_library(arcdps_individual_dps SHARED
    src/main.cpp
    src/exports.cpp
    src/combat.cpp
    src/tracker.cpp
    src/ui.cpp
    src/ui_common.cpp
    src/ui_detail.cpp
    src/ui_support.cpp
    src/ui_options.cpp
    src/log.cpp
    src/icons.cpp
    src/settings.cpp
    src/update.cpp
    res/icons.rc
)

set_target_properties(arcdps_individual_dps PROPERTIES
    OUTPUT_NAME "arcdps_individual_dps"
    PREFIX ""
)

target_include_directories(arcdps_individual_dps PRIVATE
    include
    src
    ${stb_SOURCE_DIR}
)

target_link_libraries(arcdps_individual_dps PRIVATE imgui_static d3d11 dxgi winhttp)

if(MSVC)
    target_compile_options(arcdps_individual_dps PRIVATE
        /W4 /permissive- /MP /Zc:__cplusplus
    )
    target_compile_definitions(arcdps_individual_dps PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    )
endif()
