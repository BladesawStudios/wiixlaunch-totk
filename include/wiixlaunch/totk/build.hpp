#pragma once

// Which TotK builds this module has addresses for.
//
// Every offset into TotK's main is true of exactly one executable. The host
// already knows which one is running - GameVersion fingerprints a slice of
// read-only data at boot (targets/totk.json, "identity") - so this table is
// keyed by that fingerprint, one row per build, and anything that needs an
// address reads it from Current() rather than from a constant.
//
// A build with no row gets nullptr, and a feature that needs an address
// REFUSES BY NAME on it. That is the whole point of keying the table rather
// than hardcoding 1.2.1: an unenrolled build is a refused feature, not a hook
// at an address that means something else in that executable.
//
// Only some of the module needs this. The frame source (graphics/present.hpp)
// and input (game/input.hpp) go through sdk exports and need no address in
// main on any build; the player (game/player.hpp), GameData
// (game/gamedata.hpp), world (game/world.hpp) and camera (game/camera.hpp)
// layers do. Fields join Row as features need them, with the 1.2.1 value
// first and other builds as rows are derived from their dumps.
//
// Keyed by FINGERPRINT, not name: the name is a label totk.json gives a
// fingerprint, and only 1.2.1's is corroborated by a real boot (see the
// "//names" note there). If the identity slice in totk.json ever moves, every
// fingerprint changes and every row here stops matching - which fails closed,
// as an unenrolled build, rather than open.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/game_version.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Build {

// A function in main: where it is, and the first instruction word expected
// there. The word is checked before anything hooks or calls the function, so
// a wrong row is refused at the function rather than trusted - the
// fingerprint says which executable this is, the word says this row is right
// about it.
struct Func {
    uint32_t offset;   // from main's base
    uint32_t insn;     // little-endian first instruction
};

// Player layer (game/player.hpp). Every value derived from the 1.2.1
// decompile; the comment on each says where.
struct PlayerLayout {
    Func calc;           // game::component::Player::calc(this, const float* dt)
    Func preFinalize;    // game::component::Player::preFinalize(this)
    Func addLife;        // game::component::Life::addLife(this, int)
    Func getMaxStamina;  // game::player::StaminaCalculator::getMaxStamina(this) -> float

    uint32_t playerActor;      // Player -> owning GameActor (calcPos: *(this+0x18))
    uint32_t actorPos;         // GameActor -> Vec3f position (calcPos: 0x2b4/0x2b8/0x2bc)
    uint32_t actorForwardX;    // GameActor rotation, forward axis x (calcPos: 0x2c8)
    uint32_t actorForwardZ;    //                     forward axis z (calcPos: 0x2e0)
    uint32_t actorComponents;  // GameActor -> component table (updateLifeFlags: 0x228)
    uint32_t componentsLife;   // table -> Life component (updateLifeFlags: 0x2f8)
    uint32_t lifeCurrent;      // Life -> value holder, int at +8 (addLife: 0x1830)
    uint32_t lifeMax;          // Life -> value holder, int at +8 (addLife: 0x1838)
    uint32_t playerStamina;    // Player -> StaminaCalculator (updateStaminaByPerFrameRate: +5000)
    uint32_t staminaCurrent;   // StaminaCalculator -> float, clamped to max (calcRecovery: 0x74)
    uint32_t staminaExtra;     // StaminaCalculator -> float, yellow stamina (calcRecovery: 0x78)
};

// GameData layer (game/gamedata.hpp): gmd::GameDataMgr's typed accessors.
// Every getter is (mgr, T* out, u32 hash) -> bool, false for an unknown hash;
// every setter is (mgr, T value, u32 hash), Vector3 by pointer. Setters QUEUE
// the write in the manager's own atomic ring - the game applies it later.
struct GameDataLayout {
    uint32_t mgrCell;    // GOT cell holding &GameDataMgr::sInstance (getMaxStamina: adrp/ldr 0x3d8)
    Func getBool, setBool;
    Func getInt, setInt;
    Func getUInt, setUInt;
    Func getFloat, setFloat;
    Func getVec3, setVec3;   // getVector3, setVector3f
    Func getEnum, setEnum;   // an enum value is the murmur3 hash of its name
};

// World layer (game/world.hpp): game::wm::Module - time of day and weather.
struct WorldLayout {
    uint32_t moduleCell;     // GOT cell holding &game::wm::Module::sInstance (WorldInfo::getCurrentWeather: 0x...e148)
    Func postCalc;           // Module::postCalc_(this, job arg) - per-frame, from WorldMgrSystemPostCalcJob
    Func worldSetTime;       // World::setTime(world, float seconds, bool advance, s32 day) - one-shot;
                             //   NOT requestSetTime, whose request flag sticks and pins the clock
    Func setWeather;         // Module::setWeather(this, s32 weather, bool force) - mutex-guarded;
                             //   force sets a request flag that sticks until cleared
    uint32_t moduleWorld;    // Module -> World (Module::setTime: *(this+0x98))
    uint32_t worldSeconds;   // World -> float seconds of the day (saveCurrentDateTime: ToHours/ToMinutes of it)
    uint32_t worldDay;       // World -> u32 day count (saveCurrentDateTime: setInt 0xd8d100fa)
    uint32_t moduleWorldMgr; // Module -> WorldMgr (WorldInfo::getCurrentWeather: *(sInstance+0xd0))
    uint32_t weatherSlot;    // WorldMgr vtable byte offset of getCurrentWeather (WorldMgrMainField vtable +0x180)
    uint32_t moduleMutex;    // Module -> nn::os::MutexType guarding the request blocks (setWeather: +0x20)
    uint32_t requestIndex;   // Module -> u32 index of the block being written (setWeather: +0x2380)
    uint32_t requestBlocks;  // Module -> first request block; flags u64 at its start (setWeather: +0x940)
    uint32_t requestStride;  // bytes between the two blocks (setWeather: 0xd20)
    uint64_t weatherForced;  // the flag setWeather(force) ORs in (0x200000000)
};

