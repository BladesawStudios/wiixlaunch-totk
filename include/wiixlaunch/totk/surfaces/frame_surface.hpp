#pragma once

// totk.frame v1.0 - is the frame source running, and how far has it got.
//
// A mod registers its per-frame callback through wiixl.core's RegisterTick,
// which is game-agnostic; this surface is the TotK side of the same thing.
// IsLive is the question RegisterTick cannot answer: it succeeds whether or
// not anything will ever call the tick, and on a host whose present hook did
// not take, nothing will. FrameCount is presents seen since boot.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/totk/graphics/present.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Surfaces::FrameSurface {

constexpr const char* kName = "totk.frame";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

extern "C" inline uint32_t TkFrameCount() { return Present::FrameCount(); }
extern "C" inline uint32_t TkFrameIsLive() { return Present::IsLive() ? 1u : 0u; }

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("FrameCount", &TkFrameCount),
    WIIXL_SURFACE_SYMBOL("IsLive",     &TkFrameIsLive),
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

} // namespace WiiXLaunch::TotK::Surfaces::FrameSurface
