#pragma once

// TotK's actors: walk the live ones, spawn new ones, delete and move existing
// ones - all through the game's own machinery.
//
// WALKING. Every live actor is a shaft::baseproc::BaseProc in BaseProcMgr's
// tree (root at mgr+0x38, guarded by the mutex at mgr+0). The walk is the one
// ActorMgr::deleteAllActors uses: a binary tree whose node at +0 is a child
// and at +8 the next node; each node is a container at node-8 whose +0x28 is
// the proc and +0x38 chains to more containers. A proc is kept if it is a
// GameActor (the game's own sead::DynamicCast) and not being deleted (state
// 7..11). Name: the const char* at proc+0x18 - BaseProc's constructor copies a
// {const char*, u32 length} view there, which is what getName returns.
//
// EVERYTHING HAPPENS IN BaseProcMgr::processLists, hooked - the manager's own
// once-a-frame serial step that runs processRemovals and processRequests, so
// the tree has just been settled by the manager itself when the hook runs.
// NOT in Player::calc: that runs on one of several BaseProcExecutor threads
// IN PARALLEL with other actors' updates, and walking the tree from there read
// a proc another executor was freeing - a crash in GameActor's RTTI check on
// garbage (2026-10-10, 1.2.1, Ryujinx). Per processLists:
//   1. If something is queued, or every 15th pump otherwise, the tree is
//      walked into the back half of a double-buffered snapshot, which is then
//      published. The walk TRY-locks the tree's mutex - if another thread has
//      it, this pump skips the walk rather than wait on the game thread.
//   2. Queued deletes and moves run - each against the snapshot just taken:
//      the target proc must still be in it under the same name, or the action
//      is dropped and counted. A handle names a slot in one snapshot, so a
//      mod must act within ~15 pumps of reading it.
//   3. Queued spawns run.
//
// SPAWN is ActorMgr::requestCreateActorAsync, filled in exactly as the game's
// own ExecuteGenericRequestCreateActor AI node does it: the name as a
// const char**, a 0x98-byte create arg with position at +0, identity rotation
// at +0xc, unit scale at +0x30, instance name at +0x40 and 3 at +0x90.
// Async: the actor appears a few frames later, in a later snapshot.
//
// DELETE is BaseProc::requestDelete(proc, 0) - reason 0 is what 66 of the
// game's call sites pass.
//
// MOVE is ActorBase::setTransform with the actor's own rotation and the new
// position. It goes through the actor's PhysicsComponent where it has one
// (ActorBase::forceSetMatrix tries that first) and resets a character
// controller's speeds - the game's own teleport, not a field write the next
// physics step would undo (BotW's lesson, docs/actor-transforms.md there).

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/build.hpp>
#include <wiixlaunch/totk/graphics/present.hpp>

#include <cstdint>

#if WIIXL_SWITCH
#include <lib.hpp>
#include <nn/os/os_mutex_api.hpp>
#endif

