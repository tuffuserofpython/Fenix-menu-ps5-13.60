// Menu injetado no GTA V (PPSA03420 v01.011.001) - paridade com o SimpleMenu.
//
// Restricoes (nao mudar sem checar llvm-objdump -r):
//   * ZERO relocacoes -> nada de tabela de ponteiros de funcao/string; tudo switch.
//   * ZERO imports -> sem libc.
//   * Position independent -> so RIP-relativo.
#include <stdint.h>
#include "rvas.h"

#define GBASE 0x400000ULL

struct NativeCtx {
    void*    pReturn;
    uint32_t nArgCount;
    uint32_t _p0;
    void*    pArgs;
    uint32_t nDataCount;
    uint32_t _p1;
    uint64_t ret[8];
    uint64_t arg[32];
};
typedef void (*NativeFn)(NativeCtx*);

extern "C" {
volatile uint64_t g_origRun   = 0;
volatile uint64_t g_hits      = 0;
volatile uint64_t g_menuOpen  = 0;
// campos de diagnostico, lidos de fora por TCP
volatile uint64_t g_dbgPed    = 0;
volatile uint64_t g_dbgAction = 0;
volatile uint64_t g_dbgCalls  = 0;
volatile uint64_t g_dbgTpX    = 0;
volatile uint64_t g_dbgPage   = 0;
}

static uint32_t g_lastFrame = 0xFFFFFFFFu;

// ------------------------------------------------------------------ strings
static void Zero(void* p, uint64_t n) { uint8_t* d = (uint8_t*)p; for (uint64_t i = 0; i < n; ++i) d[i] = 0; }
static char* SCat(char* d, char* e, const char* s) { while (*s && d < e - 1) *d++ = *s++; *d = 0; return d; }
static char* ICat(char* d, char* e, int64_t v) {
    if (v < 0) { if (d < e - 1) *d++ = '-'; v = -v; }
    char t[24]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n && d < e - 1) *d++ = t[--n];
    *d = 0; return d;
}
static char g_buf[96];
static const char* Lbl(const char* name, int64_t v) {
    char* e = g_buf + sizeof(g_buf);
    char* p = SCat(g_buf, e, name);
    p = SCat(p, e, ": ");
    ICat(p, e, v);
    return g_buf;
}
static const char* LblS(const char* name, const char* v) {
    char* e = g_buf + sizeof(g_buf);
    char* p = SCat(g_buf, e, name);
    p = SCat(p, e, ": ");
    SCat(p, e, v);
    return g_buf;
}
static const char* OnOff(const char* name, bool b) { return LblS(name, b ? "ON" : "OFF"); }

// ------------------------------------------------------------- chamar native
static NativeCtx g_ctx;
static inline void Prep() {
    Zero(&g_ctx, sizeof(g_ctx));
    g_ctx.pReturn = g_ctx.ret;
    g_ctx.pArgs   = g_ctx.arg;
}
static inline void PI(int64_t v)        { g_ctx.arg[g_ctx.nArgCount++] = (uint64_t)v; }
static inline void PP(const void* p)    { g_ctx.arg[g_ctx.nArgCount++] = (uint64_t)p; }
static inline void PF(float f)          { uint32_t b; __builtin_memcpy(&b, &f, 4); g_ctx.arg[g_ctx.nArgCount++] = b; }
static inline void Fire(uint64_t rva)   { ++g_dbgCalls; ((NativeFn)(GBASE + rva))(&g_ctx); }
static inline uint32_t R32()            { return (uint32_t)g_ctx.ret[0]; }
static inline float RF()                { float f; uint32_t b = (uint32_t)g_ctx.ret[0]; __builtin_memcpy(&f, &b, 4); return f; }

static int Ped()  { Prep(); Fire(RVA_PLAYER_PED_ID); return (int)R32(); }
static int Plr()  { Prep(); Fire(RVA_PLAYER_ID);     return (int)R32(); }
static uint32_t Hash(const char* s) { Prep(); PP(s); Fire(RVA_GET_HASH_KEY); return R32(); }
// GET_VEHICLE_PED_IS_IN devolve um handle velho (nao zero) mesmo a pe neste build,
// entao so confiamos nele depois do IS_PED_IN_ANY_VEHICLE.
static int Veh() {
    int p = Ped();
    Prep(); PI(p); PI(0); Fire(RVA_IS_PED_IN_ANY_VEHICLE);
    if (!R32()) return 0;
    Prep(); PI(p); PI(0); Fire(RVA_GET_VEHICLE_PED_IS_IN);
    return (int)R32();
}

struct V3 { float x, y, z; };
static V3 Coords(int ent) {
    Prep(); PI(ent); PI(1); Fire(RVA_GET_ENTITY_COORDS);
    float* f = (float*)g_ctx.ret;
    V3 v; v.x = f[0]; v.y = f[2]; v.z = f[4]; return v;
}

static bool Held(int c)   { Prep(); PI(0); PI(c); Fire(RVA_IS_DISABLED_CONTROL_PRESSED);      return R32() != 0; }
static bool Tap(int c)    { Prep(); PI(0); PI(c); Fire(RVA_IS_DISABLED_CONTROL_JUST_PRESSED); return R32() != 0; }
static void Block(int c)  { Prep(); PI(0); PI(c); PI(1); Fire(RVA_DISABLE_CONTROL_ACTION); }

static void Rect(float x, float y, float w, float h, int r, int g, int b, int a) {
    Prep(); PF(x); PF(y); PF(w); PF(h); PI(r); PI(g); PI(b); PI(a); PI(0); Fire(RVA_DRAW_RECT);
}
static void Txt(const char* s, float x, float y, float sc, int r, int g, int b, bool mid) {
    Prep(); PI(0);                                 Fire(RVA_SET_TEXT_FONT);
    Prep(); PF(0.0f); PF(sc);                      Fire(RVA_SET_TEXT_SCALE);
    Prep(); PI(r); PI(g); PI(b); PI(255);          Fire(RVA_SET_TEXT_COLOUR);
    Prep(); PI(mid ? 1 : 0);                       Fire(RVA_SET_TEXT_CENTRE);
    Prep();                                        Fire(RVA_SET_TEXT_DROP_SHADOW);
    Prep(); PP("STRING");                          Fire(RVA_BEGIN_TEXT_COMMAND_DISPLAY_TEXT);
    Prep(); PP(s);                                 Fire(RVA_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME);
    Prep(); PF(x); PF(y); PI(0);                   Fire(RVA_END_TEXT_COMMAND_DISPLAY_TEXT);
}

