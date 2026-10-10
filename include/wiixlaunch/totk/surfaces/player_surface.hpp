#pragma once

// totk.player v1.0 - where Link is, which way he faces, his hearts and stamina.
//
// Every read is from a snapshot the game thread takes during the player's own
// update (game/player.hpp), so a mod calling these from its tick never touches
// game memory, and cannot read a player that is mid-teardown. Writes are
// queued and applied on the game thread at the next update.
//
// Supported is false on a build with no address row: every read then answers
// "no player" and every write returns 0. IsPresent is whether there is a
// player right now; UpdateCount moves while the game is running and stops
// while it is paused.
//
// Life and stamina are SHARED - one Link - so writes are attributed the way
// input capture is: last writer wins, and each module is named in the log the
// first time it changes either.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/game/player.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::PlayerSurface {

constexpr const char* kName = "totk.player";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

constexpr uint32_t kMaxNoted = 8;
constexpr uint32_t kOwnerLen = 17;
inline char g_Noted[kMaxNoted][kOwnerLen];
inline uint32_t g_NotedCount = 0;

inline void NoteWriterOnce(const char* what) {
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
    WIIXL_LOG("totk.player: %s writes the player's %s%s", owner, what,
              g_NotedCount > 1 ? " - more than one module does, last writer wins" : "");
}

extern "C" inline uint32_t TkPlSupported() { return Player::Supported() ? 1u : 0u; }
extern "C" inline uint32_t TkPlIsPresent() { return Player::IsPresent() ? 1u : 0u; }
extern "C" inline uint32_t TkPlUpdateCount() { return Player::UpdateCount(); }

// x, y, z through a float[3] - botw.player's shape. 0 when there is no player.
extern "C" inline uint32_t TkPlGetPosition(float* out3) {
    return Player::GetPosition(out3) ? 1u : 0u;
}
extern "C" inline float TkPlGetFacing() { return Player::GetFacing(); }

extern "C" inline int32_t TkPlGetLife() { return Player::GetLife(); }
extern "C" inline int32_t TkPlGetMaxLife() { return Player::GetMaxLife(); }
extern "C" inline uint32_t TkPlSetLife(int32_t value) {
    NoteWriterOnce("life");
    return Player::SetLife(value) ? 1u : 0u;
}
extern "C" inline uint32_t TkPlAddLife(int32_t delta) {
    NoteWriterOnce("life");
    return Player::AddLife(delta) ? 1u : 0u;
}

extern "C" inline float TkPlGetStamina() { return Player::GetStamina(); }
extern "C" inline float TkPlGetMaxStamina() { return Player::GetMaxStamina(); }
extern "C" inline float TkPlGetExtraStamina() { return Player::GetExtraStamina(); }
extern "C" inline uint32_t TkPlSetStamina(float value) {
    NoteWriterOnce("stamina");
    return Player::SetStamina(value) ? 1u : 0u;
}
extern "C" inline uint32_t TkPlAddStamina(float delta) {
    NoteWriterOnce("stamina");
    return Player::AddStamina(delta) ? 1u : 0u;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Supported",       &TkPlSupported),
    WIIXL_SURFACE_SYMBOL("IsPresent",       &TkPlIsPresent),
    WIIXL_SURFACE_SYMBOL("UpdateCount",     &TkPlUpdateCount),
    WIIXL_SURFACE_SYMBOL("GetPosition",     &TkPlGetPosition),
    WIIXL_SURFACE_SYMBOL("GetFacing",       &TkPlGetFacing),
    WIIXL_SURFACE_SYMBOL("GetLife",         &TkPlGetLife),
    WIIXL_SURFACE_SYMBOL("GetMaxLife",      &TkPlGetMaxLife),
    WIIXL_SURFACE_SYMBOL("SetLife",         &TkPlSetLife),
    WIIXL_SURFACE_SYMBOL("AddLife",         &TkPlAddLife),
    WIIXL_SURFACE_SYMBOL("GetStamina",      &TkPlGetStamina),
    WIIXL_SURFACE_SYMBOL("GetMaxStamina",   &TkPlGetMaxStamina),
    WIIXL_SURFACE_SYMBOL("GetExtraStamina", &TkPlGetExtraStamina),
    WIIXL_SURFACE_SYMBOL("SetStamina",      &TkPlSetStamina),
    WIIXL_SURFACE_SYMBOL("AddStamina",      &TkPlAddStamina),
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

} // namespace WiiXLaunch::TotK::Surfaces::PlayerSurface