namespace WiiXLaunch::TotK::Actor {

constexpr uint32_t kMaxActors = 1024;
constexpr uint32_t kNameLen = 64;
constexpr uint32_t kRefreshEvery = 15;
constexpr uint32_t kMaxQueue = 16;

using Handle = uint32_t;   // (snapshot generation << 11) | (slot + 1); 0 = none

namespace impl {

struct Entry {
    void* proc;
    void* actor;
    char name[kNameLen];
    float pos[3];
};

struct Snapshot {
    uint32_t gen;
    uint32_t count;
    Entry e[kMaxActors];
};
inline Snapshot g_Snap[2];
inline volatile uint32_t g_Front = 0;      // index of the published snapshot
inline volatile uint32_t g_Published = 0;  // any snapshot yet?

struct Action {
    uint32_t kind;          // 1 delete, 2 move
    void* proc;
    char name[kNameLen];
    float pos[3];
};
struct Spawn {
    char name[kNameLen];
    float pos[3];
};

// Small queues, filled by mods (any thread) and drained by the pump. One
// spin flag guards both; the pump only ever holds it to copy them out.
inline volatile uint32_t g_QLock = 0;
inline Action g_Actions[kMaxQueue];
inline uint32_t g_ActionCount = 0;
inline Spawn g_Spawns[kMaxQueue];
inline uint32_t g_SpawnCount = 0;

inline void QLock() { while (__atomic_exchange_n(&g_QLock, 1u, __ATOMIC_ACQUIRE)) {} }
inline void QUnlock() { __atomic_store_n(&g_QLock, 0u, __ATOMIC_RELEASE); }

// Counters, for the log and the surface.
inline volatile uint32_t g_Walks = 0, g_WalkSkips = 0;
inline volatile uint32_t g_Deleted = 0, g_Moved = 0, g_Dropped = 0;
inline volatile uint32_t g_SpawnsOk = 0, g_SpawnsFailed = 0;
inline volatile int32_t g_LastSpawnError = -1;

inline const Build::ActorLayout* g_L = nullptr;
inline uintptr_t g_ProcMgrCell = 0, g_ActorMgrCell = 0;
inline bool g_Ready = false;
inline uint32_t g_Pumps = 0;
inline bool g_FirstLogged = false;

// Where a real proc's vtable and its first virtual must live: main's image.
// An entry that fails this is not dispatched on - it is skipped, counted and
// (the first few) logged. Added after two boots crashed on the same object
// with vtable 0x400000000, inside the game's own DynamicCast.
inline uintptr_t g_MainStart = 0, g_MainEnd = 0;
inline volatile uint32_t g_Invalid = 0;
inline uint32_t g_InvalidLogged = 0;

using DynCastFn = void* (*)(void* proc);
using RequestCreateFn = uint32_t (*)(void* mgr, const char** name, void* arg, void* handle,
                                     uint32_t p5, void* p6, long p7, uint64_t p8, uint8_t p9,
                                     int32_t* err, long* p11);
using RequestDeleteFn = uint32_t (*)(void* proc, uint32_t reason);
using SetTransformFn = void (*)(void* actor, const float* matrix34);
inline DynCastFn g_DynCast = nullptr;
inline RequestCreateFn g_Create = nullptr;
inline RequestDeleteFn g_Delete = nullptr;
inline SetTransformFn g_SetTransform = nullptr;

template <typename T>
inline T At(const void* base, uint32_t off) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + off);
}

inline void* Singleton(uintptr_t cell) {
    if (!cell) return nullptr;
    void* const* sInstance = *reinterpret_cast<void* const* const*>(cell);
    return sInstance ? *sInstance : nullptr;
}