enum { C_DOWN = 187, C_UP = 188, C_LEFT = 189, C_RIGHT = 190,
       C_ACCEPT = 201, C_CANCEL = 202, C_R1 = 206, C_R2 = 208, C_ATTACK = 24, C_AIM = 25 };

// --------------------------------------------------------------------- flags
static bool tGod, tInfAbility, tShowStats, tInvis;
static bool tSuperPunch, tJump, tRun, tSwim, tNoRagdoll, tDrunk;
static bool tNight, tThermal, tBlur, tBars, tShake;
static bool tSuspension, tVehGod, tTurbo, tStick, tAutoRepair, tAutoFlip, tEngineOn, tNoEject;
static bool tBulletTires, tNeon;
static bool tInfAmmo, tFreezeAmmo, tExplMelee, tFireAmmo, tAccuracy, tRecoil;
static bool tNeverWanted, tFreezeWanted, tDispatch = true, tPoliceIgnore, tNoReports;
static bool tFreezeTime, tIgnorePlayer, tChaos;
static bool tFirstPerson, tSpeedo, tCoords, tHud = true, tRadar = true, tAreaName = true;
static bool tGuardGod, tMpMaps;
static bool tVehWeapon, tPedWeapon, tTpWeapon;
static bool tSeatbelt;

static int vTint, vPrimary, vSecondary, vNeonCol, vWheelType, vFrontWheel, vSpoiler, vBumper, vExhaust;
static int vHour = 12, vMinute = 0, vWeather, vTimeOfDay, vWanted, vTraffic = 10, vPeds = 10;

static const char* WeatherName(int i) {
    switch (i & 7) {
        case 0: return "CLEAR";      case 1: return "EXTRASUNNY"; case 2: return "CLOUDS";
        case 3: return "OVERCAST";   case 4: return "RAIN";       case 5: return "THUNDER";
        case 6: return "FOGGY";      default: return "XMAS";
    }
}

// --------------------------------------------------------------------- paginas
enum { P_MAIN = 0, P_PLAYER, P_ABIL, P_VISION, P_VEHICLE, P_TUNING, P_VPROPS, P_VFUNC,
       P_SPAWNV, P_WEAPON, P_CUSTW, P_WANTED, P_TP, P_LAND, P_IMPT, P_WORLD, P_NPC,
       P_MISC, P_HUD, P_GUARD, P_MPMAP, P_ABOUT, P_COUNT };

static int Count(int p) {
    switch (p) {
        case P_MAIN:    return 12;
        case P_PLAYER:  return 9;
        case P_ABIL:    return 7;
        case P_VISION:  return 6;
        case P_VEHICLE: return 9;
        case P_TUNING:  return 13;
        case P_VPROPS:  return 7;
        case P_VFUNC:   return 5;
        case P_SPAWNV:  return 9;
        case P_WEAPON:  return 11;
        case P_CUSTW:   return 4;
        case P_WANTED:  return 8;
        case P_TP:      return 5;
        case P_LAND:    return 8;
        case P_IMPT:    return 6;
        case P_WORLD:   return 8;
        case P_NPC:     return 5;
        case P_MISC:    return 6;
        case P_HUD:     return 4;
        case P_GUARD:   return 4;
        case P_MPMAP:   return 2;
        case P_ABOUT:   return 6;
    }
    return 0;
}

static const char* Title(int p) {
    switch (p) {
        case P_MAIN:    return "SimpleMenu PS5";
        case P_PLAYER:  return "Player Settings";
        case P_ABIL:    return "Abilities";
        case P_VISION:  return "Player Vision";
        case P_VEHICLE: return "Vehicle Settings";
        case P_TUNING:  return "Car Tuning";
        case P_VPROPS:  return "Utility Features";
        case P_VFUNC:   return "Vehicle Functions";
        case P_SPAWNV:  return "Spawn Vehicle";
        case P_WEAPON:  return "Weapon Settings";
        case P_CUSTW:   return "Custom Weapons";
        case P_WANTED:  return "Wanted Level";
        case P_TP:      return "Teleport";
        case P_LAND:    return "Landmarks";
        case P_IMPT:    return "Important Teleports";
        case P_WORLD:   return "World Settings";
        case P_NPC:     return "NPC Settings";
        case P_MISC:    return "Misc Options";
        case P_HUD:     return "HUD Options";
        case P_GUARD:   return "Bodyguards";
        case P_MPMAP:   return "MP Maps";
        case P_ABOUT:   return "About";
    }
    return "?";
}

