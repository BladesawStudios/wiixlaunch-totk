#pragma once

// totk.gamedata v1.0 - the save's flag store.
//
// By NAME, with botw.gamedata's names and shapes (GetFlagS32(name, out) and
// the rest), so a mod's flag code ports by changing the surface and the flag
// names. By HASH too: Hash(name) once, then the ...ByHash calls, for a mod that
// reads a flag every frame and should not re-hash it every frame.
//
// Getters return 1 and write through the pointer, or 0 - "no such flag" or
// "GameData not available yet" - and leave it alone. Setters return 0 only
// when GameData is not available; the game QUEUES the write, so a Get straight
// after a Set reads the old value for a frame or so (game/gamedata.hpp).
//
// Writes are shared state - one save - so the first time each module writes a
// flag it is named in the log, as input capture and life writes are.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/game/gamedata.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::GameDataSurface {

constexpr const char* kName = "totk.gamedata";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

namespace GD = WiiXLaunch::TotK::GameData;

constexpr uint32_t kMaxNoted = 8;
constexpr uint32_t kOwnerLen = 17;
inline char g_Noted[kMaxNoted][kOwnerLen];
inline uint32_t g_NotedCount = 0;

inline void NoteWriterOnce() {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0') owner = "<host>";
    for (uint32_t i = 0; i < g_NotedCount; ++i) {
        bool same = true;
        for (uint32_t c = 0; c < kOwnerLen; ++c) {
            if (g_Noted[i][c] != owner[c]) { same = false; break; }
            if (owner[c] == '\0') break;
        }
        if (same) return;
    }
    if (g_NotedCount < kMaxNoted) {
        uint32_t i = 0;
        for (; i + 1 < kOwnerLen && owner[i]; ++i) g_Noted[g_NotedCount][i] = owner[i];
        g_Noted[g_NotedCount][i] = '\0';
        ++g_NotedCount;
    }
    WIIXL_LOG("totk.gamedata: %s writes save flags%s", owner,
              g_NotedCount > 1 ? " - more than one module does" : "");
}

extern "C" inline uint32_t TkGdSupported() { return GD::Supported() ? 1u : 0u; }
extern "C" inline uint32_t TkGdAvailable() { return GD::Available() ? 1u : 0u; }
extern "C" inline uint32_t TkGdHash(const char* name) { return GD::Hash(name); }

// --- by hash -----------------------------------------------------------------

