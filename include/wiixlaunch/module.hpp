#pragma once

// THE FIXED PATH A HOST LOOKS FOR - see wiixlaunch-botw's copy for the
// contract. A name for the log, and a registration call.
//
// Switch only: there is no Wii U Tears of the Kingdom, and targets/totk.json
// declares no wiiu or cemu block. A non-Switch build that reaches this header
// was pointed at the wrong module by its target, and saying so here beats a
// host that registers nothing and boots.

#include <wiixlaunch/platform.hpp>

#if !WIIXL_SWITCH
#error "wiixlaunch-totk is Switch-only - a non-Switch target lists it in \"modules\""
#endif

#include "totk/totk.hpp"
#include "totk/surfaces.hpp"

namespace WiiXLaunch::GameModule {

inline constexpr const char* kName = "totk";

inline void Register() {
    WiiXLaunch::TotK::Build::LogState();
    WiiXLaunch::TotK::Surfaces::Register();
    // The frame source. Not inside a surface's Register: it is a hook, it
    // must be in place before the game's NVN setup, and a host should have a
    // tick whether or not any mod imports totk.frame.
    WiiXLaunch::TotK::Present::Init();
    // The pad hooks, for the same reason: installed whether or not a mod
    // imports totk.input, so the boot log says whether the pad is visible.
    WiiXLaunch::TotK::Input::Init();
    // The player layer - the first that needs addresses in main, so it is
    // also the first that can be refused on a build with no row.
    WiiXLaunch::TotK::Player::Init();
    // GameData: no hooks, only the game's own accessors, checked.
    WiiXLaunch::TotK::GameData::Init();
    // The world layer: hooks the world manager's own per-frame update.
    WiiXLaunch::TotK::World::Init();
    // The camera layer: hooks LookAtCamera::doUpdateMatrix.
    WiiXLaunch::TotK::Camera::Init();
    // The actor layer: hooks BaseProcMgr::processLists, the manager's own
    // serial per-frame step - never an actor's update, which runs in parallel.
    WiiXLaunch::TotK::Actor::Init();
}

} // namespace WiiXLaunch::GameModule
