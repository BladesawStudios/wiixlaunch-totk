#pragma once

// totk.camera v1.0 - the active gameplay camera: where it is, where it looks,
// and a held override that moves it.
//
// NO CAMERA POINTERS CROSS. botw.camera takes a raw camera pointer from the
// mod; this surface always means the active camera and hands out only values,
// for the reason every TotK surface does - a mod holding a game pointer reads
// freed memory the frame the game swaps cameras. GetPosition, GetLookAt and
// GetUp keep botw.camera's names; the camera argument is simply gone.
//
// Override(pos, at, up, frames) holds the view for that many presented
// frames; HoldForever() is "until Release", which still lapses 300 frames
// after the last Override call. up may be null to keep the game's.
// Every module that overrides the camera is named in the log the first time.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/game/camera.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::CameraSurface {

constexpr const char* kName = "totk.camera";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

namespace C = WiiXLaunch::TotK::Camera;

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
    WIIXL_LOG("totk.camera: %s overrides the camera%s", owner,
              g_NotedCount > 1 ? " - more than one module does, last call wins" : "");
}

extern "C" inline uint32_t TkCmSupported() { return C::Supported() ? 1u : 0u; }
extern "C" inline uint32_t TkCmIsPresent() { return C::IsPresent() ? 1u : 0u; }
extern "C" inline uint32_t TkCmUpdateCount() { return C::UpdateCount(); }
extern "C" inline uint32_t TkCmGetPosition(float* out3) { return C::GetPosition(out3) ? 1u : 0u; }
extern "C" inline uint32_t TkCmGetLookAt(float* out3) { return C::GetLookAt(out3) ? 1u : 0u; }
extern "C" inline uint32_t TkCmGetUp(float* out3) { return C::GetUp(out3) ? 1u : 0u; }
extern "C" inline uint32_t TkCmGetForward(float* out3) { return C::GetForward(out3) ? 1u : 0u; }

extern "C" inline uint32_t TkCmOverride(const float* pos3, const float* at3, const float* up3,
                                        uint32_t frames) {
    NoteWriterOnce();
    return C::Override(pos3, at3, up3, frames) ? 1u : 0u;
}
extern "C" inline uint32_t TkCmHoldForever() { return C::kHoldForever; }
extern "C" inline void TkCmRelease() { C::Release(); }
extern "C" inline uint32_t TkCmIsOverridden() { return C::IsOverridden() ? 1u : 0u; }

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Supported",    &TkCmSupported),
    WIIXL_SURFACE_SYMBOL("IsPresent",    &TkCmIsPresent),
    WIIXL_SURFACE_SYMBOL("UpdateCount",  &TkCmUpdateCount),
    WIIXL_SURFACE_SYMBOL("GetPosition",  &TkCmGetPosition),
    WIIXL_SURFACE_SYMBOL("GetLookAt",    &TkCmGetLookAt),
    WIIXL_SURFACE_SYMBOL("GetUp",        &TkCmGetUp),
    WIIXL_SURFACE_SYMBOL("GetForward",   &TkCmGetForward),
    WIIXL_SURFACE_SYMBOL("Override",     &TkCmOverride),
    WIIXL_SURFACE_SYMBOL("HoldForever",  &TkCmHoldForever),
    WIIXL_SURFACE_SYMBOL("Release",      &TkCmRelease),
    WIIXL_SURFACE_SYMBOL("IsOverridden", &TkCmIsOverridden),
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

} // namespace WiiXLaunch::TotK::Surfaces::CameraSurface
