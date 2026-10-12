#pragma once

// totk.display v1.0 - frame rate and render resolution: what the game chose
// this frame, and held overrides for both.
//
// Frame rate is a PRESENT INTERVAL - vsyncs per frame, 1 = 60 fps, 2 = 30,
// 3 = 20, 4 = 15. The game's clock follows it (sDeltaFrame = interval * 0.5),
// so a changed interval changes smoothness, not game speed - as long as the
// frames keep up. SetPresentInterval FIXES it (and slows the game when frames
// take longer); SetPresentIntervalRange(1, 0) lets the game's own logic pick
// 60 when it can and drop to 30 when it cannot - dynamic 60.
// SetVariableFrameRate goes further: the game advances by the measured frame
// time, so any rate up to 60 runs at the right speed, not only 60/30/20. It is what the
// GAME asked for: another mod (UltraCam) can present at a different rate, and
// GetFramePeriodUs - measured between presents - is what is actually shown.
// GetFrameWorkUs is how long the game's last frame took to compute. Resolution
// is a render SCALE of the mode's base size (docked 1600x900): 1.0 is the
// game's own best, GetMaxResolutionScale() fills the render targets (docked
// 1.2 = 1920x1080), and GetRenderSize says what is drawn, in pixels.
// Scales above 1.0 need bigger texture pools: ReserveResolution(w, h), from
// ModEntry, grows them before the game sizes its heaps. SetOutputResolution
// (w, h), also from ModEntry, makes docked render and output at w x h -
// display buffers, render targets and pools.
//
// SetPresentInterval / SetResolutionScale hold for `frames` presented frames;
// HoldForever() is "until Release", which still lapses 300 frames after the
// last Set. Both are refused while UltraCam is loaded (IsUltraCamLoaded), and
// every refusal is logged once per reason with the module that asked.
// Every module that sets either is named in the log the first time.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/graphics/display.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::DisplaySurface {

constexpr const char* kName = "totk.display";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

namespace D = WiiXLaunch::TotK::Display;

constexpr uint32_t kMaxNoted = 8;
constexpr uint32_t kOwnerLen = 17;

struct Noted {
    char owners[kMaxNoted][kOwnerLen];
    uint32_t count;
};
inline Noted g_FpsWriters{}, g_ResWriters{};

inline const char* Owner() {
    const char* owner = WiiXLaunch::ModContext::Current();
    return (owner && owner[0]) ? owner : "<host>";
}

// True the first time `owner` is seen in `n`.
inline bool FirstTime(Noted& n, const char* owner) {
    for (uint32_t i = 0; i < n.count; ++i) {
        bool same = true;
        for (uint32_t c = 0; c < kOwnerLen; ++c) {
            if (n.owners[i][c] != owner[c]) { same = false; break; }
            if (owner[c] == '\0') break;
        }
        if (same) return false;
    }
    if (n.count < kMaxNoted) {
        uint32_t i = 0;
        for (; i + 1 < kOwnerLen && owner[i]; ++i) n.owners[n.count][i] = owner[i];
        n.owners[n.count][i] = '\0';
        ++n.count;
    }
    return true;
}

inline const char* g_LastLoggedRefusal = nullptr;

inline uint32_t Result(bool ok, Noted& writers, const char* what) {
    const char* owner = Owner();
    if (ok) {
        if (FirstTime(writers, owner)) {
            WIIXL_LOG("totk.display: %s sets the %s%s", owner, what,
                      writers.count > 1 ? " - more than one module does, last call wins" : "");
        }
        return 1u;
    }
    if (D::g_LastRefusal && D::g_LastRefusal != g_LastLoggedRefusal) {
        g_LastLoggedRefusal = D::g_LastRefusal;
        WIIXL_LOG("totk.display: %s cannot set the %s - %s", owner, what, D::g_LastRefusal);
    }
    return 0u;
}

extern "C" inline uint32_t TkDsFpsSupported() { return D::FpsSupported() ? 1u : 0u; }
extern "C" inline uint32_t TkDsResolutionSupported() { return D::ResolutionSupported() ? 1u : 0u; }
extern "C" inline uint32_t TkDsIsUltraCamLoaded() { return D::IsUltraCamLoaded() ? 1u : 0u; }
extern "C" inline uint32_t TkDsHoldForever() { return D::kHoldForever; }

extern "C" inline uint32_t TkDsGetPresentInterval() { return D::GetPresentInterval(); }
extern "C" inline uint32_t TkDsGetFramePeriodUs() { return D::GetFramePeriodUs(); }
extern "C" inline uint32_t TkDsGetFrameWorkUs() { return D::GetFrameWorkUs(); }
extern "C" inline uint32_t TkDsSetPresentInterval(uint32_t interval, uint32_t frames) {
    return Result(D::SetPresentInterval(interval, frames), g_FpsWriters, "frame rate");
}
extern "C" inline void TkDsReleasePresentInterval() { D::ReleasePresentInterval(); }
extern "C" inline uint32_t TkDsSetPresentIntervalRange(uint32_t fastest, uint32_t slowest,
                                                       uint32_t frames) {
    return Result(D::SetPresentIntervalRange(fastest, slowest, frames), g_FpsWriters,
                  "frame-rate range");
}
extern "C" inline void TkDsReleasePresentIntervalRange() { D::ReleasePresentIntervalRange(); }
extern "C" inline uint32_t TkDsIsPresentIntervalRangeOverridden() {
    return D::IsPresentIntervalRangeOverridden() ? 1u : 0u;
}
extern "C" inline uint32_t TkDsIsPresentIntervalOverridden() {
    return D::IsPresentIntervalOverridden() ? 1u : 0u;
}

