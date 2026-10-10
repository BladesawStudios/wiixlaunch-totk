#pragma once

// TotK's world: time of day, day count, weather - game::wm::Module.
//
// READS ARE A SNAPSHOT, the same rule as the player layer: Module::postCalc_
// is hooked - it runs every frame from WorldMgrSystemPostCalcJob, the world
// manager's own update - and after it the hook copies the clock and the
// current weather here. Mods read the copy and never touch the world objects.
//
//   seconds of day  World +0x28, float, 0..86400. Seconds, not minutes: the
//                   game's own timeline node passes h*3600 + m*60, and
//                   saveCurrentDateTime splits it with ToHours/ToMinutes.
//   day count       World +0x2c, u32 (saved as an Int flag, 0xd8d100fa).
//   weather         the world manager's virtual getCurrentWeather
//                   (WorldMgrMainField vtable +0x180). It does NOT report a
//                   requested weather: it picks one of four live weather
//                   objects by the sky's own blend factors (thunder, snow,
//                   rain, clear) and asks that one.
//
// MODULE REQUESTS ARE STICKY - found the hard way. Module::requestSetTime and
// setWeather(force) set a flag in a request block that postCalc_ copies into
// the next frame's block every frame (memcpy of the 0x24-byte header), so the
// flag stays until something clears it. requestSetTime's flag makes
// World::updateTime override the clock from the request EVERY frame - the
// time froze at whatever was asked (2026-10-10, 1.2.1, Ryujinx). It is the
// cutscene "hold the time" path, and is not used here.
//
// TIME is set with World::setTime(world, seconds, false, -1) instead: it sets
// World +0x48 and flag bit 1, and updateTime copies +0x48 into the clock once
// and carries on. It is not mutex-guarded, so SetSecondsOfDay queues and the
// postCalc_ hook makes the call, on the world manager's own thread.
//
// WEATHER is set with setWeather(force): sticky, on purpose - that is what
// forcing the weather means - and ClearWeather releases it by clearing the
// same flag in the current request block under the module's own mutex, the
// lock setWeather takes. The next frame's copy carries the cleared flag.
//
// WEATHER NUMBERS start at Bluesky = 0, BotW's order: the getter returned 0
// under a clear sky and 1 once the sky clouded over, and setting 1..11 never
// produced sun. The binary's enum string table also lists an "Invalid" first;
// that name is not a weather and is not in kWeatherNames.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/build.hpp>

#include <cstdint>

#if WIIXL_SWITCH
#include <lib.hpp>
#include <nn/os/os_mutex_api.hpp>
#endif

namespace WiiXLaunch::TotK::World {

inline constexpr const char* kWeatherNames[] = {
    "Bluesky", "Cloudy", "Rain", "HeavyRain", "Snow", "HeavySnow",
    "ThunderStorm", "ThunderRain", "BlueskyRain", "CaveRain", "SuperHeavySnow",
};
inline constexpr int32_t kWeatherCount =
    static_cast<int32_t>(sizeof(kWeatherNames) / sizeof(kWeatherNames[0]));

inline const char* WeatherName(int32_t w) {
    return (w >= 0 && w < kWeatherCount) ? kWeatherNames[w] : "?";
}

constexpr float kSecondsPerDay = 86400.0f;

namespace impl {

struct Snapshot {
    volatile uint32_t present;
    volatile uint32_t updates;
    volatile float seconds;
    volatile uint32_t day;
    volatile int32_t weather;
    volatile uint32_t weatherValid;
};
inline Snapshot g_Snap{};

// A time set waiting for the world thread.
inline volatile uint32_t g_TimePending = 0;
inline volatile float g_TimeValue = 0.f;

inline const Build::WorldLayout* g_L = nullptr;
inline uintptr_t g_ModuleCell = 0;
inline bool g_Ready = false;
inline bool g_FirstLogged = false;

using WorldSetTimeFn = void (*)(void* world, float seconds, bool advance, int32_t day);
using SetWeatherFn = void (*)(void* module, int32_t weather, bool force);
using GetWeatherFn = int32_t (*)(void* worldMgr);
inline WorldSetTimeFn g_WorldSetTime = nullptr;
inline SetWeatherFn g_SetWeather = nullptr;

inline void* Module() {
    if (!g_ModuleCell) return nullptr;
    void* const* sInstance = *reinterpret_cast<void* const* const*>(g_ModuleCell);
    return sInstance ? *sInstance : nullptr;
}

template <typename T>
inline T At(const void* base, uint32_t off) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + off);
}
template <typename T>
inline T* Ptr(void* base, uint32_t off) {
    return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
}

// On the world manager's own update, after it has run.
inline void Update(void* module) {
    const Build::WorldLayout& L = *g_L;
    void* world = At<void*>(module, L.moduleWorld);
    if (!world) return;

    if (__atomic_exchange_n(&g_TimePending, 0u, __ATOMIC_ACQ_REL) && g_WorldSetTime) {
        g_WorldSetTime(world, g_TimeValue, false, -1);
    }

    g_Snap.seconds = At<float>(world, L.worldSeconds);
    g_Snap.day = At<uint32_t>(world, L.worldDay);

    void* wm = At<void*>(module, L.moduleWorldMgr);
    if (wm) {
        void* const* vtbl = *reinterpret_cast<void* const* const*>(wm);
        const auto fn = reinterpret_cast<GetWeatherFn>(vtbl[L.weatherSlot / sizeof(void*)]);
        g_Snap.weather = fn(wm);
        g_Snap.weatherValid = 1;
    } else {
        g_Snap.weatherValid = 0;
    }
    g_Snap.updates = g_Snap.updates + 1;
    g_Snap.present = 1;

    if (!g_FirstLogged) {
        g_FirstLogged = true;
        const int secs = static_cast<int>(g_Snap.seconds);
        WIIXL_LOG("totk.world: world seen - day %u, %02d:%02d, weather %d (%s)",
                  g_Snap.day, secs / 3600, (secs / 60) % 60,
                  g_Snap.weatherValid ? g_Snap.weather : -1,
                  g_Snap.weatherValid ? WeatherName(g_Snap.weather) : "no world manager");
    }
}

