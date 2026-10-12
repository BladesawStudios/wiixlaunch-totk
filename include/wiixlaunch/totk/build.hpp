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
// (game/gamedata.hpp), world (game/world.hpp), camera (game/camera.hpp),
// actor (game/actor.hpp) and display (graphics/display.hpp) layers do. Fields join Row as features need them, with the 1.2.1 value
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

// Actor layer (game/actor.hpp): walking live procs, spawning, deleting, moving.
struct ActorLayout {
    uint32_t procMgrCell;     // GOT cell -> &shaft::baseproc::BaseProcMgr::sInstance
    uint32_t procTree;        // BaseProcMgr -> tree root (deleteAllActors: *(mgr+0x38)); mutex at mgr+0
    uint32_t nodeContainer;   // tree node -> container is node - this (deleteAll callback: -8)
    uint32_t containerProc;   // container -> BaseProc* (+0x28)
    uint32_t containerNext;   // container -> next container, same key (+0x38)
    uint32_t procState;       // BaseProc -> s32 state (+0x24; 7..11 = being deleted)
    uint32_t procNameStr;     // BaseProc -> const char* name. The ctor copies a {const char*, u32 len}
                              //   view to +0x18/+0x20 (getName returns +0x18). NOT +0x20 - that read
                              //   len|state as a pointer, 0x0000000400000000, and crashed every boot.
    uint32_t actorMgrCell;    // GOT cell -> &engine::actor::ActorMgr::sInstance
    uint32_t actorPos;        // GameActor -> Vec3f position (+0x2b4, as the player layer)
    uint32_t actorRot;        // GameActor -> Matrix33f rotation, row-major (+0x2c0)
    Func dynamicCast;         // sead::DynamicCast<GameActor, BaseProc>(proc) -> GameActor* or null
    Func requestCreate;       // ActorMgr::requestCreateActorAsync(mgr, const char** name, arg, handle, ...)
    Func requestDelete;       // BaseProc::requestDelete(proc, u32 reason) - thread-safe
    Func setTransform;        // ActorBase::setTransform(actor, const Matrix34f*) - physics-aware
    Func processLists;        // BaseProcMgr::processLists(mgr) - the per-frame serial step that
                              //   runs processRemovals + processRequests; the tree is settled after it
};

// Display layer (graphics/display.hpp): frame pacing and render scale.
struct DisplayLayout {
    Func presentUpdate;          // engine::framework::PresentationInfo::update(info, framework) -
                                 //   once a frame from Framework::procFrame_
    uint32_t presMin;            // PresentationInfo -> s32 fastest interval it may pick (+0x0)
    uint32_t presMax;            // PresentationInfo -> s32 slowest interval it may pick (+0x4)
    uint32_t presStepDown;       // PresentationInfo -> s32 interval it steps back down to (+0x18)
    uint32_t presInterval;       // PresentationInfo -> s32 current present interval (+0x14)
    uint32_t presLock;           // PresentationInfo -> u8 "use the interval as is" (+0x24)
    uint32_t fwFrameTicks;       // Framework -> s64 last frame's duration in system ticks (+0x118)
    Func calcDynamicResolution;  // game::gfx::ResolutionMgr::calcDynamicResolution(mgr, info) ->
                                 //   u64: low 32 = float scale, bits 32..39 = 0 apply/1 reset/2 same
    uint32_t resFlags;           // ResolutionMgr -> u32 flags, bit 0 = dynamic resolution on (+0x118)
    Func updateResolution;       // game::gfx::ModelSceneExtension::updateResolution(mse, mode) - at
                                 //   start and on every docked/handheld switch
    uint32_t mseFinalW;          // ModelSceneExtension -> float final-image (= render target) width (+0xcc0)
    uint32_t mseBaseW;           // ModelSceneExtension -> float base render width at scale 1.0 (+0xcc4)
    uint32_t mseBaseH;           // ModelSceneExtension -> float base render height (+0xcc8)
    Func getHeapSize;            // engine::util::HeapSizeDB::getHeapSize(db, const char** name) ->
                                 //   {s64 bytes; bool found} through x8. EVERY heap size the game
                                 //   reads from System/HeapSize/*.HeapDef.byml goes through it -
                                 //   module heaps (ModuleSystem::initializeModules) and named
                                 //   pools (engine::util::GetHeapSize) alike
    Func initGraphics;           // sead::GameFrameworkNx::initializeGraphicsSystem(fw, heap, heap, arg) -
                                 //   creates the display buffers; called by AppMain AFTER mod entries
    uint32_t fwDisplayW;         // GameFrameworkNx -> u32 display buffer width (+0x9c; AppMain: 1920)
    uint32_t fwDisplayH;         // GameFrameworkNx -> u32 display buffer height (+0xa0; AppMain: 1080)
    Func getBaseResolution;      // game::gfx::ResolutionParam::getBaseResolution(p) -> const s32[2]*
    Func getFinalResolution;     // game::gfx::ResolutionParam::getFinalImageResolution(p) -> const s32[2]*
    // Variable frame rate: three INLINE hook sites. Each Func names the
    // instruction the hook displaces, not a function entry.
    Func vfrDeltaStore;          // engine::framework::VFRMgr::update: stp s0,s1,[x0,#8] - stores
                                 //   deltaFrame (interval*0.5) and deltaSec (interval/60); every
                                 //   delta the game hands out is scaled from these two
    Func clothStep;              // cloth sim (FUN_7100821048): s0 was just loaded with a fixed
                                 //   1/30 s step (main+0x3811eac) that is not scaled by the VFR
    Func ultrahandStep;          // Ultrahand (FUN_7101d48210): s0 holds a per-30fps-frame amount
};

