#pragma once

// totk.input v1.0 - reading the controller, and hiding it from the game.
//
// The read calls have the same names and signatures as botw.input's, and
// Button indices mean the same buttons, so a mod's input code ports between
// the games by changing the surface name. Injection (pressing buttons the
// player did not) is not here yet; it is the planned v1.1.
//
// IsLive is the call botw.input does not have. Every read answers "nothing
// held" on a host whose pad hooks never fired as well as on one where the
// player is simply not touching anything; IsLive tells those apart.
//
// Capture is ONE shared state - there is one pad and the game either sees it
// or it does not - so the rule is the ordinary one for shared state: last
// writer wins, and the log names every module that has touched it.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/game/input.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::InputSurface {

constexpr const char* kName = "totk.input";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

constexpr uint32_t kMaxNoted = 8;
constexpr uint32_t kOwnerLen = 17;
inline char g_Noted[kMaxNoted][kOwnerLen];
inline uint32_t g_NotedCount = 0;

// Names each module the first time it changes capture.
inline void NoteCaptureOwnerOnce() {
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
    if (g_NotedCount > 1) {
        WIIXL_LOG("totk.input: %s is module %u to change input capture - there is ONE "
                  "pad, last writer wins", owner, g_NotedCount);
    } else {
        WIIXL_LOG("totk.input: %s changed input capture", owner);
    }
}

extern "C" inline uint32_t TkInIsLive() { return Input::IsLive() ? 1u : 0u; }
extern "C" inline uint32_t TkInHeldButtons() { return Input::HeldButtons(); }
extern "C" inline uint32_t TkInMaskFor(int32_t button) { return Input::MaskFor(button); }
extern "C" inline uint32_t TkInIsPressed(int32_t button) {
    return Input::IsPressed(button) ? 1u : 0u;
}

// Sticks through a float[2]: x, y - botw.input's shape.
extern "C" inline uint32_t TkInGetLeftStick(float* out2) {
    if (!out2) return 0;
    float x = 0.f, y = 0.f;
    Input::GetLeftStick(x, y);
    out2[0] = x; out2[1] = y;
    return 1;
}

extern "C" inline uint32_t TkInGetRightStick(float* out2) {
    if (!out2) return 0;
    float x = 0.f, y = 0.f;
    Input::GetRightStick(x, y);
    out2[0] = x; out2[1] = y;
    return 1;
}

// 0 frames means botw.input's default of 8. Returns 0 if there is no live
// frame clock to count the frames with - see input.hpp.
extern "C" inline uint32_t TkInHoldInputCapture(uint32_t frames) {
    NoteCaptureOwnerOnce();
    return Input::HoldInputCapture(frames ? frames : 8u) ? 1u : 0u;
}

extern "C" inline uint32_t TkInSetInputCapture(uint32_t on) {
    NoteCaptureOwnerOnce();
    Input::SetInputCapture(on != 0);
    return 1;
}

extern "C" inline uint32_t TkInIsInputCaptured() {
    return Input::IsInputCaptured() ? 1u : 0u;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("IsLive",           &TkInIsLive),
    WIIXL_SURFACE_SYMBOL("HeldButtons",      &TkInHeldButtons),
    WIIXL_SURFACE_SYMBOL("MaskFor",          &TkInMaskFor),
    WIIXL_SURFACE_SYMBOL("IsPressed",        &TkInIsPressed),
    WIIXL_SURFACE_SYMBOL("GetLeftStick",     &TkInGetLeftStick),
    WIIXL_SURFACE_SYMBOL("GetRightStick",    &TkInGetRightStick),
    WIIXL_SURFACE_SYMBOL("HoldInputCapture", &TkInHoldInputCapture),
    WIIXL_SURFACE_SYMBOL("SetInputCapture",  &TkInSetInputCapture),
    WIIXL_SURFACE_SYMBOL("IsInputCaptured",  &TkInIsInputCaptured),
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

} // namespace WiiXLaunch::TotK::Surfaces::InputSurface
