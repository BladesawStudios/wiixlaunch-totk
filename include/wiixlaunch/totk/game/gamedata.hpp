#pragma once

// TotK's GameData: the save's flag store, through gmd::GameDataMgr's own
// typed accessors.
//
// NAMES ARE MURMUR3. A flag is addressed by murmur3_x86_32(name, seed 0) - the
// engine's Murmur32 (engine::actor::ActorParam::getNameMurmur32 returns exactly
// murmur3("engine__actor__ActorParam")), and checked against GameDataList
// itself: 777 of 1031 enum-value names in the file hash to values stored in it
// and 51 event-flow strings hash to flags, against 0-1 for FNV-1a and CRC32.
// PlayerStatus.CurrentRupee, .Life, .MaxLife, .Stamina and .MaxStamina all
// resolve, with the defaults one would expect (12 quarter-hearts, 1000.0).
//
// THE GAME'S OWN FUNCTIONS, NOT ITS TABLES. Every getter here is the game's,
// with its own hash lookup and its own answer for an unknown flag (false), so
// a wrong name is a clean "no such flag" rather than a read of whatever slot
// it collided with. Every setter is the game's too, and the game QUEUES the
// write in an atomic ring inside the manager - safe from any thread, and
// applied when the game next processes the queue. So a Set followed at once by
// a Get reads the OLD value; the new one shows up a frame or so later.
//
// ADD IS GET-THEN-SET. The game has an addInt, but it skips the queue and
// writes the savable store directly behind a check this module has not
// identified, so it is not used. Two adds to the same flag before the game
// applies its queue therefore lose one.
//
// ENUMS are u32 too: the value is the murmur3 hash of the value's name, which
// is what GameDataList stores.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/build.hpp>

#include <cstdint>

#if WIIXL_SWITCH
#include <lib.hpp>
#endif

namespace WiiXLaunch::TotK::GameData {

// murmur3_x86_32 with seed 0 - the hash the game addresses flags by.
inline uint32_t Hash(const char* name) {
    if (!name) return 0;
    uint32_t len = 0;
    while (name[len]) ++len;
    const auto* d = reinterpret_cast<const uint8_t*>(name);
    const uint32_t c1 = 0xCC9E2D51u, c2 = 0x1B873593u;
    uint32_t h = 0;
    const uint32_t blocks = len / 4;
    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t k = static_cast<uint32_t>(d[i * 4]) | (static_cast<uint32_t>(d[i * 4 + 1]) << 8) |
                     (static_cast<uint32_t>(d[i * 4 + 2]) << 16) |
                     (static_cast<uint32_t>(d[i * 4 + 3]) << 24);
        k *= c1; k = (k << 15) | (k >> 17); k *= c2;
        h ^= k;  h = (h << 13) | (h >> 19); h = h * 5 + 0xE6546B64u;
    }
    const uint8_t* tail = d + blocks * 4;
    uint32_t k = 0;
    switch (len & 3) {
        case 3: k ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
        case 2: k ^= static_cast<uint32_t>(tail[1]) << 8;  [[fallthrough]];
        case 1: k ^= tail[0];
                k *= c1; k = (k << 15) | (k >> 17); k *= c2; h ^= k;
    }
    h ^= len;
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}

struct Vec3 { float x, y, z; };

namespace impl {

using GetU8Fn   = bool (*)(void* mgr, uint8_t* out, uint32_t hash);
using GetI32Fn  = bool (*)(void* mgr, int32_t* out, uint32_t hash);
using GetU32Fn  = bool (*)(void* mgr, uint32_t* out, uint32_t hash);
using GetF32Fn  = bool (*)(void* mgr, float* out, uint32_t hash);
using GetVecFn  = bool (*)(void* mgr, Vec3* out, uint32_t hash);
using SetBoolFn = void (*)(void* mgr, bool value, uint32_t hash);
using SetI32Fn  = void (*)(void* mgr, int32_t value, uint32_t hash);
using SetU32Fn  = void (*)(void* mgr, uint32_t value, uint32_t hash);
using SetF32Fn  = void (*)(void* mgr, float value, uint32_t hash);
using SetVecFn  = void (*)(void* mgr, const Vec3* value, uint32_t hash);

struct Fns {
    GetU8Fn  getBool;  SetBoolFn setBool;
    GetI32Fn getInt;   SetI32Fn  setInt;
    GetU32Fn getUInt;  SetU32Fn  setUInt;
    GetF32Fn getFloat; SetF32Fn  setFloat;
    GetVecFn getVec3;  SetVecFn  setVec3;
    GetU32Fn getEnum;  SetU32Fn  setEnum;
};
inline Fns g_Fns{};
inline uintptr_t g_MgrCell = 0;
inline bool g_Ready = false;

// The live GameDataMgr, or null before the game has made one.
inline void* Mgr() {
    if (!g_MgrCell) return nullptr;
    void* const* sInstance = *reinterpret_cast<void* const* const*>(g_MgrCell);
    return sInstance ? *sInstance : nullptr;
}

#if WIIXL_SWITCH
inline uintptr_t Checked(const Build::Func& f, const char* name) {
    const uintptr_t a = exl::util::GetMainModuleInfo().m_Total.m_Start + f.offset;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(a);
    if (word != f.insn) {
        WIIXL_LOG("totk.gamedata: %s at main+0x%X starts 0x%08X, the row says 0x%08X - "
                  "NOT using it", name, f.offset, word, f.insn);
        return 0;
    }
    return a;
}
#endif

} // namespace impl

