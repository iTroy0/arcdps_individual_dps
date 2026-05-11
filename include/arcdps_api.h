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
    const char* name;
    uintptr_t   id;
    uint32_t    prof;
    uint32_t    elite;
    uint32_t    self;
    uint16_t    team;
} ag;

typedef struct arcdps_exports {
    uint64_t    size;
    uint32_t    sig;
    uint32_t    imguivers;
    const char* out_name;
    const char* out_build;
    void*       wnd_nofilter;
    void*       combat;
    void*       imgui;
    void*       options_tab;
    void*       combat_local;
    void*       wnd_filter;
    void*       options_windows;
} arcdps_exports;

enum cbtstatechange : uint8_t {
    CBTS_COMBAT                  = 0,
    CBTS_ENTERCOMBAT             = 1,
    CBTS_EXITCOMBAT              = 2,
    CBTS_CHANGEUP                = 3,
    CBTS_CHANGEDEAD              = 4,
    CBTS_CHANGEDOWN              = 5,
    CBTS_SPAWN                   = 6,
    CBTS_DESPAWN                 = 7,
    CBTS_HEALTHPCTUPDATE         = 8,
    CBTS_SQCOMBATSTART           = 9,
    CBTS_SQCOMBATEND             = 10,
    CBTS_WEAPSWAP                = 11,
    CBTS_MAXHEALTHUPDATE         = 12,
    CBTS_POINTOFVIEW             = 13,
    CBTS_LANGUAGE                = 14,
    CBTS_GWBUILD                 = 15,
    CBTS_SHARDID                 = 16,
    CBTS_REWARD                  = 17,
    CBTS_BUFFINITIAL             = 18,
    CBTS_POSITION                = 19,
    CBTS_VELOCITY                = 20,
    CBTS_FACING                  = 21,
    CBTS_TEAMCHANGE              = 22,
    CBTS_ATTACKTARGET            = 23,
    CBTS_TARGETABLE              = 24,
    CBTS_MAPID                   = 25,
    CBTS_REPLINFO                = 26,
    CBTS_BUFFACTIVE              = 27,
    CBTS_BUFFDEACTIVE            = 28,
    CBTS_GUILD                   = 29,
    CBTS_BUFFINFO                = 30,
    CBTS_BUFFFORMULA             = 31,
    CBTS_SKILLINFO               = 32,
    CBTS_SKILLTIMING             = 33,
    CBTS_DEFIANCEBARSTATE        = 34,
    CBTS_DEFIANCEBARPERCENT      = 35,
    CBTS_INTEGRITY               = 36,
    CBTS_MARKER                  = 37,
    CBTS_BARRIERPCTUPDATE        = 38,
    CBTS_STATRESET_DEFUNC        = 39,
    CBTS_EXTENSION               = 40,
    CBTS_APIDELAYED_DEFUNC       = 41,
    CBTS_INSTANCESTART           = 42,
    CBTS_RATEHEALTH              = 43,
    CBTS_LAST90BEFOREDOWN_DEFUNC = 44,
    CBTS_EFFECT1_DEFUNC          = 45,
    CBTS_IDTOGUID                = 46,
    CBTS_LOGNPCUPDATE            = 47,
    CBTS_IDLEEVENT               = 48,
    CBTS_EXTENSIONCOMBAT         = 49,
    CBTS_FRACTALSCALE            = 50,
    CBTS_EFFECT2_DEFUNC          = 51,
    CBTS_RULESET                 = 52,
    CBTS_SQUADMARKER             = 53,
    CBTS_ARCBUILD                = 54,
    CBTS_GLIDER                  = 55,
    CBTS_STUNBREAK               = 56,
    CBTS_MISSILECREATE           = 57,
    CBTS_MISSILELAUNCH           = 58,
    CBTS_MISSILEREMOVE           = 59,
    CBTS_EFFECTGROUNDCREATE      = 60,
    CBTS_EFFECTGROUNDREMOVE      = 61,
    CBTS_EFFECTAGENTCREATE       = 62,
    CBTS_EFFECTAGENTREMOVE       = 63,
    CBTS_IIDCHANGE               = 64,
    CBTS_MAPCHANGE               = 65,
    CBTS_EARLYEXIT               = 66,
    CBTS_ANIMATIONSTART          = 67,
    CBTS_ANIMATIONSTOP           = 68,
    CBTS_BUFFAPPLY               = 69,
    CBTS_BUFFCHANGE              = 70,
    CBTS_BUFFREMOVE_SINGLE       = 71,
    CBTS_BUFFREMOVE_ALL          = 72,
    CBTS_TRANSFORMATION          = 73,
    CBTS_UNKNOWN                 = 74,
};

enum iff : uint8_t {
    IFF_FRIEND  = 0,
    IFF_FOE     = 1,
    IFF_UNKNOWN = 2,
};

enum cbtresult : uint8_t {
    CBTR_STRIKE_DAMAGENORMAL                         = 0,
    CBTR_STRIKE_DAMAGECRIT                           = 1,
    CBTR_STRIKE_DAMAGEGLANCE                         = 2,
    CBTR_BLOCK                                       = 3,
    CBTR_EVADE                                       = 4,
    CBTR_INTERRUPT                                   = 5,
    CBTR_ABSORB                                      = 6,
    CBTR_BLIND                                       = 7,
    CBTR_KILLINGBLOW                                 = 8,
    CBTR_DOWNED                                      = 9,
    CBTR_DEFIANCE_DAMAGENORMAL                       = 10,
    CBTR_SKILLCAST                                   = 11,
    CBTR_CROWDCONTROL                                = 12,
    CBTR_INVERT                                      = 13,
    CBTR_BUFF_DAMAGECYCLE                            = 14,
    CBTR_BUFF_DAMAGENOTCYCLE                         = 15,
    CBTR_BUFF_DAMAGENOTCYCLEDMGTOTARGETONHIT         = 16,
    CBTR_BUFF_DAMAGENOTCYCLEDMGTOSOURCEONHIT         = 17,
    CBTR_BUFF_DAMAGENOTCYCLEDMGTOTARGETONSTACKREMOVE = 18,
    CBTR_UNKNOWN                                     = 19,
};

enum cbtbuffremove : uint8_t {
    CBTB_NONE    = 0,
    CBTB_ALL     = 1,
    CBTB_SINGLE  = 2,
    CBTB_MANUAL  = 3,
    CBTB_UNKNOWN = 4,
};

} // extern "C"