extern "C" inline uint32_t TkDsGetResolutionScale(float* out) {
    return D::GetResolutionScale(out) ? 1u : 0u;
}
extern "C" inline uint32_t TkDsSetVariableFrameRate(uint32_t frames) {
    return Result(D::SetVariableFrameRate(frames), g_FpsWriters, "variable frame rate");
}
extern "C" inline void TkDsReleaseVariableFrameRate() { D::ReleaseVariableFrameRate(); }
extern "C" inline uint32_t TkDsIsVariableFrameRateOn() { return D::IsVariableFrameRateOn() ? 1u : 0u; }
extern "C" inline uint32_t TkDsGetGameStepUs() { return D::GetGameStepUs(); }
extern "C" inline uint32_t TkDsSetDockedRenderSize(uint32_t w, uint32_t h) {
    return Result(D::SetDockedRenderSize(w, h), g_ResWriters, "docked render size");
}
extern "C" inline uint32_t TkDsSetOutputResolution(uint32_t w, uint32_t h) {
    return Result(D::SetOutputResolution(w, h), g_ResWriters, "output resolution");
}
extern "C" inline uint32_t TkDsReserveResolution(uint32_t w, uint32_t h) {
    return Result(D::ReserveResolution(w, h), g_ResWriters, "reserved resolution");
}
extern "C" inline uint32_t TkDsGetMaxResolutionScale(float* out) {
    if (!out) return 0u;
    *out = D::GetMaxResolutionScale();
    return 1u;
}
extern "C" inline uint32_t TkDsGetRenderSize(uint32_t* w, uint32_t* h) {
    return D::GetRenderSize(w, h) ? 1u : 0u;
}
extern "C" inline uint32_t TkDsIsDynamicResolutionOn() { return D::IsDynamicResolutionOn() ? 1u : 0u; }
extern "C" inline uint32_t TkDsSetResolutionScale(float scale, uint32_t frames) {
    return Result(D::SetResolutionScale(scale, frames), g_ResWriters, "render scale");
}
extern "C" inline void TkDsReleaseResolutionScale() { D::ReleaseResolutionScale(); }
extern "C" inline uint32_t TkDsIsResolutionScaleOverridden() {
    return D::IsResolutionScaleOverridden() ? 1u : 0u;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("FpsSupported",                &TkDsFpsSupported),
    WIIXL_SURFACE_SYMBOL("ResolutionSupported",         &TkDsResolutionSupported),
    WIIXL_SURFACE_SYMBOL("IsUltraCamLoaded",            &TkDsIsUltraCamLoaded),
    WIIXL_SURFACE_SYMBOL("HoldForever",                 &TkDsHoldForever),
    WIIXL_SURFACE_SYMBOL("GetPresentInterval",          &TkDsGetPresentInterval),
    WIIXL_SURFACE_SYMBOL("GetFramePeriodUs",            &TkDsGetFramePeriodUs),
    WIIXL_SURFACE_SYMBOL("GetFrameWorkUs",              &TkDsGetFrameWorkUs),
    WIIXL_SURFACE_SYMBOL("SetPresentInterval",          &TkDsSetPresentInterval),
    WIIXL_SURFACE_SYMBOL("ReleasePresentInterval",      &TkDsReleasePresentInterval),
    WIIXL_SURFACE_SYMBOL("IsPresentIntervalOverridden", &TkDsIsPresentIntervalOverridden),
    WIIXL_SURFACE_SYMBOL("SetPresentIntervalRange",     &TkDsSetPresentIntervalRange),
    WIIXL_SURFACE_SYMBOL("ReleasePresentIntervalRange", &TkDsReleasePresentIntervalRange),
    WIIXL_SURFACE_SYMBOL("IsPresentIntervalRangeOverridden", &TkDsIsPresentIntervalRangeOverridden),
    WIIXL_SURFACE_SYMBOL("GetResolutionScale",          &TkDsGetResolutionScale),
    WIIXL_SURFACE_SYMBOL("SetOutputResolution",         &TkDsSetOutputResolution),
    WIIXL_SURFACE_SYMBOL("SetDockedRenderSize",         &TkDsSetDockedRenderSize),
    WIIXL_SURFACE_SYMBOL("SetVariableFrameRate",        &TkDsSetVariableFrameRate),
    WIIXL_SURFACE_SYMBOL("ReleaseVariableFrameRate",    &TkDsReleaseVariableFrameRate),
    WIIXL_SURFACE_SYMBOL("IsVariableFrameRateOn",       &TkDsIsVariableFrameRateOn),
    WIIXL_SURFACE_SYMBOL("GetGameStepUs",               &TkDsGetGameStepUs),
    WIIXL_SURFACE_SYMBOL("ReserveResolution",           &TkDsReserveResolution),
    WIIXL_SURFACE_SYMBOL("GetMaxResolutionScale",       &TkDsGetMaxResolutionScale),
    WIIXL_SURFACE_SYMBOL("GetRenderSize",               &TkDsGetRenderSize),
    WIIXL_SURFACE_SYMBOL("IsDynamicResolutionOn",       &TkDsIsDynamicResolutionOn),
    WIIXL_SURFACE_SYMBOL("SetResolutionScale",          &TkDsSetResolutionScale),
    WIIXL_SURFACE_SYMBOL("ReleaseResolutionScale",      &TkDsReleaseResolutionScale),
    WIIXL_SURFACE_SYMBOL("IsResolutionScaleOverridden", &TkDsIsResolutionScaleOverridden),
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

} // namespace WiiXLaunch::TotK::Surfaces::DisplaySurface