// Resolves and checks all twelve accessors. Once. The layer is on only if
// every one checks out - a store half of whose types work is a store whose
// failures look like data.
inline void Init() {
#if WIIXL_SWITCH
    static bool done = false;
    if (done) return;
    done = true;

    const Build::Row* row = Build::Current();
    if (!row) {
        WIIXL_LOG("totk.gamedata: no address row for this build - GameData is OFF");
        return;
    }
    const Build::GameDataLayout& L = row->gamedata;
    impl::Fns f{};
    bool ok = true;
#define WIIXL_GMD_RESOLVE(field, type, label)                                        \
    {                                                                                \
        const uintptr_t a = impl::Checked(L.field, label);                           \
        ok = ok && a != 0;                                                           \
        f.field = reinterpret_cast<type>(a);                                         \
    }
    WIIXL_GMD_RESOLVE(getBool,  impl::GetU8Fn,   "getBool")
    WIIXL_GMD_RESOLVE(setBool,  impl::SetBoolFn, "setBool")
    WIIXL_GMD_RESOLVE(getInt,   impl::GetI32Fn,  "getInt")
    WIIXL_GMD_RESOLVE(setInt,   impl::SetI32Fn,  "setInt")
    WIIXL_GMD_RESOLVE(getUInt,  impl::GetU32Fn,  "getUInt")
    WIIXL_GMD_RESOLVE(setUInt,  impl::SetU32Fn,  "setUInt")
    WIIXL_GMD_RESOLVE(getFloat, impl::GetF32Fn,  "getFloat")
    WIIXL_GMD_RESOLVE(setFloat, impl::SetF32Fn,  "setFloat")
    WIIXL_GMD_RESOLVE(getVec3,  impl::GetVecFn,  "getVector3")
    WIIXL_GMD_RESOLVE(setVec3,  impl::SetVecFn,  "setVector3f")
    WIIXL_GMD_RESOLVE(getEnum,  impl::GetU32Fn,  "getEnum")
    WIIXL_GMD_RESOLVE(setEnum,  impl::SetU32Fn,  "setEnum")
#undef WIIXL_GMD_RESOLVE
    if (!ok) {
        WIIXL_LOG("totk.gamedata: an accessor failed its check - GameData is OFF");
        return;
    }
    impl::g_Fns = f;
    impl::g_MgrCell = exl::util::GetMainModuleInfo().m_Total.m_Start + L.mgrCell;
    impl::g_Ready = true;
    WIIXL_LOG("totk.gamedata: 12 accessors checked for %s; manager %s yet", row->name,
              impl::Mgr() ? "exists" : "does not exist");
#endif
}

inline bool Supported() { return impl::g_Ready; }
// Supported, AND the game has created its manager - false until early boot.
inline bool Available() { return impl::g_Ready && impl::Mgr() != nullptr; }

// ---- by hash ---------------------------------------------------------------
// Every getter: false for "no such flag" or "not available", out untouched.
// Every setter: false only for "not available"; an unknown flag is ignored by
// the game, so check with a getter first if that matters.

inline bool GetBool(uint32_t h, bool& out) {
    void* m = impl::Mgr();
    uint8_t v = 0;
    if (!impl::g_Ready || !m || !impl::g_Fns.getBool(m, &v, h)) return false;
    out = (v & 1) != 0;
    return true;
}
inline bool SetBool(uint32_t h, bool v) {
    void* m = impl::Mgr();
    if (!impl::g_Ready || !m) return false;
    impl::g_Fns.setBool(m, v, h);
    return true;
}

#define WIIXL_GMD_PAIR(Name, T, getF, setF)                                         \
    inline bool Get##Name(uint32_t h, T& out) {                                     \
        void* m = impl::Mgr();                                                      \
        T v{};                                                                      \
        if (!impl::g_Ready || !m || !impl::g_Fns.getF(m, &v, h)) return false;      \
        out = v;                                                                    \
        return true;                                                                \
    }                                                                               \
    inline bool Set##Name(uint32_t h, T v) {                                        \
        void* m = impl::Mgr();                                                      \
        if (!impl::g_Ready || !m) return false;                                     \
        impl::g_Fns.setF(m, v, h);                                                  \
        return true;                                                                \
    }
WIIXL_GMD_PAIR(Int,   int32_t,  getInt,   setInt)
WIIXL_GMD_PAIR(UInt,  uint32_t, getUInt,  setUInt)
WIIXL_GMD_PAIR(Float, float,    getFloat, setFloat)
WIIXL_GMD_PAIR(Enum,  uint32_t, getEnum,  setEnum)
#undef WIIXL_GMD_PAIR

inline bool GetVec3(uint32_t h, Vec3& out) {
    void* m = impl::Mgr();
    Vec3 v{};
    if (!impl::g_Ready || !m || !impl::g_Fns.getVec3(m, &v, h)) return false;
    out = v;
    return true;
}
inline bool SetVec3(uint32_t h, const Vec3& v) {
    void* m = impl::Mgr();
    if (!impl::g_Ready || !m) return false;
    impl::g_Fns.setVec3(m, &v, h);
    return true;
}

// Get-then-set: see the header on why this is not the game's addInt.
inline bool AddInt(uint32_t h, int32_t delta) {
    int32_t v = 0;
    return GetInt(h, v) && SetInt(h, v + delta);
}

// ---- the one convenience ----------------------------------------------------

inline uint32_t RupeeHash() { return Hash("PlayerStatus.CurrentRupee"); }

} // namespace WiiXLaunch::TotK::GameData