#if WIIXL_SWITCH
using PostCalcFn = void (*)(void* module, void* arg);
inline PostCalcFn g_OrigPostCalc = nullptr;

inline void PostCalcHook(void* module, void* arg) {
    const PostCalcFn orig = g_OrigPostCalc;
    if (orig) orig(module, arg);
    if (module) Update(module);
}

inline uintptr_t Checked(const Build::Func& f, const char* name) {
    const uintptr_t a = exl::util::GetMainModuleInfo().m_Total.m_Start + f.offset;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(a);
    if (word != f.insn) {
        WIIXL_LOG("totk.world: %s at main+0x%X starts 0x%08X, the row says 0x%08X - "
                  "NOT using it", name, f.offset, word, f.insn);
        return 0;
    }
    return a;
}

// The flags word of the request block being written this frame. Caller holds
// the module mutex.
inline uint64_t* CurrentRequestFlags(void* module) {
    const Build::WorldLayout& L = *g_L;
    uint32_t idx = At<uint32_t>(module, L.requestIndex);
    if (idx > 1) idx = 0;   // the same clamp setWeather applies
    return Ptr<uint64_t>(module, L.requestBlocks + idx * L.requestStride);
}
#endif

} // namespace impl

inline void Init() {
#if WIIXL_SWITCH
    static bool done = false;
    if (done) return;
    done = true;

    const Build::Row* row = Build::Current();
    if (!row) {
        WIIXL_LOG("totk.world: no address row for this build - the world layer is OFF");
        return;
    }
    impl::g_L = &row->world;
    const Build::WorldLayout& L = row->world;
    const uintptr_t post = impl::Checked(L.postCalc, "Module::postCalc_");
    const uintptr_t set  = impl::Checked(L.worldSetTime, "World::setTime");
    const uintptr_t wea  = impl::Checked(L.setWeather, "Module::setWeather");
    if (!post) {
        WIIXL_LOG("totk.world: cannot hook the world update - the world layer is OFF");
        return;
    }
    impl::g_WorldSetTime = reinterpret_cast<impl::WorldSetTimeFn>(set);
    impl::g_SetWeather = reinterpret_cast<impl::SetWeatherFn>(wea);
    impl::g_ModuleCell = exl::util::GetMainModuleInfo().m_Total.m_Start + L.moduleCell;

    impl::g_OrigPostCalc = reinterpret_cast<impl::PostCalcFn>(exl::hook::Hook(
        reinterpret_cast<void*>(post), reinterpret_cast<void*>(&impl::PostCalcHook), true));
    if (!impl::g_OrigPostCalc) {
        WIIXL_LOG("totk.world: the postCalc_ hook returned no original - the world layer is BROKEN");
        return;
    }
    impl::g_Ready = true;
    WIIXL_LOG("totk.world: hooked Module::postCalc_ for %s%s%s", row->name,
              set ? "" : " (time writes OFF)", wea ? "" : " (weather writes OFF)");
#endif
}

inline bool Supported() { return impl::g_Ready; }
inline bool IsPresent() { return impl::g_Snap.present != 0; }
inline uint32_t UpdateCount() { return impl::g_Snap.updates; }

inline float GetSecondsOfDay() { return impl::g_Snap.seconds; }
inline uint32_t GetDay() { return impl::g_Snap.day; }

// -1 when there is no world manager to ask.
inline int32_t GetWeather() {
    return impl::g_Snap.weatherValid ? impl::g_Snap.weather : -1;
}

// Queued; applied once on the world thread at its next update, after which
// the clock runs on from the new time. Wrapped into a day.
inline bool SetSecondsOfDay(float seconds) {
    if (!impl::g_Ready || !impl::g_WorldSetTime) return false;
    while (seconds < 0.f) seconds += kSecondsPerDay;
    while (seconds >= kSecondsPerDay) seconds -= kSecondsPerDay;
    impl::g_TimeValue = seconds;
    __atomic_store_n(&impl::g_TimePending, 1u, __ATOMIC_RELEASE);
    return true;
}

// Forces the weather until ClearWeather. The game's own mutex-guarded request.
inline bool SetWeather(int32_t weather) {
    void* m = impl::Module();
    if (!impl::g_Ready || !impl::g_SetWeather || !m) return false;
    if (weather < 0 || weather >= kWeatherCount) return false;
    impl::g_SetWeather(m, weather, true);
    return true;
}

// Releases a forced weather; the game's own forecast takes over again.
inline bool ClearWeather() {
#if WIIXL_SWITCH
    void* m = impl::Module();
    if (!impl::g_Ready || !m) return false;
    auto* mutex = impl::Ptr<nn::os::MutexType>(m, impl::g_L->moduleMutex);
    nn::os::LockMutex(mutex);
    *impl::CurrentRequestFlags(m) &= ~impl::g_L->weatherForced;
    nn::os::UnlockMutex(mutex);
    return true;
#else
    return false;
#endif
}

inline bool IsWeatherForced() {
#if WIIXL_SWITCH
    void* m = impl::Module();
    if (!impl::g_Ready || !m) return false;
    return (*impl::CurrentRequestFlags(m) & impl::g_L->weatherForced) != 0;
#else
    return false;
#endif
}

} // namespace WiiXLaunch::TotK::World
