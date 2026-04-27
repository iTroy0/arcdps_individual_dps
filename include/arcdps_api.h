// Vendored arcdps API declarations from the arcdps evtc README and combat demo.

#pragma once

#include <cstdint>

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

enum cbtstatechange : uint8_t {
    CBTS_NONE              = 0,
    CBTS_ENTERCOMBAT       = 1,
    CBTS_EXITCOMBAT        = 2,
    CBTS_CHANGEUP          = 3,
    CBTS_CHANGEDEAD        = 4,
    CBTS_CHANGEDOWN        = 5,
    CBTS_SPAWN             = 6,
    CBTS_DESPAWN           = 7,
    CBTS_HEALTHPCTUPDATE   = 8,
    CBTS_SQCOMBATSTART     = 9,
    CBTS_SQCOMBATEND       = 10,
    CBTS_WEAPSWAP          = 11,
    CBTS_MAXHEALTHUPDATE   = 12,
    CBTS_POINTOFVIEW       = 13,
    CBTS_LANGUAGE          = 14,
    CBTS_GWBUILD           = 15,
    CBTS_SHARDID           = 16,
    CBTS_REWARD            = 17,
    CBTS_BUFFINITIAL       = 18,
    CBTS_POSITION          = 19,
    CBTS_VELOCITY          = 20,
    CBTS_FACING            = 21,
    CBTS_TEAMCHANGE        = 22,
    CBTS_ATTACKTARGET      = 23,
    CBTS_TARGETABLE        = 24,
    CBTS_MAPID             = 25,
    CBTS_REPLINFO          = 26,
    CBTS_STACKACTIVE       = 27,
    CBTS_STACKRESET        = 28,
    CBTS_GUILD             = 29,
    CBTS_BUFFINFO          = 30,
    CBTS_BUFFFORMULA       = 31,
    CBTS_SKILLINFO         = 32,
    CBTS_SKILLTIMING       = 33,
    CBTS_BREAKBARSTATE     = 34,
    CBTS_BREAKBARPERCENT   = 35,
    CBTS_INTEGRITY         = 36,
    CBTS_MARKER            = 37,
    CBTS_BARRIERPCTUPDATE  = 38,
    CBTS_STATRESET_DEFUNC  = 39,
    CBTS_EXTENSION         = 40,
    CBTS_APIDELAYED        = 41,
    CBTS_INSTANCESTART     = 42,
    CBTS_RATEHEALTH        = 43,
    CBTS_LAST90BEFOREDOWN  = 44,
    CBTS_EFFECT_DEFUNC     = 45,
    CBTS_IDTOGUID          = 46,
    CBTS_LOGNPCUPDATE      = 47,
    CBTS_IDLEEVENT         = 48,
    CBTS_EXTENSIONCOMBAT   = 49,
    CBTS_FRACTALSCALE      = 50,
    CBTS_EFFECT2_DEFUNC    = 51,
    CBTS_RULESET           = 52,
    CBTS_SQUADMARKER       = 53,
    CBTS_ARCBUILD          = 54,
    CBTS_GLIDER            = 55,
    CBTS_STUNBREAK         = 56,
    CBTS_MISSILECREATE     = 57,
    CBTS_MISSILELAUNCH     = 58,
    CBTS_MISSILEREMOVE     = 59,
    CBTS_EFFECTGROUNDCREATE = 60,
    CBTS_EFFECTGROUNDREMOVE = 61,
    CBTS_EFFECTAGENTCREATE = 62,
    CBTS_EFFECTAGENTREMOVE = 63,
    CBTS_IIDCHANGE         = 64,
    CBTS_MAPCHANGE         = 65,
    CBTS_UNKNOWN           = 66,
};

enum cbtresult : uint8_t {
    CBTR_NORMAL       = 0,
    CBTR_CRIT         = 1,
    CBTR_GLANCE       = 2,
    CBTR_BLOCK        = 3,
    CBTR_EVADE        = 4,
    CBTR_INTERRUPT    = 5,
    CBTR_ABSORB       = 6,
    CBTR_BLIND        = 7,
    CBTR_KILLINGBLOW  = 8,
    CBTR_DOWNED       = 9,
    CBTR_BREAKBAR     = 10,
    CBTR_ACTIVATION   = 11,
    CBTR_CROWDCONTROL = 12,
};

} // extern "C"