static const char* Item(int p, int i) {
    switch (p) {
    case P_MAIN: switch (i) {
        case 0: return "Player Settings  >";  case 1: return "Vehicle Settings  >";
        case 2: return "Weapon Settings  >";  case 3: return "Wanted Level  >";
        case 4: return "Teleport  >";         case 5: return "World Settings  >";
        case 6: return "NPC Settings  >";     case 7: return "Misc Options  >";
        case 8: return "HUD Options  >";      case 9: return "Bodyguards  >";
        case 10: return "MP Maps  >";         case 11: return "About  >"; } break;
    case P_PLAYER: switch (i) {
        case 0: return OnOff("God Mode", tGod);
        case 1: return OnOff("Infinite Ability", tInfAbility);
        case 2: return "Heal Player";
        case 3: return "Set Max Health & Armor";
        case 4: return OnOff("Display Health & Armor", tShowStats);
        case 5: return OnOff("Invisible Player", tInvis);
        case 6: return "Abilities  >";
        case 7: return "Player Vision  >";
        case 8: return "< Back"; } break;
    case P_ABIL: switch (i) {
        case 0: return OnOff("Super Punch", tSuperPunch);
        case 1: return OnOff("Super Jump", tJump);
        case 2: return OnOff("Fast Run", tRun);
        case 3: return OnOff("Fast Swim", tSwim);
        case 4: return OnOff("Disable Ragdoll", tNoRagdoll);
        case 5: return OnOff("Drunk Mode", tDrunk);
        case 6: return "< Back"; } break;
    case P_VISION: switch (i) {
        case 0: return OnOff("Night Vision", tNight);
        case 1: return OnOff("Thermal Vision", tThermal);
        case 2: return OnOff("Motion Blur", tBlur);
        case 3: return OnOff("Cinematic Black Bars", tBars);
        case 4: return OnOff("Camera Shake", tShake);
        case 5: return "< Back"; } break;
    case P_VEHICLE: switch (i) {
        case 0: return OnOff("GTA IV Suspension", tSuspension);
        case 1: return OnOff("Vehicle God Mode", tVehGod);
        case 2: return "Repair Vehicle";
        case 3: return "Destroy Vehicle Engine";
        case 4: return "Car Tuning  >";
        case 5: return "Utility Features  >";
        case 6: return "Vehicle Functions  >";
        case 7: return "Spawn Vehicle  >";
        case 8: return "< Back"; } break;
    case P_TUNING: switch (i) {
        case 0: return "Max Upgrade Vehicle";
        case 1: return OnOff("Bulletproof Tires", tBulletTires);
        case 2: return Lbl("Window Tint", vTint);
        case 3: return Lbl("Primary Color", vPrimary);
        case 4: return Lbl("Secondary Color", vSecondary);
        case 5: return OnOff("Neon Lights", tNeon);
        case 6: return Lbl("Neon Color", vNeonCol);
        case 7: return Lbl("Wheel Type", vWheelType);
        case 8: return Lbl("Front Wheels", vFrontWheel);
        case 9: return Lbl("Spoiler Mod", vSpoiler);
        case 10: return Lbl("Front Bumper Mod", vBumper);
        case 11: return Lbl("Exhaust Mod", vExhaust);
        case 12: return "< Back"; } break;
    case P_VPROPS: switch (i) {
        case 0: return OnOff("Turbo Boost", tTurbo);
        case 1: return OnOff("Stick to Ground", tStick);
        case 2: return OnOff("Auto Repair", tAutoRepair);
        case 3: return OnOff("Auto Flip", tAutoFlip);
        case 4: return OnOff("Engine Always On", tEngineOn);
        case 5: return OnOff("Prevent Ejection", tNoEject);
        case 6: return "< Back"; } break;
    case P_VFUNC: switch (i) {
        case 0: return "Toggle Engine";
        case 1: return "Lock/Unlock Doors";
        case 2: return "Toggle All Doors";
        case 3: return OnOff("Always Seatbelt", tSeatbelt);
        case 4: return "< Back"; } break;
    case P_SPAWNV: switch (i) {
        case 0: return "Adder";    case 1: return "Zentorno"; case 2: return "T20";
        case 3: return "Oppressor";case 4: return "Buzzard";  case 5: return "Insurgent";
        case 6: return "Kuruma";   case 7: return "Deluxo";   case 8: return "< Back"; } break;
    case P_WEAPON: switch (i) {
        case 0: return "Give All Upgraded Weapons";
        case 1: return OnOff("Infinite Ammo Clip", tInfAmmo);
        case 2: return "Give Max Ammo";
        case 3: return OnOff("Freeze Ammo", tFreezeAmmo);
        case 4: return OnOff("Explosive Melee", tExplMelee);
        case 5: return OnOff("Flaming Bullets", tFireAmmo);
        case 6: return "Remove All Weapons";
        case 7: return OnOff("Better Accuracy", tAccuracy);
        case 8: return OnOff("Better Recoil", tRecoil);
        case 9: return "Custom Weapons  >";
        case 10: return "< Back"; } break;
    case P_CUSTW: switch (i) {
        case 0: return OnOff("Vehicles Weapon", tVehWeapon);
        case 1: return OnOff("Peds Weapon", tPedWeapon);
        case 2: return OnOff("Teleport Weapon", tTpWeapon);
        case 3: return "< Back"; } break;
    case P_WANTED: switch (i) {
        case 0: return Lbl("Wanted Level", vWanted);
        case 1: return OnOff("Never Wanted", tNeverWanted);
        case 2: return "Clear Wanted Level";
        case 3: return OnOff("Freeze Wanted Level", tFreezeWanted);
        case 4: return OnOff("Dispatch Enabled", tDispatch);
        case 5: return OnOff("Police Ignore Player", tPoliceIgnore);
        case 6: return OnOff("Disable Civilian Reports", tNoReports);
        case 7: return "< Back"; } break;
    case P_TP: switch (i) {
        case 0: return "Teleport Map Point";
        case 1: return "Teleport To Objective";
        case 2: return "Landmarks  >";
        case 3: return "Important Teleports  >";
        case 4: return "< Back"; } break;
    case P_LAND: switch (i) {
        case 0: return "Maze Bank Roof";  case 1: return "Mount Chiliad";
        case 2: return "Vinewood Sign";   case 3: return "Del Perro Pier";
        case 4: return "Fort Zancudo";    case 5: return "Airport";
        case 6: return "Casino";          case 7: return "< Back"; } break;
    case P_IMPT: switch (i) {
        case 0: return "North Yankton";       case 1: return "Los Santos Customs";
        case 2: return "Ammu-Nation";         case 3: return "Hospital";
        case 4: return "Police Station";      case 5: return "< Back"; } break;
    case P_WORLD: switch (i) {
        case 0: return Lbl("Hour", vHour);
        case 1: return Lbl("Minute", vMinute);
        case 2: return "Apply Time";
        case 3: return LblS("Weather", WeatherName(vWeather));
        case 4: return "Apply Weather";
        case 5: return OnOff("Freeze Time", tFreezeTime);
        case 6: return Lbl("Time of Day", vTimeOfDay);
        case 7: return "< Back"; } break;
    case P_NPC: switch (i) {
        case 0: return Lbl("Traffic Density", vTraffic);
        case 1: return Lbl("Pedestrian Density", vPeds);
        case 2: return OnOff("Everyone Ignores Player", tIgnorePlayer);
        case 3: return OnOff("Chaos Mode", tChaos);
        case 4: return "< Back"; } break;
    case P_MISC: switch (i) {
        case 0: return "Explode All Cars";
        case 1: return OnOff("Force First Person", tFirstPerson);
        case 2: return OnOff("Show Speedometer", tSpeedo);
        case 3: return OnOff("Show Coordinates", tCoords);
        case 4: return "Kill All Enemies";
        case 5: return "< Back"; } break;
    case P_HUD: switch (i) {
        case 0: return OnOff("HUD", tHud);
        case 1: return OnOff("Radar", tRadar);
        case 2: return OnOff("Area Name", tAreaName);
        case 3: return "< Back"; } break;
    case P_GUARD: switch (i) {
        case 0: return "Spawn Bodyguard";
        case 1: return OnOff("Godmode Bodyguards", tGuardGod);
        case 2: return "Remove All Bodyguards";
        case 3: return "< Back"; } break;
    case P_MPMAP: switch (i) {
        case 0: return OnOff("Enable MP Maps", tMpMaps);
        case 1: return "< Back"; } break;
    case P_ABOUT: switch (i) {
        case 0: return "SimpleMenu PS5 port";
        case 1: return "Original: MO3IZO v1.2";
        case 2: return "GTA V PPSA03420 01.011.001";
        case 3: return "Open/close: R1 + R2";
        case 4: return Lbl("native calls", (int64_t)g_dbgCalls);
        case 5: return "< Back"; } break;
    }
    return "";
}

