#pragma once

// TotK's player: where Link is, which way he faces, his hearts and stamina.
//
// THE FIRST PART OF THIS MODULE THAT NEEDS ADDRESSES IN MAIN, so everything
// here comes from Build::Current()->player and is refused on a build with no
// row. Every function it hooks or calls has its first instruction word in the
// row, and is checked before it is touched.
//
// MODS NEVER TOUCH GAME MEMORY. A mod's tick runs on the render thread; the
// player is updated, and torn down, on the game thread. A mod reading through
// a player pointer from the render thread is a mod that reads freed memory the
// one frame a load screen lands between the read and the use. So:
//
//   * Player::calc is hooked. After the game's own calc - on the game thread,
//     in the middle of the player's own update - the hook copies position,
//     facing, life and stamina into a snapshot here. Mods read the snapshot.
//   * Writes are QUEUED, and applied by the same hook on the next calc, on the
//     game thread, before the snapshot is taken. Life goes through the game's
//     own Life::addLife (atomic, clamped to max); stamina is the float the
//     game's calcRecovery clamps and syncs to GameData itself.
//   * Player::preFinalize is hooked, so the snapshot stops claiming a player
//     the moment the game starts tearing it down.
//
// PAUSED IS PRESENT. calc does not run while the game is paused; the snapshot
// keeps the last values and IsPresent stays true, because Link still exists.
// UpdateCount tells a mod whether the values are moving.
//
// UNITS, seen in game (1.2.1, Ryujinx, 2026-10-10, examples/player_test):
// life is an int in quarter-hearts, as in BotW - AddLife(4) moved the HUD by
// one heart. Stamina is a float; SetStamina(0) emptied the wheel and
// SetStamina(GetMaxStamina()) filled it. The default max of 1000.0 reads as
// one wheel, but a multi-wheel max has not been checked.
//
// LIFE CAN BE WRITTEN TO 0. The game's addLife has no lower clamp; this layer
// clamps at 0 so it never goes negative, and nothing more. Whether the game
// runs its death sequence from a life of 0 it did not cause is not known.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/build.hpp>

#include <cmath>
#include <cstdint>

#if WIIXL_SWITCH
#include <lib.hpp>
#endif

namespace WiiXLaunch::TotK::Player {

namespace impl {

struct Snapshot {
    volatile uint32_t present;
    volatile uint32_t updates;      // calc calls seen while present
    volatile float x, y, z;
    volatile float facing;          // radians, atan2(forward.x, forward.z), as the game computes it
    volatile int32_t life, maxLife;
    volatile float stamina, maxStamina, extraStamina;
};
inline Snapshot g_Snap{};

// Pending writes, from any thread; consumed on the game thread.
inline volatile uint32_t g_LifeSetPending = 0;
inline volatile int32_t  g_LifeSetValue = 0;
inline volatile int32_t  g_LifeAdd = 0;
inline volatile uint32_t g_StaminaSetPending = 0;
inline volatile float    g_StaminaSetValue = 0.f;
inline volatile int32_t  g_StaminaAddMilli = 0;   // thousandths, so it can be accumulated atomically

inline void* volatile g_Player = nullptr;
inline const Build::PlayerLayout* g_L = nullptr;
inline bool g_Ready = false;
inline bool g_FirstLogged = false;

using AddLifeFn = void (*)(void* life, int32_t amount);
using MaxStaminaFn = float (*)(void* calc);
inline AddLifeFn g_AddLife = nullptr;
inline MaxStaminaFn g_MaxStamina = nullptr;

template <typename T>
inline T At(const void* base, uint32_t off) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + off);
}
template <typename T>
inline T* Ptr(void* base, uint32_t off) {
    return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
}

// The Life component, or null if any link on the way is missing.
inline void* LifeOf(void* actor) {
    void* table = At<void*>(actor, g_L->actorComponents);
    return table ? At<void*>(table, g_L->componentsLife) : nullptr;
}

// A Life value holder's int, at +8 - see Life::addLife.
inline int32_t* HolderValue(void* life, uint32_t off) {
    void* holder = At<void*>(life, off);
    return holder ? Ptr<int32_t>(holder, 8) : nullptr;
}

