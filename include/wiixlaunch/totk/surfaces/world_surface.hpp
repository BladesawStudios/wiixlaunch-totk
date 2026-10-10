#pragma once

// totk.world v1.0 - time of day, day count and weather.
//
// The calls TotK can honour have botw.world's names and signatures: GetWeather,
// SetWeather, ClearWeather, IsWeatherOverridden, WeatherName, WeatherFromName,
// WeatherTypeCount, GetGameTime(HM), SetGameTime(HM), GetDay. Two are TotK's
// own: Get/SetSecondsOfDay, in the unit the game itself keeps (0..86400).
//
// SetWeather ALWAYS HOLDS the weather, whatever lockOut says: TotK's forced
// weather is a request flag that stays set until ClearWeather releases it
// (game/world.hpp). lockOut is accepted so botw.world code ports, and ignored.
// A mod that forces the weather should clear it when it is done.
//
// Weather numbers start at Bluesky = 0, BotW's order (game/world.hpp).
//
// Reads come from the snapshot the world manager's own update takes; writes
// are the game's queued, mutex-guarded requests and land a frame later.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/game/world.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::WorldSurface {

constexpr const char* kName = "totk.world";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

namespace W = WiiXLaunch::TotK::World;

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
    WIIXL_LOG("totk.world: %s changes the %s%s", owner, what,
              g_NotedCount > 1 ? " - more than one module does, last writer wins" : "");
}

inline uint32_t CopyOut(const char* src, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    uint32_t n = 0;
    while (src && src[n] && n + 1 < cap) { out[n] = src[n]; ++n; }
    out[n] = '\0';
    return n;
}

extern "C" inline uint32_t TkWdSupported() { return W::Supported() ? 1u : 0u; }
extern "C" inline uint32_t TkWdIsPresent() { return W::IsPresent() ? 1u : 0u; }
extern "C" inline uint32_t TkWdUpdateCount() { return W::UpdateCount(); }

// --- time ----------------------------------------------------------------------

extern "C" inline uint32_t TkWdGetSecondsOfDay(float* out) {
    if (!out || !W::IsPresent()) return 0;
    *out = W::GetSecondsOfDay();
    return 1;
}
extern "C" inline uint32_t TkWdSetSecondsOfDay(float seconds) {
    NoteWriterOnce("time of day");
    return W::SetSecondsOfDay(seconds) ? 1u : 0u;
}
extern "C" inline uint32_t TkWdGetGameTime(float* hours) {
    if (!hours || !W::IsPresent()) return 0;
    *hours = W::GetSecondsOfDay() / 3600.0f;
    return 1;
}
extern "C" inline uint32_t TkWdGetGameTimeHM(int32_t* hour, int32_t* minute) {
    if (!hour || !minute || !W::IsPresent()) return 0;
    const int32_t s = static_cast<int32_t>(W::GetSecondsOfDay());
    *hour = s / 3600;
    *minute = (s / 60) % 60;
    return 1;
}
extern "C" inline uint32_t TkWdSetGameTime(float hours) {
    NoteWriterOnce("time of day");
    return W::SetSecondsOfDay(hours * 3600.0f) ? 1u : 0u;
}
extern "C" inline uint32_t TkWdSetGameTimeHM(int32_t hour, int32_t minute) {
    NoteWriterOnce("time of day");
    // The same arithmetic as the game's own timeline node.
    return W::SetSecondsOfDay(static_cast<float>(hour) * 3600.0f +
                              static_cast<float>(minute) * 60.0f) ? 1u : 0u;
}
extern "C" inline uint32_t TkWdGetDay(int32_t* out) {
    if (!out || !W::IsPresent()) return 0;
    *out = static_cast<int32_t>(W::GetDay());
    return 1;
}

// --- weather ------------------------------------------------------------------

extern "C" inline uint32_t TkWdGetWeather(int32_t* out) {
    if (!out) return 0;
    const int32_t w = W::GetWeather();
    if (w < 0) return 0;
    *out = w;
    return 1;
}
extern "C" inline uint32_t TkWdSetWeather(int32_t type, uint32_t lockOut) {
    (void)lockOut;   // TotK always holds it - see the header
    NoteWriterOnce("weather");
    return W::SetWeather(type) ? 1u : 0u;
}
extern "C" inline uint32_t TkWdClearWeather() {
    NoteWriterOnce("weather");
    return W::ClearWeather() ? 1u : 0u;
}
extern "C" inline uint32_t TkWdIsWeatherOverridden() {
    return W::IsWeatherForced() ? 1u : 0u;
}
extern "C" inline uint32_t TkWdWeatherName(int32_t type, char* out, uint32_t cap) {
    return CopyOut(type >= 0 && type < W::kWeatherCount ? W::kWeatherNames[type] : nullptr,
                   out, cap);
}
extern "C" inline uint32_t TkWdWeatherFromName(const char* name, int32_t* out) {
    if (!name || !out) return 0;
    for (int32_t i = 0; i < W::kWeatherCount; ++i) {
        const char* a = W::kWeatherNames[i];
        uint32_t c = 0;
        while (a[c] && a[c] == name[c]) ++c;
        if (a[c] == '\0' && name[c] == '\0') { *out = i; return 1; }
    }
    return 0;
}
extern "C" inline int32_t TkWdWeatherTypeCount() { return W::kWeatherCount; }

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Supported",        &TkWdSupported),
    WIIXL_SURFACE_SYMBOL("IsPresent",        &TkWdIsPresent),
    WIIXL_SURFACE_SYMBOL("UpdateCount",      &TkWdUpdateCount),
    WIIXL_SURFACE_SYMBOL("GetSecondsOfDay",  &TkWdGetSecondsOfDay),
    WIIXL_SURFACE_SYMBOL("SetSecondsOfDay",  &TkWdSetSecondsOfDay),
    WIIXL_SURFACE_SYMBOL("GetGameTime",      &TkWdGetGameTime),
    WIIXL_SURFACE_SYMBOL("GetGameTimeHM",    &TkWdGetGameTimeHM),
    WIIXL_SURFACE_SYMBOL("SetGameTime",      &TkWdSetGameTime),
    WIIXL_SURFACE_SYMBOL("SetGameTimeHM",    &TkWdSetGameTimeHM),
    WIIXL_SURFACE_SYMBOL("GetDay",           &TkWdGetDay),
    WIIXL_SURFACE_SYMBOL("GetWeather",       &TkWdGetWeather),
    WIIXL_SURFACE_SYMBOL("SetWeather",       &TkWdSetWeather),
    WIIXL_SURFACE_SYMBOL("ClearWeather",     &TkWdClearWeather),
    WIIXL_SURFACE_SYMBOL("IsWeatherOverridden", &TkWdIsWeatherOverridden),
    WIIXL_SURFACE_SYMBOL("WeatherName",      &TkWdWeatherName),
    WIIXL_SURFACE_SYMBOL("WeatherFromName",  &TkWdWeatherFromName),
    WIIXL_SURFACE_SYMBOL("WeatherTypeCount", &TkWdWeatherTypeCount),
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

} // namespace WiiXLaunch::TotK::Surfaces::WorldSurface