// ---------------------------------------------------------------- teleporte
static void SetCoords(int ent, float x, float y, float z) {
    Prep(); PI(ent); PF(x); PF(y); PF(z); PI(0); PI(0); PI(1); PI(1);
    Fire(RVA_SET_ENTITY_COORDS);
}
// Um SET_ENTITY_COORDS unico para longe e revertido pelo jogo enquanto o destino
// nao esta carregado. Congelamos e reenviamos as coords por ~1s ate o streaming pegar.
static float g_tpX, g_tpY, g_tpZ;
static int   g_tpTicks = 0;

static void Tp(float x, float y, float z) {
    g_tpX = x; g_tpY = y; g_tpZ = z;
    g_tpTicks = 70;
    g_dbgTpX = (uint64_t)(int64_t)x;
}

static void PumpTp() {
    if (!g_tpTicks) return;
    int ped = Ped();
    g_dbgPed = (uint64_t)(uint32_t)ped;
    int v = Veh();
    int ent = v ? v : ped;
    if (g_tpTicks == 70) { Prep(); PI(ent); PI(1); Fire(RVA_FREEZE_ENTITY_POSITION); }
    SetCoords(ent, g_tpX, g_tpY, g_tpZ);
    if (--g_tpTicks == 0) {
        Prep(); PI(ent); PI(0); Fire(RVA_FREEZE_ENTITY_POSITION);
        if (v) { Prep(); PI(v); PF(5.0f); Fire(RVA_SET_VEHICLE_ON_GROUND_PROPERLY); }
    }
}
static void TpWaypoint() {
    Prep(); Fire(RVA_GET_WAYPOINT_BLIP_ENUM_ID);
    int spr = (int)R32();
    Prep(); PI(spr); Fire(RVA_GET_FIRST_BLIP_INFO_ID);
    int blip = (int)R32();
    Prep(); PI(blip); Fire(RVA_DOES_BLIP_EXIST);
    if (!R32()) return;
    Prep(); PI(blip); Fire(RVA_GET_BLIP_COORDS);
    float* f = (float*)g_ctx.ret;
    float x = f[0], y = f[2];
    // altura do solo: sobe alto e pede o Z
    static float gz = 0.0f;
    gz = 0.0f;
    Prep(); PF(x); PF(y); PF(1000.0f); PP(&gz); PI(0); Fire(RVA_GET_GROUND_Z_FOR_3D_COORD);
    Tp(x, y, (gz > 0.0f ? gz + 1.0f : 300.0f));
}
static void TpObjective() {
    Prep(); PI(1); Fire(RVA_GET_FIRST_BLIP_INFO_ID);   // sprite 1 = destino generico
    int blip = (int)R32();
    Prep(); PI(blip); Fire(RVA_DOES_BLIP_EXIST);
    if (!R32()) return;
    Prep(); PI(blip); Fire(RVA_GET_BLIP_COORDS);
    float* f = (float*)g_ctx.ret;
    Tp(f[0], f[2], f[4]);
}

// ------------------------------------------------------------------- spawn
static uint32_t g_wantModel = 0;
static int      g_wantTicks = 0;
static void ReqVeh(const char* m) {
    uint32_t h = Hash(m);
    Prep(); PI((int64_t)h); Fire(RVA_IS_MODEL_IN_CDIMAGE);
    if (!R32()) return;
    Prep(); PI((int64_t)h); Fire(RVA_REQUEST_MODEL);
    g_wantModel = h; g_wantTicks = 240;
}
static void PumpSpawn() {
    if (!g_wantModel) return;
    if (--g_wantTicks <= 0) { g_wantModel = 0; return; }
    Prep(); PI((int64_t)g_wantModel); Fire(RVA_HAS_MODEL_LOADED);
    if (!R32()) return;
    int ped = Ped();
    V3 p = Coords(ped);
    Prep(); PI(ped); Fire(RVA_GET_ENTITY_HEADING);
    float hd = RF();
    Prep(); PI((int64_t)g_wantModel); PF(p.x + 3.5f); PF(p.y); PF(p.z); PF(hd); PI(0); PI(0); PI(0);
    Fire(RVA_CREATE_VEHICLE);
    int v = (int)R32();
    Prep(); PI(v); PF(5.0f); Fire(RVA_SET_VEHICLE_ON_GROUND_PROPERLY);
    Prep(); PI(v); PI(0);    Fire(RVA_SET_VEHICLE_MOD_KIT);
    Prep(); PI((int64_t)g_wantModel); Fire(RVA_SET_MODEL_AS_NO_LONGER_NEEDED);
    g_wantModel = 0;
}