// Game thread, after the game's own calc.
inline void Update(void* player) {
    const Build::PlayerLayout& L = *g_L;
    void* actor = At<void*>(player, L.playerActor);
    if (!actor) return;

    void* life = LifeOf(actor);
    int32_t* cur = life ? HolderValue(life, L.lifeCurrent) : nullptr;

    // Queued life writes. Set first, then add, so "set to 12 then add 4" in
    // the same frame means 16 and not whichever arrived last.
    if (life && cur && g_AddLife) {
        if (__atomic_exchange_n(&g_LifeSetPending, 0u, __ATOMIC_ACQ_REL)) {
            int32_t target = g_LifeSetValue;
            if (target < 0) target = 0;
            g_AddLife(life, target - *cur);
        }
        int32_t add = __atomic_exchange_n(&g_LifeAdd, 0, __ATOMIC_ACQ_REL);
        if (add != 0) {
            if (*cur + add < 0) add = -*cur;
            g_AddLife(life, add);
        }
    }

    // Queued stamina writes, onto the float calcRecovery clamps and syncs.
    void* stam = Ptr<uint8_t>(player, L.playerStamina);
    const float maxStam = g_MaxStamina ? g_MaxStamina(stam) : 0.f;
    float* stamCur = Ptr<float>(stam, L.staminaCurrent);
    {
        float v = *stamCur;
        bool changed = false;
        if (__atomic_exchange_n(&g_StaminaSetPending, 0u, __ATOMIC_ACQ_REL)) {
            v = g_StaminaSetValue;
            changed = true;
        }
        const int32_t milli = __atomic_exchange_n(&g_StaminaAddMilli, 0, __ATOMIC_ACQ_REL);
        if (milli != 0) {
            v += static_cast<float>(milli) / 1000.0f;
            changed = true;
        }
        if (changed) {
            if (v < 0.f) v = 0.f;
            if (maxStam > 0.f && v > maxStam) v = maxStam;
            *stamCur = v;
        }
    }

    // The snapshot.
    const float* pos = Ptr<float>(actor, L.actorPos);
    g_Snap.x = pos[0];
    g_Snap.y = pos[1];
    g_Snap.z = pos[2];
    g_Snap.facing = std::atan2(At<float>(actor, L.actorForwardX), At<float>(actor, L.actorForwardZ));
    g_Snap.life = cur ? *cur : 0;
    int32_t* max = life ? HolderValue(life, L.lifeMax) : nullptr;
    g_Snap.maxLife = max ? *max : 0;
    g_Snap.stamina = *stamCur;
    g_Snap.maxStamina = maxStam;
    g_Snap.extraStamina = At<float>(stam, L.staminaExtra);
    g_Snap.updates = g_Snap.updates + 1;
    g_Snap.present = 1;
    g_Player = player;

    if (!g_FirstLogged) {
        g_FirstLogged = true;
        WIIXL_LOG("totk.player: player seen - life %d/%d, stamina %d/%d (x1000), "
                  "pos %d,%d,%d", g_Snap.life, g_Snap.maxLife,
                  static_cast<int>(g_Snap.stamina * 1000.f),
                  static_cast<int>(maxStam * 1000.f),
                  static_cast<int>(g_Snap.x), static_cast<int>(g_Snap.y),
                  static_cast<int>(g_Snap.z));
    }
}

#if WIIXL_SWITCH
using CalcFn = void (*)(void*, const float*);
using PreFinalizeFn = uint32_t (*)(void*);
inline CalcFn g_OrigCalc = nullptr;
inline PreFinalizeFn g_OrigPreFinalize = nullptr;

inline void CalcHook(void* player, const float* dt) {
    const CalcFn orig = g_OrigCalc;
    if (orig) orig(player, dt);
    if (player) Update(player);
}

inline uint32_t PreFinalizeHook(void* player) {
    if (player && player == g_Player) {
        g_Snap.present = 0;
        g_Player = nullptr;
        g_FirstLogged = false;
        WIIXL_LOG("totk.player: player torn down - no longer present");
    }
    const PreFinalizeFn orig = g_OrigPreFinalize;
    return orig ? orig(player) : 0;
}

// The function's runtime address, or 0 if its first word is not the row's.
inline uintptr_t Checked(const Build::Func& f, const char* name) {
    const uintptr_t a = exl::util::GetMainModuleInfo().m_Total.m_Start + f.offset;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(a);
    if (word != f.insn) {
        WIIXL_LOG("totk.player: %s at main+0x%X starts 0x%08X, the row says 0x%08X - "
                  "NOT using it", name, f.offset, word, f.insn);
        return 0;
    }
    return a;
}
#endif

} // namespace impl