extern "C" inline uint32_t TkGdGetBoolByHash(uint32_t h, uint32_t* out) {
    bool v = false;
    if (!out || !GD::GetBool(h, v)) return 0;
    *out = v ? 1u : 0u;
    return 1;
}
extern "C" inline uint32_t TkGdSetBoolByHash(uint32_t h, uint32_t v) {
    NoteWriterOnce();
    return GD::SetBool(h, v != 0) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdGetS32ByHash(uint32_t h, int32_t* out) {
    return out && GD::GetInt(h, *out) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdSetS32ByHash(uint32_t h, int32_t v) {
    NoteWriterOnce();
    return GD::SetInt(h, v) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdAddS32ByHash(uint32_t h, int32_t d) {
    NoteWriterOnce();
    return GD::AddInt(h, d) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdGetU32ByHash(uint32_t h, uint32_t* out) {
    return out && GD::GetUInt(h, *out) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdSetU32ByHash(uint32_t h, uint32_t v) {
    NoteWriterOnce();
    return GD::SetUInt(h, v) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdGetF32ByHash(uint32_t h, float* out) {
    return out && GD::GetFloat(h, *out) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdSetF32ByHash(uint32_t h, float v) {
    NoteWriterOnce();
    return GD::SetFloat(h, v) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdGetVec3ByHash(uint32_t h, float* out3) {
    GD::Vec3 v{};
    if (!out3 || !GD::GetVec3(h, v)) return 0;
    out3[0] = v.x; out3[1] = v.y; out3[2] = v.z;
    return 1;
}
extern "C" inline uint32_t TkGdSetVec3ByHash(uint32_t h, const float* in3) {
    if (!in3) return 0;
    NoteWriterOnce();
    return GD::SetVec3(h, GD::Vec3{in3[0], in3[1], in3[2]}) ? 1u : 0u;
}
// An enum's value is the hash of the value's name: Hash("Bokoblin") and so on.
extern "C" inline uint32_t TkGdGetEnumByHash(uint32_t h, uint32_t* outValueHash) {
    return outValueHash && GD::GetEnum(h, *outValueHash) ? 1u : 0u;
}
extern "C" inline uint32_t TkGdSetEnumByHash(uint32_t h, uint32_t valueHash) {
    NoteWriterOnce();
    return GD::SetEnum(h, valueHash) ? 1u : 0u;
}

// --- by name: botw.gamedata's shapes ------------------------------------------

extern "C" inline uint32_t TkGdGetFlagBool(const char* n, uint32_t* out) {
    return n ? TkGdGetBoolByHash(GD::Hash(n), out) : 0u;
}
extern "C" inline uint32_t TkGdSetFlagBool(const char* n, uint32_t v) {
    return n ? TkGdSetBoolByHash(GD::Hash(n), v) : 0u;
}
extern "C" inline uint32_t TkGdGetFlagS32(const char* n, int32_t* out) {
    return n ? TkGdGetS32ByHash(GD::Hash(n), out) : 0u;
}
extern "C" inline uint32_t TkGdSetFlagS32(const char* n, int32_t v) {
    return n ? TkGdSetS32ByHash(GD::Hash(n), v) : 0u;
}
extern "C" inline uint32_t TkGdAddFlagS32(const char* n, int32_t d) {
    return n ? TkGdAddS32ByHash(GD::Hash(n), d) : 0u;
}
extern "C" inline uint32_t TkGdGetFlagU32(const char* n, uint32_t* out) {
    return n ? TkGdGetU32ByHash(GD::Hash(n), out) : 0u;
}
extern "C" inline uint32_t TkGdSetFlagU32(const char* n, uint32_t v) {
    return n ? TkGdSetU32ByHash(GD::Hash(n), v) : 0u;
}
extern "C" inline uint32_t TkGdGetFlagF32(const char* n, float* out) {
    return n ? TkGdGetF32ByHash(GD::Hash(n), out) : 0u;
}
extern "C" inline uint32_t TkGdSetFlagF32(const char* n, float v) {
    return n ? TkGdSetF32ByHash(GD::Hash(n), v) : 0u;
}
extern "C" inline uint32_t TkGdGetFlagVec3(const char* n, float* out3) {
    return n ? TkGdGetVec3ByHash(GD::Hash(n), out3) : 0u;
}
extern "C" inline uint32_t TkGdSetFlagVec3(const char* n, const float* in3) {
    return n ? TkGdSetVec3ByHash(GD::Hash(n), in3) : 0u;
}
// Value by NAME here, so a mod writes SetFlagEnum("Flag", "Bokoblin").
extern "C" inline uint32_t TkGdGetFlagEnum(const char* n, uint32_t* outValueHash) {
    return n ? TkGdGetEnumByHash(GD::Hash(n), outValueHash) : 0u;
}
extern "C" inline uint32_t TkGdSetFlagEnum(const char* n, const char* value) {
    return n && value ? TkGdSetEnumByHash(GD::Hash(n), GD::Hash(value)) : 0u;
}

// --- rupees: botw.gamedata's convenience ----------------------------------------

extern "C" inline uint32_t TkGdGetRupees(int32_t* out) {
    return TkGdGetS32ByHash(GD::RupeeHash(), out);
}
extern "C" inline uint32_t TkGdSetRupees(int32_t v) {
    return TkGdSetS32ByHash(GD::RupeeHash(), v < 0 ? 0 : v);
}
extern "C" inline uint32_t TkGdAddRupees(int32_t d) {
    int32_t cur = 0;
    if (!TkGdGetS32ByHash(GD::RupeeHash(), &cur)) return 0;
    return TkGdSetRupees(cur + d);
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Supported",     &TkGdSupported),
    WIIXL_SURFACE_SYMBOL("Available",     &TkGdAvailable),
    WIIXL_SURFACE_SYMBOL("Hash",          &TkGdHash),

    WIIXL_SURFACE_SYMBOL("GetFlagBool",   &TkGdGetFlagBool),
    WIIXL_SURFACE_SYMBOL("SetFlagBool",   &TkGdSetFlagBool),
    WIIXL_SURFACE_SYMBOL("GetFlagS32",    &TkGdGetFlagS32),
    WIIXL_SURFACE_SYMBOL("SetFlagS32",    &TkGdSetFlagS32),
    WIIXL_SURFACE_SYMBOL("AddFlagS32",    &TkGdAddFlagS32),
    WIIXL_SURFACE_SYMBOL("GetFlagU32",    &TkGdGetFlagU32),
    WIIXL_SURFACE_SYMBOL("SetFlagU32",    &TkGdSetFlagU32),
    WIIXL_SURFACE_SYMBOL("GetFlagF32",    &TkGdGetFlagF32),
    WIIXL_SURFACE_SYMBOL("SetFlagF32",    &TkGdSetFlagF32),
    WIIXL_SURFACE_SYMBOL("GetFlagVec3",   &TkGdGetFlagVec3),
    WIIXL_SURFACE_SYMBOL("SetFlagVec3",   &TkGdSetFlagVec3),
    WIIXL_SURFACE_SYMBOL("GetFlagEnum",   &TkGdGetFlagEnum),
    WIIXL_SURFACE_SYMBOL("SetFlagEnum",   &TkGdSetFlagEnum),

    WIIXL_SURFACE_SYMBOL("GetBoolByHash", &TkGdGetBoolByHash),
    WIIXL_SURFACE_SYMBOL("SetBoolByHash", &TkGdSetBoolByHash),
    WIIXL_SURFACE_SYMBOL("GetS32ByHash",  &TkGdGetS32ByHash),
    WIIXL_SURFACE_SYMBOL("SetS32ByHash",  &TkGdSetS32ByHash),
    WIIXL_SURFACE_SYMBOL("AddS32ByHash",  &TkGdAddS32ByHash),
    WIIXL_SURFACE_SYMBOL("GetU32ByHash",  &TkGdGetU32ByHash),
    WIIXL_SURFACE_SYMBOL("SetU32ByHash",  &TkGdSetU32ByHash),
    WIIXL_SURFACE_SYMBOL("GetF32ByHash",  &TkGdGetF32ByHash),
    WIIXL_SURFACE_SYMBOL("SetF32ByHash",  &TkGdSetF32ByHash),
    WIIXL_SURFACE_SYMBOL("GetVec3ByHash", &TkGdGetVec3ByHash),
    WIIXL_SURFACE_SYMBOL("SetVec3ByHash", &TkGdSetVec3ByHash),
    WIIXL_SURFACE_SYMBOL("GetEnumByHash", &TkGdGetEnumByHash),
    WIIXL_SURFACE_SYMBOL("SetEnumByHash", &TkGdSetEnumByHash),

    WIIXL_SURFACE_SYMBOL("GetRupees",     &TkGdGetRupees),
    WIIXL_SURFACE_SYMBOL("SetRupees",     &TkGdSetRupees),
    WIIXL_SURFACE_SYMBOL("AddRupees",     &TkGdAddRupees),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::TotK::Surfaces::GameDataSurface