// Camera layer (game/camera.hpp): the active sead::LookAtCamera.
struct CameraLayout {
    uint32_t moduleCell;     // GOT cell holding &engine::camera::CameraModule::sInstance (GetCameraForwardDir)
    Func doUpdateMatrix;     // sead::LookAtCamera::doUpdateMatrix(cam, Matrix34f* out) - builds the view matrix
    uint32_t moduleEntity0;  // CameraModule -> CameraEntity* of camera 0 (getCameraEntity: +0x238 + i*0x20)
    uint32_t entityCamera;   // CameraEntity -> the active sead::Camera (GetCameraForwardDir: +8)
    uint32_t camMatrix;      // sead::Camera -> Matrix34f view matrix (getWorldPosByMatrix: +8)
    uint32_t camUp;          // LookAtCamera -> float[3] up (setUp: +0x50)
    uint32_t camPos;         // LookAtCamera -> double[3] position (doUpdateMatrix: +0x60)
    uint32_t camAt;          // LookAtCamera -> double[3] look-at (doUpdateMatrix: +0x78)
};

struct Row {
    uint32_t       fingerprint;   // GameVersion::Fingerprint() for this executable
    const char*    name;          // for the log only, never for matching
    PlayerLayout   player;
    GameDataLayout gamedata;
    WorldLayout    world;
    CameraLayout   camera;
};

inline constexpr Row kRows[] = {
    // build_id 9B4E43650501A4D4489B4BBFDB740F26AF3CF850 - the one Ryujinx
    // reported for the session it called v1.2.1, and the one in the dump the
    // decompile below was made from (checked: same 20 bytes at NSO +0x40).
    { 0x181D91D8u, "1.2.1",
      {
        { 0x006515cc, 0xA9BE7BFDu },   // stp x29,x30,[sp,#-0x20]!
        { 0x0161eb80, 0xD100C3FFu },   // sub sp,sp,#0x30
        { 0x006836b0, 0xF94C1808u },   // ldr x8,[x0,#0x1830]
        { 0x01623d88, 0xD10083FFu },   // sub sp,sp,#0x20
        0x18, 0x2b4, 0x2c8, 0x2e0, 0x228, 0x2f8, 0x1830, 0x1838, 0x1388, 0x74, 0x78,
      },
      {
        0x0462e3d8,
        { 0x00d45810, 0xA9BD7BFDu }, { 0x00b20a48, 0xA9BF7BFDu },   // getBool, setBool
        { 0x010cd5bc, 0xA9BD7BFDu }, { 0x00b51b08, 0xA9BF7BFDu },   // getInt, setInt
        { 0x01068290, 0x34000262u }, { 0x0110a95c, 0xA9BF7BFDu },   // getUInt, setUInt
        { 0x00d7d620, 0x34000262u }, { 0x00b964fc, 0xA9BF7BFDu },   // getFloat, setFloat
        { 0x010bcac0, 0x34000262u }, { 0x00decdec, 0xD10083FFu },   // getVector3, setVector3f
        { 0x00d4447c, 0x34000262u }, { 0x00d451b0, 0xA9BF7BFDu },   // getEnum, setEnum
      },
      {
        0x0462e148,
        { 0x00b36920, 0xD10483FFu },   // postCalc_: sub sp,sp,#0x120
        { 0x0110f958, 0xA9BD7BFDu },   // World::setTime: stp x29,x30,[sp,#-0x30]!
        { 0x01e0d604, 0xA9BD7BFDu },   // setWeather: stp x29,x30,[sp,#-0x30]!
        0x98, 0x28, 0x2c, 0xd0, 0x180,
        0x20, 0x2380, 0x940, 0xd20, 0x200000000ull,
      },
      {
        0x0462ed90,
        { 0x00a2e4b0, 0xD10103FFu },   // LookAtCamera::doUpdateMatrix: sub sp,sp,#0x40
        0x238, 0x8, 0x8, 0x50, 0x60, 0x78,
      } },
};

inline constexpr uint32_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

// The row for the running build, or nullptr if this module has none.
inline const Row* Current() {
    const uint32_t fp = WiiXLaunch::GameVersion::Fingerprint();
    if (fp == 0) return nullptr;
    for (uint32_t i = 0; i < kRowCount; ++i) {
        if (kRows[i].fingerprint == fp) return &kRows[i];
    }
    return nullptr;
}

// Once, at registration. Says which of three different states this is: no
// fingerprint at all, a fingerprint this module has no row for, or a row.
inline void LogState() {
    const uint32_t fp = WiiXLaunch::GameVersion::Fingerprint();
    if (fp == 0) {
        WIIXL_LOG("totk: build cannot be fingerprinted, so no address-based feature "
                  "will be enabled (%u build(s) have rows here)", kRowCount);
        return;
    }
    if (const Row* row = Current()) {
        WIIXL_LOG("totk: running build %s (0x%08X) has an address row", row->name, fp);
        return;
    }
    const char* known = WiiXLaunch::GameVersion::Name();
    WIIXL_LOG("totk: running build %s (0x%08X) has NO address row in this module - "
              "anything that needs an address in main is refused on it",
              known ? known : "<unenrolled>", fp);
}

} // namespace WiiXLaunch::TotK::Build