// Checks the row and installs the two hooks. Once. Logs exactly what it
// refused and why; a refused player layer means every read says "no player".
inline void Init() {
#if WIIXL_SWITCH
    static bool done = false;
    if (done) return;
    done = true;

    const Build::Row* row = Build::Current();
    if (!row) {
        WIIXL_LOG("totk.player: no address row for this build - the player layer is OFF");
        return;
    }
    impl::g_L = &row->player;
    const Build::PlayerLayout& L = row->player;

    const uintptr_t calc = impl::Checked(L.calc, "Player::calc");
    const uintptr_t fin  = impl::Checked(L.preFinalize, "Player::preFinalize");
    const uintptr_t add  = impl::Checked(L.addLife, "Life::addLife");
    const uintptr_t max  = impl::Checked(L.getMaxStamina, "StaminaCalculator::getMaxStamina");
    if (!calc || !fin) {
        WIIXL_LOG("totk.player: cannot hook the player's update - the player layer is OFF");
        return;
    }
    impl::g_AddLife = reinterpret_cast<impl::AddLifeFn>(add);
    impl::g_MaxStamina = reinterpret_cast<impl::MaxStaminaFn>(max);

    impl::g_OrigCalc = reinterpret_cast<impl::CalcFn>(exl::hook::Hook(
        reinterpret_cast<void*>(calc), reinterpret_cast<void*>(&impl::CalcHook), true));
    impl::g_OrigPreFinalize = reinterpret_cast<impl::PreFinalizeFn>(exl::hook::Hook(
        reinterpret_cast<void*>(fin), reinterpret_cast<void*>(&impl::PreFinalizeHook), true));
    if (!impl::g_OrigCalc || !impl::g_OrigPreFinalize) {
        WIIXL_LOG("totk.player: a hook returned no original - the player layer is BROKEN");
        return;
    }
    impl::g_Ready = true;
    WIIXL_LOG("totk.player: hooked Player::calc and preFinalize for %s%s%s", row->name,
              add ? "" : " (life writes OFF)", max ? "" : " (max stamina OFF)");
#endif
}

// Whether the layer is installed at all - false on an unenrolled build.
inline bool Supported() { return impl::g_Ready; }


inline bool IsPresent() { return impl::g_Snap.present != 0; }
inline uint32_t UpdateCount() { return impl::g_Snap.updates; }

inline bool GetPosition(float* out3) {
    if (!out3 || !IsPresent()) return false;
    out3[0] = impl::g_Snap.x; out3[1] = impl::g_Snap.y; out3[2] = impl::g_Snap.z;
    return true;
}
inline float GetFacing() { return impl::g_Snap.facing; }

inline int32_t GetLife() { return impl::g_Snap.life; }
inline int32_t GetMaxLife() { return impl::g_Snap.maxLife; }
inline float GetStamina() { return impl::g_Snap.stamina; }
inline float GetMaxStamina() { return impl::g_Snap.maxStamina; }
inline float GetExtraStamina() { return impl::g_Snap.extraStamina; }

// Queued; applied on the game thread at the player's next update. False when
// there is nothing to apply them to.
inline bool SetLife(int32_t value) {
    if (!impl::g_Ready || !impl::g_AddLife) return false;
    impl::g_LifeSetValue = value;
    __atomic_store_n(&impl::g_LifeSetPending, 1u, __ATOMIC_RELEASE);
    return true;
}
inline bool AddLife(int32_t delta) {
    if (!impl::g_Ready || !impl::g_AddLife) return false;
    __atomic_fetch_add(&impl::g_LifeAdd, delta, __ATOMIC_ACQ_REL);
    return true;
}
inline bool SetStamina(float value) {
    if (!impl::g_Ready) return false;
    impl::g_StaminaSetValue = value;
    __atomic_store_n(&impl::g_StaminaSetPending, 1u, __ATOMIC_RELEASE);
    return true;
}
inline bool AddStamina(float delta) {
    if (!impl::g_Ready) return false;
    __atomic_fetch_add(&impl::g_StaminaAddMilli, static_cast<int32_t>(delta * 1000.0f),
                       __ATOMIC_ACQ_REL);
    return true;
}

} // namespace WiiXLaunch::TotK::Player