static void GiveGuns(bool maxAmmo) {
    int ped = Ped();
    for (int i = 0; i < 14; ++i) {
        const char* w = "WEAPON_PISTOL";
        switch (i) {
            case 0:  w = "WEAPON_PISTOL";        break;
            case 1:  w = "WEAPON_COMBATPISTOL";  break;
            case 2:  w = "WEAPON_APPISTOL";      break;
            case 3:  w = "WEAPON_MICROSMG";      break;
            case 4:  w = "WEAPON_SMG";           break;
            case 5:  w = "WEAPON_ASSAULTRIFLE";  break;
            case 6:  w = "WEAPON_CARBINERIFLE";  break;
            case 7:  w = "WEAPON_PUMPSHOTGUN";   break;
            case 8:  w = "WEAPON_ASSAULTSHOTGUN";break;
            case 9:  w = "WEAPON_SNIPERRIFLE";   break;
            case 10: w = "WEAPON_HEAVYSNIPER";   break;
            case 11: w = "WEAPON_RPG";           break;
            case 12: w = "WEAPON_MINIGUN";       break;
            case 13: w = "WEAPON_GRENADE";       break;
        }
        uint32_t h = Hash(w);
        Prep(); PI(ped); PI((int64_t)h); PI(maxAmmo ? 9999 : 500); PI(0); PI(1);
        Fire(RVA_GIVE_WEAPON_TO_PED);
    }
}

static void SpawnGuard() {
    uint32_t h = Hash("s_m_y_blackops_01");
    Prep(); PI((int64_t)h); Fire(RVA_REQUEST_MODEL);
    Prep(); PI((int64_t)h); Fire(RVA_HAS_MODEL_LOADED);
    if (!R32()) return;
    int ped = Ped();
    V3 p = Coords(ped);
    Prep(); PI(26); PI((int64_t)h); PF(p.x + 2.0f); PF(p.y + 2.0f); PF(p.z); PF(0.0f); PI(1); PI(1);
    Fire(RVA_CREATE_PED);
    int g = (int)R32();
    Prep(); PI(g); PI((int64_t)Hash("PLAYER")); Fire(RVA_SET_PED_RELATIONSHIP_GROUP_HASH);
    Prep(); PI(g); PI((int64_t)Hash("WEAPON_CARBINERIFLE")); PI(9999); PI(0); PI(1);
    Fire(RVA_GIVE_WEAPON_TO_PED);
    if (tGuardGod) { Prep(); PI(g); PI(1); Fire(RVA_SET_ENTITY_INVINCIBLE); }
}

// -------------------------------------------------------------------- acoes
static int g_page = 0;
static int g_sel[P_COUNT] = {};

static void ApplyMod(int slot, int value) {
    int v = Veh(); if (!v) return;
    Prep(); PI(v); PI(0); Fire(RVA_SET_VEHICLE_MOD_KIT);
    Prep(); PI(v); PI(slot); PI(value); PI(0); Fire(RVA_SET_VEHICLE_MOD);
}

