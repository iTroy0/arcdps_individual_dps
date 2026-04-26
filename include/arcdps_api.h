// Minimal arcdps extension API.
// Vendored/derived from deltaconnected.com/arcdps/api/arcdps_combatdemo.cpp.
// Verify statechange numeric values against current arcdps header — the
// April 14, 2026 update removed CBTS_STATRESET and some values may have shifted.

#pragma once

#include <cstdint>
#include <windows.h>

extern "C" {

typedef struct cbtevent {
    uint64_t time;
    uint64_t src_agent;
    uint64_t dst_agent;
    int32_t  value;
    int32_t  buff_dmg;
    uint32_t overstack_value;
    uint32_t skillid;
    uint16_t src_instid;
    uint16_t dst_instid;
    uint16_t src_master_instid;
    uint16_t dst_master_instid;
    uint8_t  iff;
    uint8_t  buff;
    uint8_t  result;
    uint8_t  is_activation;
    uint8_t  is_buffremove;
    uint8_t  is_ninety;
    uint8_t  is_fifty;
    uint8_t  is_moving;
    uint8_t  is_statechange;
    uint8_t  is_flanking;
    uint8_t  is_shields;
    uint8_t  is_offcycle;
    uint8_t  pad61;
    uint8_t  pad62;
    uint8_t  pad63;
    uint8_t  pad64;
} cbtevent;

typedef struct ag {
    char*     name;
    uintptr_t id;
    uint32_t  prof;
    uint32_t  elite;
    uint32_t  self;
    uint16_t  team;
} ag;

typedef struct arcdps_exports {
    uintptr_t   size;
    uint32_t    sig;
    uint32_t    imguivers;
    const char* out_name;
    const char* out_build;
    void*       wnd_nofilter;
    void*       combat;
    void*       imgui;
    void*       options_end;
    void*       combat_local;
    void*       wnd_filter;
    void*       options_windows;
} arcdps_exports;

// Statechange enum subset. Numeric values MUST be verified against current
// arcdps header in-game before trusting. Known stable bases shown.
//
// Observed in current arc stream but not yet mapped here (seen via timing
// diagnostics in v0.4.0): codes 11, 18, 27, 40, 41, 49. Code 41 in particular
// is fired in a delayed flush at LOGEND with very large lag (~20s+),
// suggesting post-fight aggregated stats. Names are not yet known — do not
// guess; default-skip in switches keeps current behavior safe. Update this
// enum when deltaconnected publishes the current arcdps_combat header.
enum cbtstatechange : uint8_t {
    CBTS_NONE          = 0,
    CBTS_ENTERCOMBAT   = 1,
    CBTS_EXITCOMBAT    = 2,
    CBTS_CHANGEUP      = 3,
    CBTS_CHANGEDEAD    = 4,
    CBTS_CHANGEDOWN    = 5,
    CBTS_LOGSTART      = 9,
    CBTS_LOGEND        = 10,
};

// Result values for physical hits. IFF codes: 0 friend, 1 foe, 2 unknown.
enum cbtresult : uint8_t {
    CBTR_NORMAL     = 0,
    CBTR_CRIT       = 1,
    CBTR_GLANCE     = 2,
    CBTR_BLOCK      = 3,
    CBTR_EVADE      = 4,
    CBTR_INTERRUPT  = 5,
    CBTR_ABSORB     = 6,
    CBTR_BLIND      = 7,
    CBTR_KILLINGBLOW= 8,
    CBTR_DOWNED     = 9,
};

} // extern "C"