struct Row {
    uint32_t       fingerprint;   // GameVersion::Fingerprint() for this executable
    const char*    name;          // for the log only, never for matching
    PlayerLayout   player;
    GameDataLayout gamedata;
    WorldLayout    world;
    CameraLayout   camera;
    ActorLayout    actor;
    DisplayLayout  display;
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
      },
      {
        0x0462ded8, 0x38, 0x8, 0x28, 0x38, 0x24, 0x18,
        0x0462e3f8, 0x2b4, 0x2c0,
        { 0x00b7ee80, 0xD10083FFu },   // DynamicCast<GameActor, BaseProc>: sub sp,sp,#0x20
        { 0x00ab92cc, 0xA9BA7BFDu },   // requestCreateActorAsync: stp x29,x30,[sp,#-0x60]!
        { 0x0083aa68, 0xD10143FFu },   // BaseProc::requestDelete: sub sp,sp,#0x50
        { 0x014d5880, 0xD100C3FFu },   // ActorBase::setTransform: sub sp,sp,#0x30
        { 0x008ed56c, 0xA9BE7BFDu },   // BaseProcMgr::processLists: stp x29,x30,[sp,#-0x20]!
      },
      {
        { 0x008f26c8, 0xD10183FFu },   // PresentationInfo::update: sub sp,sp,#0x60
        0x0, 0x4, 0x18, 0x14, 0x24, 0x118,
        { 0x00cc26f4, 0xD102C3FFu },   // ResolutionMgr::calcDynamicResolution: sub sp,sp,#0xb0
        0x118,
        { 0x00cc3f10, 0xD10143FFu },   // ModelSceneExtension::updateResolution: sub sp,sp,#0x50
        0xcc0, 0xcc4, 0xcc8,
        { 0x007f6a18, 0xD101C3FFu },   // HeapSizeDB::getHeapSize: sub sp,sp,#0x70
        { 0x00e7e9fc, 0xD10303FFu },   // GameFrameworkNx::initializeGraphicsSystem: sub sp,sp,#0xc0
        0x9c, 0xa0,
        { 0x00cc410c, 0xA9BE7BFDu },   // ResolutionParam::getBaseResolution: stp x29,x30,[sp,#-0x20]!
        { 0x00cc4160, 0xA9BE7BFDu },   // ResolutionParam::getFinalImageResolution: same prologue
        { 0x007ede9c, 0x2D010400u },   // VFRMgr::update: stp s0,s1,[x0,#8]
        { 0x00821924, 0x52A7F008u },   // cloth: mov w8,#0x3f800000 (after ldr s0, 1/30)
        { 0x01d48d44, 0xAA1303E0u },   // Ultrahand: mov x0,x19 (after the per-frame amount in s0)
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