inline void CopyName(char* dst, const char* src) {
    uint32_t i = 0;
    for (; src && src[i] && i + 1 < kNameLen; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline bool SameName(const char* a, const char* b) {
    for (uint32_t i = 0; i < kNameLen; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

// --- the walk -----------------------------------------------------------------

inline void Visit(Snapshot& s, void* proc) {
    if (!proc || s.count >= kMaxActors) return;
    const Build::ActorLayout& L = *g_L;
    const int32_t state = At<int32_t>(proc, L.procState);
    if (state <= 1 || (state >= 7 && state <= 11)) return;
    // Never dispatch on an object whose vtable is not in main.
    const uintptr_t vt = (reinterpret_cast<uintptr_t>(proc) & 7) == 0 ? At<uintptr_t>(proc, 0) : 0;
    const bool vtOk = vt >= g_MainStart && vt + 8 <= g_MainEnd && (vt & 7) == 0;
    const uintptr_t slot0 = vtOk ? *reinterpret_cast<const uintptr_t*>(vt) : 0;
    if (!vtOk || slot0 < g_MainStart || slot0 >= g_MainEnd) {
        g_Invalid = g_Invalid + 1;
        if (g_InvalidLogged < 4) {
            ++g_InvalidLogged;
            WIIXL_LOG("totk.actor: skipped a tree entry - proc %p state %d vtable %p slot0 %p",
                      proc, state, reinterpret_cast<void*>(vt), reinterpret_cast<void*>(slot0));
        }
        return;
    }
    void* actor = g_DynCast(proc);
    if (!actor) return;
    Entry& e = s.e[s.count++];
    e.proc = proc;
    e.actor = actor;
    CopyName(e.name, At<const char*>(proc, L.procNameStr));
    const float* p = reinterpret_cast<const float*>(static_cast<uint8_t*>(actor) + L.actorPos);
    e.pos[0] = p[0]; e.pos[1] = p[1]; e.pos[2] = p[2];
}

// The deleteAllActors traversal, iterative over the "next" links and
// recursive over the child links (tree depth, not actor count).
inline void WalkNode(Snapshot& s, void* node) {
    const Build::ActorLayout& L = *g_L;
    while (node) {
        void* child = At<void*>(node, 0);
        if (child) WalkNode(s, child);
        void* next = At<void*>(node, 8);
        for (uint8_t* c = static_cast<uint8_t*>(node) - L.nodeContainer; c;
             c = At<uint8_t*>(c, L.containerNext)) {
            Visit(s, At<void*>(c, L.containerProc));
        }
        node = next;
    }
}

inline bool Walk() {
#if WIIXL_SWITCH
    void* mgr = Singleton(g_ProcMgrCell);
    if (!mgr) return false;
    auto* mutex = static_cast<nn::os::MutexType*>(mgr);
    const bool mine = nn::os::IsMutexLockedByCurrentThread(mutex);
    if (!mine && !nn::os::TryLockMutex(mutex)) {
        g_WalkSkips = g_WalkSkips + 1;
        return false;
    }
    const uint32_t back = 1u - g_Front;
    Snapshot& s = g_Snap[back];
    s.count = 0;
    s.gen = (g_Snap[g_Front].gen + 1) & 0x1FFFFF;
    if (s.gen == 0) s.gen = 1;
    WalkNode(s, At<void*>(mgr, g_L->procTree));
    if (!mine) nn::os::UnlockMutex(mutex);
    __atomic_store_n(&g_Front, back, __ATOMIC_RELEASE);
    g_Published = 1;
    g_Walks = g_Walks + 1;
    if (!g_FirstLogged) {
        g_FirstLogged = true;
        WIIXL_LOG("totk.actor: first walk - %u live actors, %u entries skipped%s", s.count,
                  g_Invalid, s.count >= kMaxActors ? " (TABLE FULL - more exist)" : "");
    }
    return true;
#else
    return false;
#endif
}

// The proc's entry in the current snapshot, if it is still there under name.
inline const Entry* StillLive(void* proc, const char* name) {
    const Snapshot& s = g_Snap[g_Front];
    for (uint32_t i = 0; i < s.count; ++i) {
        if (s.e[i].proc == proc) return SameName(s.e[i].name, name) ? &s.e[i] : nullptr;
    }
    return nullptr;
}

inline void DoMove(const Entry& e, const float* pos) {
    // The actor's own rotation (row-major 3x3) with the new translation, as
    // the row-major 3x4 setTransform takes.
    const float* r = reinterpret_cast<const float*>(static_cast<uint8_t*>(e.actor) + g_L->actorRot);
    const float m[12] = {
        r[0], r[1], r[2], pos[0],
        r[3], r[4], r[5], pos[1],
        r[6], r[7], r[8], pos[2],
    };
    g_SetTransform(e.actor, m);
}

inline void DoSpawn(const Spawn& sp) {
    void* mgr = Singleton(g_ActorMgrCell);
    if (!mgr) { g_LastSpawnError = 100; g_SpawnsFailed = g_SpawnsFailed + 1; return; }
    alignas(16) uint8_t arg[0x98] = {};
    float* f = reinterpret_cast<float*>(arg);
    f[0] = sp.pos[0]; f[1] = sp.pos[1]; f[2] = sp.pos[2];       // +0x00 position
    f[3] = 1.f;  f[7] = 1.f;  f[11] = 1.f;                         // +0x0c identity rotation
    f[12] = 1.f; f[13] = 1.f; f[14] = 1.f;                         // +0x30 unit scale
    static const char kNoInstanceName[] = "";
    *reinterpret_cast<const char**>(arg + 0x40) = kNoInstanceName;
    arg[0x90] = 3;
    const char* name = sp.name;
    int32_t err = -1;
    const uint32_t ok = g_Create(mgr, &name, arg, nullptr, 1, nullptr, 0, 0, 0, &err, nullptr);
    g_LastSpawnError = ok ? 0 : err;
    if (ok) g_SpawnsOk = g_SpawnsOk + 1; else g_SpawnsFailed = g_SpawnsFailed + 1;
    WIIXL_LOG("totk.actor: spawn '%s' at %d,%d,%d - %s (code %d)", sp.name,
              static_cast<int>(sp.pos[0]), static_cast<int>(sp.pos[1]),
              static_cast<int>(sp.pos[2]), ok ? "queued by the game" : "REFUSED", err);
}

// Once per BaseProcMgr::processLists, after it.
inline void Pump() {
    ++g_Pumps;
    Action actions[kMaxQueue];
    Spawn spawns[kMaxQueue];
    uint32_t na = 0, ns = 0;
    QLock();
    na = g_ActionCount; ns = g_SpawnCount;
    for (uint32_t i = 0; i < na; ++i) actions[i] = g_Actions[i];
    for (uint32_t i = 0; i < ns; ++i) spawns[i] = g_Spawns[i];
    g_ActionCount = 0; g_SpawnCount = 0;
    QUnlock();

    // Not before the game has drawn a frame: the first walks of the earlier
    // build ran during boot, alongside actor construction on worker threads.
    const bool due = (na || ns) || (g_Pumps % kRefreshEvery) == 0;
    const bool walked = (due && Present::IsLive()) ? Walk() : false;

    for (uint32_t i = 0; i < na; ++i) {
        const Entry* e = walked ? StillLive(actions[i].proc, actions[i].name) : nullptr;
        if (!e) { g_Dropped = g_Dropped + 1; continue; }
        if (actions[i].kind == 1) {
            g_Delete(e->proc, 0);
            g_Deleted = g_Deleted + 1;
        } else {
            DoMove(*e, actions[i].pos);
            g_Moved = g_Moved + 1;
        }
    }
    for (uint32_t i = 0; i < ns; ++i) DoSpawn(spawns[i]);
}

#if WIIXL_SWITCH
using ProcessListsFn = void (*)(void* mgr);
inline ProcessListsFn g_OrigProcessLists = nullptr;

inline void ProcessListsHook(void* mgr) {
    const ProcessListsFn orig = g_OrigProcessLists;
    if (orig) orig(mgr);
    Pump();
}

inline uintptr_t Checked(const Build::Func& f, const char* name) {
    const uintptr_t a = exl::util::GetMainModuleInfo().m_Total.m_Start + f.offset;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(a);
    if (word != f.insn) {
        WIIXL_LOG("totk.actor: %s at main+0x%X starts 0x%08X, the row says 0x%08X - "
                  "NOT using it", name, f.offset, word, f.insn);
        return 0;
    }
    return a;
}
#endif

// Handle -> entry in the published snapshot, or null if stale or invalid.
inline const Entry* Resolve(Handle h) {
    if (!g_Published || h == 0) return nullptr;
    const Snapshot& s = g_Snap[g_Front];
    const uint32_t slot = (h & 0x7FF) - 1;
    if ((h >> 11) != s.gen || slot >= s.count) return nullptr;
    return &s.e[slot];
}

inline bool Enqueue(uint32_t kind, const Entry& e, const float* pos) {
    QLock();
    const bool room = g_ActionCount < kMaxQueue;
    if (room) {
        Action& a = g_Actions[g_ActionCount++];
        a.kind = kind;
        a.proc = e.proc;
        CopyName(a.name, e.name);
        a.pos[0] = pos ? pos[0] : 0.f; a.pos[1] = pos ? pos[1] : 0.f; a.pos[2] = pos ? pos[2] : 0.f;
    }
    QUnlock();
    return room;
}

} // namespace impl

// Checks the row, resolves the four functions, and hooks processLists.
inline void Init() {
#if WIIXL_SWITCH
    static bool done = false;
    if (done) return;
    done = true;

    const Build::Row* row = Build::Current();
    if (!row) {
        WIIXL_LOG("totk.actor: no address row for this build - the actor layer is OFF");
        return;
    }
    impl::g_L = &row->actor;
    const Build::ActorLayout& L = row->actor;
    impl::g_DynCast = reinterpret_cast<impl::DynCastFn>(impl::Checked(L.dynamicCast, "DynamicCast<GameActor>"));
    impl::g_Create = reinterpret_cast<impl::RequestCreateFn>(impl::Checked(L.requestCreate, "ActorMgr::requestCreateActorAsync"));
    impl::g_Delete = reinterpret_cast<impl::RequestDeleteFn>(impl::Checked(L.requestDelete, "BaseProc::requestDelete"));
    impl::g_SetTransform = reinterpret_cast<impl::SetTransformFn>(impl::Checked(L.setTransform, "ActorBase::setTransform"));
    if (!impl::g_DynCast || !impl::g_Create || !impl::g_Delete || !impl::g_SetTransform) {
        WIIXL_LOG("totk.actor: a function failed its check - the actor layer is OFF");
        return;
    }
    const uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
    impl::g_MainStart = base;
    impl::g_MainEnd = exl::util::GetMainModuleInfo().m_Total.GetEnd();
    impl::g_ProcMgrCell = base + L.procMgrCell;
    impl::g_ActorMgrCell = base + L.actorMgrCell;
    const uintptr_t pl = impl::Checked(L.processLists, "BaseProcMgr::processLists");
    if (!pl) {
        WIIXL_LOG("totk.actor: cannot hook processLists - the actor layer is OFF");
        return;
    }
    impl::g_OrigProcessLists = reinterpret_cast<impl::ProcessListsFn>(exl::hook::Hook(
        reinterpret_cast<void*>(pl), reinterpret_cast<void*>(&impl::ProcessListsHook), true));
    if (!impl::g_OrigProcessLists) {
        WIIXL_LOG("totk.actor: the processLists hook returned no original - BROKEN");
        return;
    }
    impl::g_Ready = true;
    WIIXL_LOG("totk.actor: 5 functions checked for %s; runs after BaseProcMgr::processLists",
              row->name);
#endif
}

inline bool Supported() { return impl::g_Ready; }
inline bool IsPresent() { return impl::g_Published != 0; }

inline uint32_t Count() { return impl::g_Published ? impl::g_Snap[impl::g_Front].count : 0; }
inline uint32_t Generation() { return impl::g_Published ? impl::g_Snap[impl::g_Front].gen : 0; }

// The handle for slot i of the current snapshot, 0 past the end.
inline Handle HandleAt(uint32_t i) {
    if (!impl::g_Published) return 0;
    const impl::Snapshot& s = impl::g_Snap[impl::g_Front];
    return i < s.count ? (s.gen << 11) | (i + 1) : 0;
}

inline bool GetName(Handle h, char* out, uint32_t cap) {
    const impl::Entry* e = impl::Resolve(h);
    if (!e || !out || cap == 0) return false;
    uint32_t i = 0;
    for (; e->name[i] && i + 1 < cap; ++i) out[i] = e->name[i];
    out[i] = '\0';
    return true;
}

inline bool GetPosition(Handle h, float* out3) {
    const impl::Entry* e = impl::Resolve(h);
    if (!e || !out3) return false;
    out3[0] = e->pos[0]; out3[1] = e->pos[1]; out3[2] = e->pos[2];
    return true;
}

// The first actor at or after slot `from` whose name is exactly `name`.
inline Handle FindByName(const char* name, uint32_t from) {
    if (!name || !impl::g_Published) return 0;
    const impl::Snapshot& s = impl::g_Snap[impl::g_Front];
    for (uint32_t i = from; i < s.count; ++i) {
        if (impl::SameName(s.e[i].name, name)) return (s.gen << 11) | (i + 1);
    }
    return 0;
}

// The nearest actor to (x,y,z) within maxDist, optionally only by name.
inline Handle FindNearest(const char* nameOrNull, float x, float y, float z, float maxDist) {
    if (!impl::g_Published) return 0;
    const impl::Snapshot& s = impl::g_Snap[impl::g_Front];
    float best = maxDist * maxDist;
    Handle out = 0;
    for (uint32_t i = 0; i < s.count; ++i) {
        if (nameOrNull && !impl::SameName(s.e[i].name, nameOrNull)) continue;
        const float dx = s.e[i].pos[0] - x, dy = s.e[i].pos[1] - y, dz = s.e[i].pos[2] - z;
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; out = (s.gen << 11) | (i + 1); }
    }
    return out;
}

// Queued for the next processLists. False: queue full, layer off, or stale.
inline bool Spawn(const char* name, float x, float y, float z) {
    if (!impl::g_Ready || !name || !name[0]) return false;
    impl::QLock();
    const bool room = impl::g_SpawnCount < kMaxQueue;
    if (room) {
        impl::Spawn& s = impl::g_Spawns[impl::g_SpawnCount++];
        impl::CopyName(s.name, name);
        s.pos[0] = x; s.pos[1] = y; s.pos[2] = z;
    }
    impl::QUnlock();
    return room;
}

inline bool Delete(Handle h) {
    const impl::Entry* e = impl::Resolve(h);
    return impl::g_Ready && e && impl::Enqueue(1, *e, nullptr);
}

inline bool Move(Handle h, float x, float y, float z) {
    const impl::Entry* e = impl::Resolve(h);
    const float p[3] = {x, y, z};
    return impl::g_Ready && e && impl::Enqueue(2, *e, p);
}

inline int32_t LastSpawnError() { return impl::g_LastSpawnError; }

} // namespace WiiXLaunch::TotK::Actor