static void Act(int p, int i) {
    g_dbgAction = (uint64_t)((p << 8) | i);
    int ped = Ped(), plr = Plr(), v = Veh();
    switch (p) {
    case P_MAIN:
        if (i <= 10) { static const int map[] = { P_PLAYER,P_VEHICLE,P_WEAPON,P_WANTED,P_TP,
                                                  P_WORLD,P_NPC,P_MISC,P_HUD,P_GUARD,P_MPMAP };
                       g_page = map[i]; }
        else g_page = P_ABOUT;
        return;
    case P_PLAYER: switch (i) {
        case 0: tGod = !tGod; return;
        case 1: tInfAbility = !tInfAbility; return;
        case 2: Prep(); PI(ped); PI(200); PI(0); Fire(RVA_SET_ENTITY_HEALTH);
                Prep(); PI(ped); PI(100); Fire(RVA_SET_PED_ARMOUR);
                Prep(); PI(ped); Fire(RVA_CLEAR_PED_BLOOD_DAMAGE); return;
        case 3: Prep(); PI(ped); PI(500); Fire(RVA_SET_PED_MAX_HEALTH);
                Prep(); PI(ped); PI(500); PI(0); Fire(RVA_SET_ENTITY_HEALTH);
                Prep(); PI(ped); PI(100); Fire(RVA_SET_PED_ARMOUR); return;
        case 4: tShowStats = !tShowStats; return;
        case 5: tInvis = !tInvis; return;
        case 6: g_page = P_ABIL; return;
        case 7: g_page = P_VISION; return;
        case 8: g_page = P_MAIN; return; } return;
    case P_ABIL: switch (i) {
        case 0: tSuperPunch = !tSuperPunch; return;
        case 1: tJump = !tJump; return;
        case 2: tRun = !tRun; return;
        case 3: tSwim = !tSwim; return;
        case 4: tNoRagdoll = !tNoRagdoll; return;
        case 5: tDrunk = !tDrunk;
                if (!tDrunk) { Prep(); PI(ped); PI(1000); Fire(RVA_RESET_PED_MOVEMENT_CLIPSET); }
                return;
        case 6: g_page = P_PLAYER; return; } return;
    case P_VISION: switch (i) {
        case 0: tNight = !tNight;   Prep(); PI(tNight ? 1 : 0);   Fire(RVA_SET_NIGHTVISION); return;
        case 1: tThermal = !tThermal; Prep(); PI(tThermal ? 1 : 0); Fire(RVA_SET_SEETHROUGH); return;
        case 2: tBlur = !tBlur;
                if (tBlur) { Prep(); PP("CAMERA_securitycam"); Fire(RVA_SET_TIMECYCLE_MODIFIER); }
                else       { Prep();                            Fire(RVA_CLEAR_TIMECYCLE_MODIFIER); }
                return;
        case 3: tBars = !tBars; return;
        case 4: tShake = !tShake;
                if (tShake) { Prep(); PP("SKY_DIVING_SHAKE"); PF(0.5f); Fire(RVA_SHAKE_GAMEPLAY_CAM); }
                return;
        case 5: g_page = P_PLAYER; return; } return;
    case P_VEHICLE: switch (i) {
        case 0: tSuspension = !tSuspension; return;
        case 1: tVehGod = !tVehGod; return;
        case 2: if (v) { Prep(); PI(v); Fire(RVA_SET_VEHICLE_FIXED);
                         Prep(); PI(v); PF(1000.0f); Fire(RVA_SET_VEHICLE_ENGINE_HEALTH); } return;
        case 3: if (v) { Prep(); PI(v); PF(-4000.0f); Fire(RVA_SET_VEHICLE_ENGINE_HEALTH); } return;
        case 4: g_page = P_TUNING; return;
        case 5: g_page = P_VPROPS; return;
        case 6: g_page = P_VFUNC;  return;
        case 7: g_page = P_SPAWNV; return;
        case 8: g_page = P_MAIN;   return; } return;
    case P_TUNING: switch (i) {
        case 0: if (v) { Prep(); PI(v); PI(0); Fire(RVA_SET_VEHICLE_MOD_KIT);
                         for (int s = 0; s < 50; ++s) {
                             Prep(); PI(v); PI(s); Fire(RVA_GET_NUM_VEHICLE_MODS);
                             int n = (int)R32();
                             if (n > 0) { Prep(); PI(v); PI(s); PI(n - 1); PI(0); Fire(RVA_SET_VEHICLE_MOD); }
                         } } return;
        case 1: tBulletTires = !tBulletTires; return;
        case 2: vTint = (vTint + 1) % 7;  if (v) { Prep(); PI(v); PI(vTint); Fire(RVA_SET_VEHICLE_WINDOW_TINT); } return;
        case 3: vPrimary = (vPrimary + 1) % 160;
                if (v) { Prep(); PI(v); PI(vPrimary); PI(vSecondary); Fire(RVA_SET_VEHICLE_COLOURS); } return;
        case 4: vSecondary = (vSecondary + 1) % 160;
                if (v) { Prep(); PI(v); PI(vPrimary); PI(vSecondary); Fire(RVA_SET_VEHICLE_COLOURS); } return;
        case 5: tNeon = !tNeon;
                if (v) for (int k = 0; k < 4; ++k) { Prep(); PI(v); PI(k); PI(tNeon ? 1 : 0); Fire(RVA_SET_VEHICLE_NEON_ENABLED); }
                return;
        case 6: vNeonCol = (vNeonCol + 1) % 6;
                if (v) { int r = (vNeonCol & 1) ? 255 : 0, g2 = (vNeonCol & 2) ? 255 : 0, b = (vNeonCol & 4) ? 255 : 0;
                         Prep(); PI(v); PI(r); PI(g2); PI(b); Fire(RVA_SET_VEHICLE_NEON_COLOUR); } return;
        case 7: vWheelType = (vWheelType + 1) % 8;
                if (v) { Prep(); PI(v); PI(vWheelType); Fire(RVA_SET_VEHICLE_WHEEL_TYPE); } return;
        case 8: vFrontWheel = (vFrontWheel + 1) % 20; ApplyMod(23, vFrontWheel); return;
        case 9: vSpoiler = (vSpoiler + 1) % 12;       ApplyMod(0,  vSpoiler);    return;
        case 10: vBumper = (vBumper + 1) % 12;        ApplyMod(1,  vBumper);     return;
        case 11: vExhaust = (vExhaust + 1) % 12;      ApplyMod(4,  vExhaust);    return;
        case 12: g_page = P_VEHICLE; return; } return;
    case P_VPROPS: switch (i) {
        case 0: tTurbo = !tTurbo;   if (v) ApplyMod(18, tTurbo ? 1 : 0); return;
        case 1: tStick = !tStick; return;
        case 2: tAutoRepair = !tAutoRepair; return;
        case 3: tAutoFlip = !tAutoFlip; return;
        case 4: tEngineOn = !tEngineOn; return;
        case 5: tNoEject = !tNoEject; return;
        case 6: g_page = P_VEHICLE; return; } return;
    case P_VFUNC: switch (i) {
        case 0: if (v) { Prep(); PI(v); PI(1); PI(1); PI(0); Fire(RVA_SET_VEHICLE_ENGINE_ON); } return;
        case 1: if (v) { static bool locked = false; locked = !locked;
                         Prep(); PI(v); PI(locked ? 2 : 1); Fire(RVA_SET_VEHICLE_DOORS_LOCKED); } return;
        case 2: if (v) for (int dd = 0; dd < 6; ++dd) { Prep(); PI(v); PI(dd); PI(0); PI(0); Fire(RVA_SET_VEHICLE_DOOR_OPEN); } return;
        case 3: tSeatbelt = !tSeatbelt; return;
        case 4: g_page = P_VEHICLE; return; } return;
    case P_SPAWNV: switch (i) {
        case 0: ReqVeh("adder"); return;      case 1: ReqVeh("zentorno"); return;
        case 2: ReqVeh("t20"); return;        case 3: ReqVeh("oppressor"); return;
        case 4: ReqVeh("buzzard"); return;    case 5: ReqVeh("insurgent"); return;
        case 6: ReqVeh("kuruma"); return;     case 7: ReqVeh("deluxo"); return;
        case 8: g_page = P_VEHICLE; return; } return;
    case P_WEAPON: switch (i) {
        case 0: GiveGuns(true); return;
        case 1: tInfAmmo = !tInfAmmo; return;
        case 2: GiveGuns(true); return;
        case 3: tFreezeAmmo = !tFreezeAmmo; return;
        case 4: tExplMelee = !tExplMelee; return;
        case 5: tFireAmmo = !tFireAmmo; return;
        case 6: Prep(); PI(ped); Fire(RVA_REMOVE_ALL_PED_WEAPONS); return;
        case 7: tAccuracy = !tAccuracy;
                Prep(); PI(ped); PI(tAccuracy ? 100 : 30); Fire(RVA_SET_PED_ACCURACY); return;
        case 8: tRecoil = !tRecoil;
                Prep(); PI(plr); PF(tRecoil ? 5.0f : 1.0f); Fire(RVA_SET_PLAYER_WEAPON_DAMAGE_MODIFIER); return;
        case 9: g_page = P_CUSTW; return;
        case 10: g_page = P_MAIN; return; } return;
    case P_CUSTW: switch (i) {
        case 0: tVehWeapon = !tVehWeapon; return;
        case 1: tPedWeapon = !tPedWeapon; return;
        case 2: tTpWeapon = !tTpWeapon; return;
        case 3: g_page = P_WEAPON; return; } return;
    case P_WANTED: switch (i) {
        case 0: vWanted = (vWanted + 1) % 6;
                Prep(); PI(plr); PI(vWanted); PI(0); Fire(RVA_SET_PLAYER_WANTED_LEVEL);
                Prep(); PI(plr); PI(0);              Fire(RVA_SET_PLAYER_WANTED_LEVEL_NOW); return;
        case 1: tNeverWanted = !tNeverWanted; return;
        case 2: Prep(); PI(plr); Fire(RVA_CLEAR_PLAYER_WANTED_LEVEL); return;
        case 3: tFreezeWanted = !tFreezeWanted; return;
        case 4: tDispatch = !tDispatch;
                for (int ds = 1; ds <= 12; ++ds) { Prep(); PI(ds); PI(tDispatch ? 1 : 0); Fire(RVA_ENABLE_DISPATCH_SERVICE); }
                return;
        case 5: tPoliceIgnore = !tPoliceIgnore;
                Prep(); PI(plr); PI(tPoliceIgnore ? 1 : 0); Fire(RVA_SET_POLICE_IGNORE_PLAYER); return;
        case 6: tNoReports = !tNoReports;
                Prep(); PI(tNoReports ? 0 : 1); Fire(RVA_SET_CREATE_RANDOM_COPS); return;
        case 7: g_page = P_MAIN; return; } return;
    case P_TP: switch (i) {
        case 0: TpWaypoint(); return;
        case 1: TpObjective(); return;
        case 2: g_page = P_LAND; return;
        case 3: g_page = P_IMPT; return;
        case 4: g_page = P_MAIN; return; } return;
    case P_LAND: switch (i) {
        case 0: Tp(-75.0f, -818.0f, 326.0f);      return;
        case 1: Tp(501.0f, 5604.0f, 797.0f);      return;
        case 2: Tp(711.0f, 1198.0f, 348.0f);      return;
        case 3: Tp(-1850.0f, -1231.0f, 13.0f);    return;
        case 4: Tp(-2360.0f, 3249.0f, 32.0f);     return;
        case 5: Tp(-1037.0f, -2737.0f, 20.0f);    return;
        case 6: Tp(925.0f, 46.0f, 81.0f);         return;
        case 7: g_page = P_TP; return; } return;
    case P_IMPT: switch (i) {
        case 0: Tp(3360.0f, -4849.0f, 112.0f);    return;   // North Yankton
        case 1: Tp(-337.0f, -136.0f, 39.0f);      return;
        case 2: Tp(21.0f, -1105.0f, 29.0f);       return;
        case 3: Tp(295.0f, -1446.0f, 29.0f);      return;
        case 4: Tp(436.0f, -982.0f, 30.0f);       return;
        case 5: g_page = P_TP; return; } return;
    case P_WORLD: switch (i) {
        case 0: vHour = (vHour + 1) % 24; return;
        case 1: vMinute = (vMinute + 5) % 60; return;
        case 2: Prep(); PI(vHour); PI(vMinute); PI(0); Fire(RVA_SET_CLOCK_TIME); return;
        case 3: vWeather = (vWeather + 1) & 7; return;
        case 4: Prep(); PP(WeatherName(vWeather)); Fire(RVA_SET_WEATHER_TYPE_NOW); return;
        case 5: tFreezeTime = !tFreezeTime;
                Prep(); PI(tFreezeTime ? 1 : 0); Fire(RVA_PAUSE_CLOCK); return;
        case 6: vTimeOfDay = (vTimeOfDay + 1) % 4;
                { int hh = vTimeOfDay == 0 ? 6 : vTimeOfDay == 1 ? 12 : vTimeOfDay == 2 ? 19 : 0;
                  Prep(); PI(hh); PI(0); PI(0); Fire(RVA_SET_CLOCK_TIME); } return;
        case 7: g_page = P_MAIN; return; } return;
    case P_NPC: switch (i) {
        case 0: vTraffic = (vTraffic + 1) % 11; return;
        case 1: vPeds = (vPeds + 1) % 11; return;
        case 2: tIgnorePlayer = !tIgnorePlayer;
                Prep(); PI(plr); PI(tIgnorePlayer ? 1 : 0); Fire(RVA_SET_EVERYONE_IGNORE_PLAYER); return;
        case 3: tChaos = !tChaos; return;
        case 4: g_page = P_MAIN; return; } return;
    case P_MISC: switch (i) {
        case 0: { V3 pp = Coords(ped);
                  Prep(); PF(pp.x); PF(pp.y); PF(pp.z); PF(400.0f); PI(0); PI(0); PI(0); PI(0); PI(0);
                  Fire(RVA_CLEAR_AREA_OF_VEHICLES); } return;
        case 1: tFirstPerson = !tFirstPerson;
                Prep(); PI(tFirstPerson ? 4 : 1); Fire(RVA_SET_FOLLOW_PED_CAM_VIEW_MODE); return;
        case 2: tSpeedo = !tSpeedo; return;
        case 3: tCoords = !tCoords; return;
        case 4: { V3 pp = Coords(ped);
                  Prep(); PF(pp.x); PF(pp.y); PF(pp.z); PF(30.0f); PI(0); PI(1); PF(1.0f); PI(0);
                  Fire(RVA_ADD_EXPLOSION); } return;
        case 5: g_page = P_MAIN; return; } return;
    case P_HUD: switch (i) {
        case 0: tHud = !tHud;           Prep(); PI(tHud ? 1 : 0);      Fire(RVA_DISPLAY_HUD); return;
        case 1: tRadar = !tRadar;       Prep(); PI(tRadar ? 1 : 0);    Fire(RVA_DISPLAY_RADAR); return;
        case 2: tAreaName = !tAreaName; Prep(); PI(tAreaName ? 1 : 0); Fire(RVA_DISPLAY_AREA_NAME); return;
        case 3: g_page = P_MAIN; return; } return;
    case P_GUARD: switch (i) {
        case 0: SpawnGuard(); return;
        case 1: tGuardGod = !tGuardGod; return;
        case 2: return;
        case 3: g_page = P_MAIN; return; } return;
    case P_MPMAP: switch (i) {
        case 0: tMpMaps = !tMpMaps; return;
        case 1: g_page = P_MAIN; return; } return;
    case P_ABOUT:
        if (i == 5) g_page = P_MAIN;
        return;
    }
}

