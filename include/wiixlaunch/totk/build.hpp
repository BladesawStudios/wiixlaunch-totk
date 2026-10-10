#pragma once

// Which TotK builds this module has addresses for.
//
// Every offset into TotK's main is true of exactly one executable. The host
// already knows which one is running - GameVersion fingerprints a slice of
// read-only data at boot (targets/totk.json, "identity") - so this table is
// keyed by that fingerprint, one row per build, and anything that needs an
// address reads it from Current() rather than from a constant.
//
// A build with no row gets nullptr, and a feature that needs an address
// REFUSES BY NAME on it. That is the whole point of keying the table rather
// than hardcoding 1.2.1: an unenrolled build is a refused feature, not a hook
// at an address that means something else in that executable.
//
// THE ROWS ARE EMPTY FOR NOW, deliberately. The one thing this module does so
// far - the frame source in graphics/present.hpp - goes through
// nvnBootstrapLoader, an sdk export, and needs no address in main on any
// build. Fields join Row when the first surface needs one, with the 1.2.1
// value first and other builds as rows are derived from their dumps.
//
// Keyed by FINGERPRINT, not name: the name is a label totk.json gives a
// fingerprint, and only 1.2.1's is corroborated by a real boot (see the
// "//names" note there). If the identity slice in totk.json ever moves, every
// fingerprint changes and every row here stops matching - which fails closed,
// as an unenrolled build, rather than open.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/game_version.hpp>

#include <cstdint>

namespace WiiXLaunch::TotK::Build {

struct Row {
    uint32_t    fingerprint;   // GameVersion::Fingerprint() for this executable
    const char* name;          // for the log only, never for matching
    // Per-build addresses go here, as offsets from main's base.
};

inline constexpr Row kRows[] = {
    // build_id 9B4E43650501A4D4489B4BBFDB740F26AF3CF850 - the one Ryujinx
    // reported for the session it called v1.2.1.
    { 0x181D91D8u, "1.2.1" },
};

inline constexpr uint32_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

// The row for the running build, or nullptr if this module has none.
inline const Row* Current() {
    const uint32_t fp = WiiXLaunch::GameVersion::Fingerprint();
    if (fp == 0) return nullptr;
    for (uint32_t i = 0; i < kRowCount; ++i) {
        if (kRows[i].fingerprint == fp) return &kRows[i];
    }
    return nullptr;
}

// Once, at registration. Says which of three different states this is: no
// fingerprint at all, a fingerprint this module has no row for, or a row.
inline void LogState() {
    const uint32_t fp = WiiXLaunch::GameVersion::Fingerprint();
    if (fp == 0) {
        WIIXL_LOG("totk: build cannot be fingerprinted, so no address-based feature "
                  "will be enabled (%u build(s) have rows here)", kRowCount);
        return;
    }
    if (const Row* row = Current()) {
        WIIXL_LOG("totk: running build %s (0x%08X) has an address row", row->name, fp);
        return;
    }
    const char* known = WiiXLaunch::GameVersion::Name();
    WIIXL_LOG("totk: running build %s (0x%08X) has NO address row in this module - "
              "anything that needs an address in main is refused on it",
              known ? known : "<unenrolled>", fp);
}

} // namespace WiiXLaunch::TotK::Build
