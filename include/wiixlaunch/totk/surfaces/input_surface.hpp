#pragma once

// totk.input v1.1 - reading the controller, hiding it from the game, and
// pressing buttons the player did not.
//
// The calls have the same names and signatures as botw.input's, and Button
// indices mean the same buttons, so a mod's input code ports between the games
// by changing the surface name. 1.1 appends injection: Hold, HoldIndefinitely,
// Send, HoldForever, Release, IsInjecting, InjectedButtons, InjectorCount and
// SupportsInjection - the same set botw.input has. Appended, so a mod built
// against 1.0 still resolves.
//
// INJECTION IS THE DANGEROUS HALF: a mod that can press buttons can do
// anything the player can, and unlike a hook nothing about it shows in the
// boot log afterwards. So every module that injects is named the first time it
// does, and InjectorCount says how many have.
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
constexpr uint16_t kVersionMinor = 1;

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

// --- injection (1.1) -------------------------------------------------------

inline char g_Injectors[kMaxNoted][kOwnerLen];
inline uint32_t g_InjectorCount = 0;

inline void NoteInjectorOnce() {
    const char* owner = WiiXLaunch::ModContext::Current();
    if (!owner || owner[0] == '\0') owner = "<host>";
    for (uint32_t i = 0; i < g_InjectorCount; ++i) {
        bool same = true;
        for (uint32_t c = 0; c < kOwnerLen; ++c) {
            if (g_Injectors[i][c] != owner[c]) { same = false; break; }
            if (owner[c] == '\0') break;
        }
        if (same) return;
    }
    if (g_InjectorCount < kMaxNoted) {
        uint32_t i = 0;
        for (; i + 1 < kOwnerLen && owner[i]; ++i) g_Injectors[g_InjectorCount][i] = owner[i];
        g_Injectors[g_InjectorCount][i] = '\0';
        ++g_InjectorCount;
    }
    WIIXL_LOG("totk.input: %s is pressing buttons the player did not%s", owner,
              g_InjectorCount > 1 ? " - more than one module injects, last Send wins" : "");
}

extern "C" inline uint32_t TkInSupportsInjection() { return 1; }

// Holds a mask for a number of frames, then releases on its own.
extern "C" inline uint32_t TkInHold(uint32_t buttonMask, uint32_t frames) {
    NoteInjectorOnce();
    return Input::Hold(buttonMask, frames) ? 1u : 0u;
}

// Its own symbol, as in botw.input: "press A for 8 frames" and "press A until
// I say stop" fail differently, and a mod choosing the second should have
// written the second. The dead man's switch still ends it 300 frames after the
// last call unless it is repeated.
extern "C" inline uint32_t TkInHoldIndefinitely(uint32_t buttonMask) {
    NoteInjectorOnce();
    return Input::Hold(buttonMask, Input::kHoldForever) ? 1u : 0u;
}

// The full form, flattened - botw.input's exact signature. No struct crosses.
extern "C" inline uint32_t TkInSend(uint32_t buttons,
                                    uint32_t setLeftStick, float leftX, float leftY,
                                    uint32_t setRightStick, float rightX, float rightY,
                                    uint32_t frames) {
    NoteInjectorOnce();
    Input::SendArgs a;
    a.buttons = buttons;
    a.setLeft = setLeftStick != 0;  a.lx = leftX;  a.ly = leftY;
    a.setRight = setRightStick != 0; a.rx = rightX; a.ry = rightY;
    a.frames = frames ? frames : 1u;
    return Input::Send(a) ? 1u : 0u;
}

extern "C" inline uint32_t TkInHoldForever() { return Input::kHoldForever; }
extern "C" inline void TkInRelease() { Input::Release(); }
extern "C" inline uint32_t TkInIsInjecting() { return Input::IsInjecting() ? 1u : 0u; }
extern "C" inline uint32_t TkInInjectedButtons() { return Input::InjectedButtons(); }
extern "C" inline uint32_t TkInInjectorCount() { return g_InjectorCount; }

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
    // 1.1
    WIIXL_SURFACE_SYMBOL("SupportsInjection", &TkInSupportsInjection),
    WIIXL_SURFACE_SYMBOL("Hold",              &TkInHold),
    WIIXL_SURFACE_SYMBOL("HoldIndefinitely",  &TkInHoldIndefinitely),
    WIIXL_SURFACE_SYMBOL("Send",              &TkInSend),
    WIIXL_SURFACE_SYMBOL("HoldForever",       &TkInHoldForever),
    WIIXL_SURFACE_SYMBOL("Release",           &TkInRelease),
    WIIXL_SURFACE_SYMBOL("IsInjecting",       &TkInIsInjecting),
    WIIXL_SURFACE_SYMBOL("InjectedButtons",   &TkInInjectedButtons),
    WIIXL_SURFACE_SYMBOL("InjectorCount",     &TkInInjectorCount),
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