// ------------------------------------------------------------ por frame
static void PerFrame() {
    int ped = Ped(), plr = Plr();
    g_dbgPed = (uint64_t)(uint32_t)ped;

    if (tGod)        { Prep(); PI(plr); PI(1); Fire(RVA_SET_PLAYER_INVINCIBLE); }
    if (tInvis)      { Prep(); PI(ped); PI(0); PI(0); Fire(RVA_SET_ENTITY_VISIBLE); }
    if (tJump)       { Prep(); PI(plr); Fire(RVA_SET_SUPER_JUMP_THIS_FRAME); }
    if (tRun)        { Prep(); PI(plr); PF(1.49f); Fire(RVA_SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER); }
    if (tSwim)       { Prep(); PI(plr); PF(1.49f); Fire(RVA_SET_SWIM_MULTIPLIER_FOR_PLAYER); }
    if (tNoRagdoll)  { Prep(); PI(ped); PI(0); Fire(RVA_SET_PED_CAN_RAGDOLL); }
    if (tInfAmmo)    { Prep(); PI(ped); PI(1); Fire(RVA_SET_PED_INFINITE_AMMO_CLIP); }
    if (tNeverWanted){ Prep(); PI(plr); Fire(RVA_CLEAR_PLAYER_WANTED_LEVEL); }
    if (tSuperPunch) { Prep(); PI(plr); PF(50.0f); Fire(RVA_SET_PLAYER_MELEE_WEAPON_DAMAGE_MODIFIER); }
    if (tExplMelee)  { Prep(); Fire(RVA_SET_EXPLOSIVE_MELEE_THIS_FRAME); }
    if (tFireAmmo)   { Prep(); Fire(RVA_SET_FIRE_AMMO_THIS_FRAME); }
    int v = Veh();
    if (v) {
        if (tVehGod)     { Prep(); PI(v); PI(1); Fire(RVA_SET_ENTITY_INVINCIBLE); }
        if (tAutoRepair) { Prep(); PI(v); Fire(RVA_SET_VEHICLE_FIXED); }
        if (tEngineOn)   { Prep(); PI(v); PI(1); PI(1); PI(0); Fire(RVA_SET_VEHICLE_ENGINE_ON); }
        if (tBulletTires){ Prep(); PI(v); PI(0); Fire(RVA_SET_VEHICLE_TYRES_CAN_BURST); }
        if (tAutoFlip)   { Prep(); PI(v); PF(5.0f); Fire(RVA_SET_VEHICLE_ON_GROUND_PROPERLY); }
    }
    // densidade de NPC/trafego (0..10 -> 0.0..1.0)
    if (vTraffic < 10) { float f = (float)vTraffic / 10.0f;
                         Prep(); PF(f); Fire(RVA_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME);
                         Prep(); PF(f); Fire(RVA_SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME); }
    if (vPeds < 10)    { float f = (float)vPeds / 10.0f;
                         Prep(); PF(f); Fire(RVA_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME); }
}

