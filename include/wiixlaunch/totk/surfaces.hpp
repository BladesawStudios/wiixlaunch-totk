#pragma once

// Every surface this module exports to compiled mods. A mod sees only what is
// registered here; a header with no surface may as well not exist to a .wxlm.

#include <wiixlaunch/totk/surfaces/frame_surface.hpp>
#include <wiixlaunch/totk/surfaces/input_surface.hpp>

namespace WiiXLaunch::TotK::Surfaces {

inline void Register() {
    FrameSurface::Register();
    InputSurface::Register();
}

} // namespace WiiXLaunch::TotK::Surfaces
