// Vendored arcdps API declarations.
//
// Transcribed from the upstream references, which are the authority for
// every value here:
//   https://www.deltaconnected.com/arcdps/evtc/README.txt
//   https://www.deltaconnected.com/arcdps/api/README.txt
//
// Enumerator ordering is load-bearing: arcdps sends raw integers in
// cbtevent::is_statechange / result / is_buffremove, so an enum that drifts
// from upstream silently mis-routes events. When re-syncing, diff the whole
// cbtstatechange block against the README rather than appending names.

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

// evtc file agent record. Not delivered over the realtime callback (which
// uses `ag` below) — kept for the prof/is_elite classification rules, which
// are identical in both directions.
typedef struct evtc_agent {
    uint64_t iid;
    uint32_t prof;
    uint32_t is_elite;
    int16_t  toughness;
    int16_t  concentration;
    int16_t  healing;
    uint16_t hitbox_width;
    int16_t  condition;
    uint16_t defunc;
    char     name[64];
} evtc_agent;

// Realtime agent record passed to the `combat` callback.
//
// On a tracking-add (ev == null, src->prof != 0) arcdps splits the player's
// identity across both structs, per the api README:
//   src->name = character name      dst->name = account name (':'-prefixed)
//   src->id   = agent id            dst->id   = instance id on map
//   src->team = team id             dst->team = subgroup
//                                   dst->prof / dst->elite / dst->self
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
    CBTS_COMBAT                    = 0,
    CBTS_ENTERCOMBAT               = 1,
    CBTS_EXITCOMBAT                = 2,
    CBTS_CHANGEUP                  = 3,
    CBTS_CHANGEDEAD                = 4,
    CBTS_CHANGEDOWN                = 5,
    CBTS_SPAWN                     = 6,
    CBTS_DESPAWN                   = 7,
    CBTS_HEALTHPCTUPDATE           = 8,
    CBTS_SQCOMBATSTART             = 9,
    CBTS_SQCOMBATEND               = 10,
    CBTS_WEAPSWAP                  = 11,
    CBTS_MAXHEALTHUPDATE           = 12,
    CBTS_POINTOFVIEW               = 13,
    CBTS_LANGUAGE                  = 14,
    CBTS_GWBUILD                   = 15,
    CBTS_SHARDID                   = 16,
    CBTS_REWARD                    = 17,
    CBTS_BUFFINITIAL               = 18,
    CBTS_POSITION                  = 19,
    CBTS_VELOCITY                  = 20,
    CBTS_FACING                    = 21,
    CBTS_TEAMCHANGE                = 22,
    CBTS_ATTACKTARGET              = 23,
    CBTS_TARGETABLE                = 24,
    CBTS_MAPID                     = 25,
    CBTS_REPLINFO                  = 26,
    CBTS_BUFFACTIVE                = 27,
    CBTS_BUFFDEACTIVE              = 28,
    CBTS_GUILD                     = 29,
    CBTS_BUFFINFO                  = 30,
    CBTS_BUFFFORMULA               = 31,
    CBTS_SKILLINFO                 = 32,
    CBTS_SKILLTIMING               = 33,
    CBTS_DEFIANCEBARSTATE          = 34,
    CBTS_DEFIANCEBARPERCENT        = 35,
    CBTS_INTEGRITY                 = 36,
    CBTS_MARKER                    = 37,
    CBTS_BARRIERPCTUPDATE          = 38,
    CBTS_STATRESET_DEFUNC          = 39,
    CBTS_EXTENSION                 = 40,
    CBTS_APIDELAYED_DEFUNC         = 41,
    CBTS_INSTANCESTART             = 42,
    CBTS_RATEHEALTH_DEFUNC         = 43,
    CBTS_LAST90BEFOREDOWN_DEFUNC   = 44,
    CBTS_EFFECT1_DEFUNC            = 45,
    CBTS_IDTOGUID                  = 46,
    CBTS_LOGNPCUPDATE              = 47,
    CBTS_IDLEEVENT                 = 48,
    CBTS_EXTENSIONCOMBAT           = 49,
    CBTS_FRACTALSCALE              = 50,
    CBTS_EFFECT2_DEFUNC            = 51,
    CBTS_RULESET                   = 52,
    CBTS_SQUADMARKER_GROUND        = 53,
    CBTS_ARCBUILD                  = 54,
    CBTS_GLIDER                    = 55,
    CBTS_STUNBREAK                 = 56,
    CBTS_MISSILECREATE             = 57,
    CBTS_MISSILELAUNCH             = 58,
    CBTS_MISSILEREMOVE             = 59,
    CBTS_EFFECTGROUNDCREATE        = 60,
    CBTS_EFFECTGROUNDREMOVE        = 61,
    CBTS_EFFECTAGENTCREATE         = 62,
    CBTS_EFFECTAGENTREMOVE         = 63,
    CBTS_IIDCHANGE                 = 64,
    CBTS_MAPCHANGE                 = 65,
    CBTS_EARLYEXIT                 = 66,
    CBTS_ANIMATIONSTART            = 67,
    CBTS_ANIMATIONSTOP             = 68,
    CBTS_BUFFAPPLY                 = 69,
    CBTS_BUFFCHANGE                = 70,
    CBTS_BUFFREMOVE_SINGLE         = 71,
    CBTS_BUFFREMOVE_ALL            = 72,
    CBTS_TRANSFORMATION            = 73,
    CBTS_WVWTEAMS                  = 74,
    CBTS_WVWOBJECTIVESTATUS        = 75,
    CBTS_STEALTHCHANGE             = 76,
    CBTS_GADGETANIMATION           = 77,
    CBTS_GADGETNAME                = 78,
    CBTS_MISSILEEFFECT             = 79,
    CBTS_GADGETCAPTUREOUTLINESHOW  = 80,
    CBTS_GADGETCAPTURESPLITPERCENT = 81,
    CBTS_GADGETCAPTUREOUTLINEHIDE  = 82,
    CBTS_GADGETCAPTUREOUTLINEPOINT = 83,
    CBTS_TICK                      = 84,
    CBTS_SQUADMARKER_AGENT         = 85,
    CBTS_UNKNOWN                   = 86,
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