// --------------------------------------------------------------------- HUD
static void DrawOverlays() {
    if (tBars) {
        Rect(0.5f, 0.06f, 1.0f, 0.12f, 0, 0, 0, 255);
        Rect(0.5f, 0.94f, 1.0f, 0.12f, 0, 0, 0, 255);
    }
    if (tSpeedo) {
        int v = Veh();
        if (v) {
            Prep(); PI(v); Fire(RVA_GET_ENTITY_SPEED);
            int kmh = (int)(RF() * 3.6f);
            Txt(Lbl("km/h", kmh), 0.86f, 0.90f, 0.45f, 255, 255, 255, false);
        }
    }
    if (tCoords) {
        V3 p = Coords(Ped());
        char* e = g_buf + sizeof(g_buf);
        char* q = SCat(g_buf, e, "X ");  q = ICat(q, e, (int64_t)p.x);
        q = SCat(q, e, "  Y ");          q = ICat(q, e, (int64_t)p.y);
        q = SCat(q, e, "  Z ");          ICat(q, e, (int64_t)p.z);
        Txt(g_buf, 0.02f, 0.90f, 0.40f, 255, 255, 255, false);
    }
    if (tShowStats) {
        Prep(); PI(Ped()); Fire(RVA_GET_ENTITY_HEALTH);
        Txt(Lbl("HP", (int)R32()), 0.02f, 0.86f, 0.40f, 255, 255, 255, false);
    }
}

static void DrawMenu() {
    const float x = 0.16f, w = 0.22f;
    float y = 0.13f;
    int n = Count(g_page);
    Rect(x, y, w, 0.05f, 10, 10, 10, 240);
    Txt(Title(g_page), x, y - 0.018f, 0.5f, 255, 255, 255, true);
    y += 0.05f;
    for (int i = 0; i < n; ++i) {
        bool sel = (i == g_sel[g_page]);
        Rect(x, y, w, 0.030f, sel ? 240 : 0, sel ? 240 : 0, sel ? 240 : 0, sel ? 220 : 150);
        int c = sel ? 0 : 255;
        Txt(Item(g_page, i), x - w * 0.5f + 0.006f, y - 0.010f, 0.30f, c, c, c, false);
        y += 0.030f;
    }
}

static void Input() {
    int n = Count(g_page);
    if (n <= 0) return;
    int& s = g_sel[g_page];
    if (Tap(C_DOWN))  s = (s + 1) % n;
    if (Tap(C_UP))    s = (s - 1 + n) % n;
    if (Tap(C_ACCEPT) || Tap(C_RIGHT)) Act(g_page, s);
    if (Tap(C_CANCEL)) { if (g_page != P_MAIN) g_page = P_MAIN; else g_menuOpen = 0; }
    g_dbgPage = (uint64_t)g_page;
}

static void Tick() {
    ++g_hits;
    PerFrame();
    PumpSpawn();
    PumpTp();
    DrawOverlays();

    static bool latch = false;
    bool combo = Held(C_R1) && Held(C_R2);
    if (combo && !latch) { g_menuOpen = g_menuOpen ? 0 : 1; latch = true; }
    if (!combo) latch = false;

    if (!g_menuOpen) return;
    Block(C_UP); Block(C_DOWN); Block(C_LEFT); Block(C_RIGHT);
    Block(C_ACCEPT); Block(C_CANCEL); Block(C_ATTACK); Block(C_AIM);
    Input();
    DrawMenu();
}

extern "C" int64_t hook_run(void* thread, int64_t ops) {
    typedef int64_t (*RunFn)(void*, int64_t);
    int64_t r = ((RunFn)g_origRun)(thread, ops);

    Prep(); Fire(RVA_GET_FRAME_COUNT);
    uint32_t fc = R32();
    if (fc == g_lastFrame) return r;
    g_lastFrame = fc;

    uint8_t* tls;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(tls));
    void** slot  = (void**)(tls - 0x140);
    void*  saved = *slot;
    *slot = thread;
    Tick();
    *slot = saved;
    return r;
}
