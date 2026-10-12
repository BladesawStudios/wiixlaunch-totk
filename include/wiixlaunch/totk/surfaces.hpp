#pragma once

// Every surface this module exports to compiled mods. A mod sees only what is
// registered here; a header with no surface may as well not exist to a .wxlm.

#include <wiixlaunch/totk/surfaces/frame_surface.hpp>
#include <wiixlaunch/totk/surfaces/input_surface.hpp>
#include <wiixlaunch/totk/surfaces/player_surface.hpp>
#include <wiixlaunch/totk/surfaces/gamedata_surface.hpp>
#include <wiixlaunch/totk/surfaces/world_surface.hpp>
#include <wiixlaunch/totk/surfaces/camera_surface.hpp>
#include <wiixlaunch/totk/surfaces/actor_surface.hpp>
#include <wiixlaunch/totk/surfaces/display_surface.hpp>

namespace WiiXLaunch::TotK::Surfaces {

inline void Register() {
    FrameSurface::Register();
    InputSurface::Register();
    PlayerSurface::Register();
    GameDataSurface::Register();
    WorldSurface::Register();
    CameraSurface::Register();
    ActorSurface::Register();
    DisplaySurface::Register();
}

} // namespace WiiXLaunch::TotK::Surfaces