// Reported in CBTS_ANIMATIONSTOP::is_activation.
enum cbtanimation : uint8_t {
    ACTV_NONE              = 0,
    ACTV_START_DEFUNC      = 1,
    ACTV_QUICKNESS_DEFUNC  = 2,
    ACTV_MINIMUM           = 3,
    ACTV_CANCEL            = 4,
    ACTV_RESET             = 5,
    ACTV_NODATA            = 6,
    ACTV_UNKNOWN           = 7,
};

// arcdps-synthesized skill ids. Damage/control events that the game does not
// attribute to a real skill are logged under one of these.
enum n_customskill : uint32_t {
    CSK_DODGE                   = 23275,
    CSK_DEFIANCEDAMAGE          = 23276,
    CSK_SELFCAST1               = 23277,
    CSK_ENEMYCAST1              = 23278,
    CSK_SELFCAST2               = 23279,
    CSK_ENEMYCAST2              = 23280,
    CSK_SELFCAST3               = 23281,
    CSK_ENEMYCAST3              = 23282,
    CSK_BREAKBAR_DEFUNC         = 23283,
    CSK_WEAPONDRAW              = 23284,
    CSK_WEAPONSTOW              = 23285,
    CSK_GENERICBLOCK            = 23286,
    CSK_GENERICDAMAGE           = 23287,
    CSK_GENERICKILL             = 23288,
    CSK_GENERICDOWN             = 23289,
    CSK_GENERICEVADE            = 23290,
    CSK_GENERICINTERRUPT        = 23291,
    CSK_GENERICABSORB           = 23292,
    CSK_GENERICMISS             = 23293,
    CSK_GENERICKNOCKDOWN        = 23294,
    CSK_GENERICKNOCKBACKPULL    = 23295,
    CSK_GENERICFLOATLAND        = 23296,
    CSK_GENERICLAUNCH           = 23297,
    CSK_GENERICWATERFLOATSINK_DEFUNC = 23298,
    CSK_GENERICCCBUFF           = 23299,
    CSK_GENERICSTAGGER          = 23300,
    CSK_GENERICINVALID          = 23301,
    CSK_GADGETINTERACT          = 23302,
    CSK_EMOTE                   = 23303,
    CSK_GENERICFLOATWATER       = 23304,
    CSK_GENERICSINK             = 23305,
    CSK_GENERICLOCKOUT          = 23306,
    CSK_GENERICFEAR             = 23307,
    CSK_PICKUP                  = 23308,
    CSK_GENERICFALLDOWN         = 23309,
};

// CBTS_IDTOGUID::overstack_value — what kind of content the guid names.
enum n_contentlocal : uint32_t {
    CONTENTLOCAL_EFFECT             = 0,
    CONTENTLOCAL_MARKER             = 1,
    CONTENTLOCAL_SKILL              = 2,
    CONTENTLOCAL_SPECIES_NOT_GADGET = 3,
    CONTENTLOCAL_EMOTE              = 4,
    CONTENTLOCAL_TRANSFORMATION     = 5,
};

// Passed to get_release_addr as the unload reason.
enum n_arcdpsextensionload : uint32_t {
    ARCDPSEXTENLOAD_OK                            = 0,
    ARCDPSEXTENLOAD_NO_SIG                        = 1,
    ARCDPSEXTENLOAD_INVALID_IMGUI                 = 2,
    ARCDPSEXTENLOAD_OBSOLETE                      = 3,
    ARCDPSEXTENLOAD_ALREADY_LOADED                = 4,
    ARCDPSEXTENLOAD_NO_FUNCTION_TABLE_RETURNED    = 5,
    ARCDPSEXTENLOAD_NO_INIT_FUNCTION_RETURNED     = 6,
    ARCDPSEXTENLOAD_LOADLIBRARY_ERROR             = 7,
    ARCDPSEXTENLOAD_NO_SLOTS_LEFT                 = 8,
    ARCDPSEXTENLOAD_MISSING_GET_RELEASE_ADDR      = 9,
    ARCDPSEXTENLOAD_SHUTDOWN                      = 10,
    ARCDPSEXTENLOAD_REMOVE_VIA_EXPORT             = 11,
};

// Index into the `e5` core colour array (out[0]).
enum n_colours_core : uint32_t {
    CCOL_TRANSPARENT = 0,
    CCOL_WHITE       = 1,
    CCOL_LWHITE      = 2,
    CCOL_LGREY       = 3,
    CCOL_LYELLOW     = 4,
    CCOL_LGREEN      = 5,
    CCOL_LRED        = 6,
    CCOL_LTEAL       = 7,
    CCOL_MGREY       = 8,
    CCOL_DGREY       = 9,
    CCOL_NUM         = 10,
};

// Bit positions in the `e6` UI-settings mask.
enum n_uisettings : uint32_t {
    UI_HIDDEN        = 0,
    UI_DRAW_ALWAYS   = 1,
    UI_MODLOCK_MOVE  = 2,
    UI_MODLOCK_CLICK = 3,
    UI_CLOSE_WITH_ESC = 4,
};

} // extern "C"
