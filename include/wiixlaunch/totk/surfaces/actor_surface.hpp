#pragma once

// totk.actor v1.0 - the live actors: walk them, find them, spawn, delete, move.
//
// HANDLES, NOT POINTERS - the botw.actor rule. A handle names a slot in one
// snapshot of the live actors (refreshed about every 15 frames, and
// straight away when something is queued); a handle from an older snapshot
// resolves to nothing, cleanly, instead of to whatever now lives at that
// address. Count/HandleAt walk the current snapshot; FindByName and
// FindNearest search it.
//
// Spawn, Delete and Move are QUEUED and run right after BaseProcMgr's next
// processLists - its own serial per-frame step. Delete and Move re-check, against a
// fresh walk, that the actor is still there under the same name, and drop the
// action if not (DroppedCount). Spawn is the game's async create: the actor
// shows up in a later snapshot, and LastSpawnError says how the game answered
// (0 queued; otherwise ActorMgr's own code: 1 = creation disabled, 2 = empty
// name, 4/5 = the game could not set the actor up - an unknown name is the
// likely cause, not a proven one; 100 = this module found no ActorMgr yet).
//
// Every module that spawns, deletes or moves is named in the log the first time.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/game/actor.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::ActorSurface {

constexpr const char* kName = "totk.actor";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

namespace A = WiiXLaunch::TotK::Actor;

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
    WIIXL_LOG("totk.actor: %s spawns, deletes or moves actors", owner);
}

extern "C" inline uint32_t TkAcSupported() { return A::Supported() ? 1u : 0u; }
extern "C" inline uint32_t TkAcIsPresent() { return A::IsPresent() ? 1u : 0u; }
extern "C" inline uint32_t TkAcCount() { return A::Count(); }
extern "C" inline uint32_t TkAcGeneration() { return A::Generation(); }
extern "C" inline uint32_t TkAcHandleAt(uint32_t i) { return A::HandleAt(i); }

// Caller-owned buffer; returns the length written, 0 for a stale handle.
extern "C" inline uint32_t TkAcGetName(uint32_t h, char* out, uint32_t cap) {
    if (!A::GetName(h, out, cap)) return 0;
    uint32_t n = 0;
    while (out[n]) ++n;
    return n;
}
extern "C" inline uint32_t TkAcGetPosition(uint32_t h, float* out3) {
    return A::GetPosition(h, out3) ? 1u : 0u;
}
extern "C" inline uint32_t TkAcFindByName(const char* name, uint32_t from) {
    return A::FindByName(name, from);
}
// nameOrNull may be null for "any actor".
extern "C" inline uint32_t TkAcFindNearest(const char* nameOrNull, float x, float y, float z,
                                           float maxDist) {
    return A::FindNearest(nameOrNull, x, y, z, maxDist);
}

extern "C" inline uint32_t TkAcSpawn(const char* name, float x, float y, float z) {
    NoteWriterOnce();
    return A::Spawn(name, x, y, z) ? 1u : 0u;
}
extern "C" inline uint32_t TkAcDelete(uint32_t h) {
    NoteWriterOnce();
    return A::Delete(h) ? 1u : 0u;
}
extern "C" inline uint32_t TkAcMove(uint32_t h, float x, float y, float z) {
    NoteWriterOnce();
    return A::Move(h, x, y, z) ? 1u : 0u;
}
extern "C" inline int32_t TkAcLastSpawnError() { return A::LastSpawnError(); }
extern "C" inline uint32_t TkAcDroppedCount() { return A::impl::g_Dropped; }

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Supported",      &TkAcSupported),
    WIIXL_SURFACE_SYMBOL("IsPresent",      &TkAcIsPresent),
    WIIXL_SURFACE_SYMBOL("Count",          &TkAcCount),
    WIIXL_SURFACE_SYMBOL("Generation",     &TkAcGeneration),
    WIIXL_SURFACE_SYMBOL("HandleAt",       &TkAcHandleAt),
    WIIXL_SURFACE_SYMBOL("GetName",        &TkAcGetName),
    WIIXL_SURFACE_SYMBOL("GetPosition",    &TkAcGetPosition),
    WIIXL_SURFACE_SYMBOL("FindByName",     &TkAcFindByName),
    WIIXL_SURFACE_SYMBOL("FindNearest",    &TkAcFindNearest),
    WIIXL_SURFACE_SYMBOL("Spawn",          &TkAcSpawn),
    WIIXL_SURFACE_SYMBOL("Delete",         &TkAcDelete),
    WIIXL_SURFACE_SYMBOL("Move",           &TkAcMove),
    WIIXL_SURFACE_SYMBOL("LastSpawnError", &TkAcLastSpawnError),
    WIIXL_SURFACE_SYMBOL("DroppedCount",   &TkAcDroppedCount),
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

} // namespace WiiXLaunch::TotK::Surfaces::ActorSurface
