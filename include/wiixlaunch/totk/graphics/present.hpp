#pragma once

// TotK's frame source: an inline hook on the sdk's own nvnQueuePresentTexture,
// found through nvnBootstrapLoader rather than through any address in main.
//
// Base has no concept of a frame (tick.hpp), so a game module nominates one.
// From the 1.2.1 decompile, sead::GraphicsNvn's device setup does:
//
//     init    = nvnBootstrapLoader("nvnDeviceInitialize");
//     getProc = nvnBootstrapLoader("nvnDeviceGetProcAddress");
//     nvnLoadCProcs(NULL,   getProc);   // stores pfnc_nvnQueuePresentTexture
//     ... init(device, &builder) ...
//     nvnLoadCProcs(device, getProc);   // and again, for this device
//
// nvnBootstrapLoader is an sdk EXPORT - main only imports it - so it is the
// same symbol on every TotK build. No row in build.hpp is needed.
//
// WHY AN INLINE HOOK, NOT A WRAPPED POINTER. The first version handed the game
// a wrapped getProc and a wrapped present. It hooked nvnBootstrapLoader and saw
// "nvnDeviceGetProcAddress" go past, and then never saw present (1.2.1 on
// Ryujinx, 2026-10-10, with UltraCam enabled). UltraCam's subsdk references
// the same three names and sits in the same chain. A pointer wrapper only
// works for whoever the game ends up talking to; when another subsdk is
// further out and answers for present itself, ours is never asked. Hooking
// the sdk's function body catches every caller, however many wrappers are
// stacked in front of it - the same reasoning as the host's MountRom hook.
//
// The present pointer is asked of the getProc the bootstrap returned, which
// on the 1.2.1 boot was the sdk's own (0xf3304f0, inside nnSdk). It is hooked
// only if it is ALSO inside the sdk: anything else is another module's
// wrapper, and inline-hooking someone else's code is how two mods' patches
// end up tangled. That case is refused by name and no source is live.
//
// WHICH THREAD: present runs wherever the game presents - its render thread,
// not the game-logic thread. A tick that touches game state wants a
// game-thread source, which needs base to take more than one nominated
// source. Planned, not done.
//
// WHEN: GameModule::Register runs from exl_main, before any game code, so the
// bootstrap hook is in place before sead asks for its procs. Ticks are
// registered during the Load phase; if the game presents before that, RunAll
// has nothing to call yet.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/tick.hpp>

#include <cstdint>
#include <cstring>

#if WIIXL_SWITCH
#include <lib.hpp>

// The sdk's export. Resolved by rtld at load, like nn::fs::MountRom: exlaunch
// links the subsdk --shared, so an undefined import is left for rtld to bind.
extern "C" void* nvnBootstrapLoader(const char* name);
#endif

namespace WiiXLaunch::TotK::Present {

namespace impl {

using GenericFn   = void (*)();
using BootstrapFn = GenericFn (*)(const char* name);
using GetProcFn   = GenericFn (*)(const void* device, const char* name);
using PresentFn   = void (*)(void* queue, void* window, int textureIndex);

inline BootstrapFn volatile g_OrigBootstrap = nullptr;
inline PresentFn   volatile g_OrigPresent   = nullptr;   // trampoline

// Written on the render thread, read by mods through totk.frame.
inline volatile uint32_t g_Frames = 0;

inline bool g_BootstrapHooked = false;
inline bool g_PresentHooked   = false;
inline bool g_PresentRefused  = false;

inline void PresentHook(void* queue, void* window, int textureIndex) {
    const uint32_t n = g_Frames + 1;
    g_Frames = n;
    if (n == 1) {
        WIIXL_LOG("totk: first frame presented - the tick source is live");
    }

    // Before the original, so anything a tick queues lands in this frame
    // rather than the next.
    WiiXLaunch::Tick::RunAll();

    const PresentFn orig = g_OrigPresent;
    if (orig) orig(queue, window, textureIndex);
}

#if WIIXL_SWITCH
// The module a code address belongs to, or null.
inline const exl::util::ModuleInfo* ModuleOf(const void* p) {
    return exl::util::TryGetModule(reinterpret_cast<uintptr_t>(p));
}

// Inline-hooks the sdk's present, once. `via` is for the log.
inline void HookPresentAt(GenericFn present, const char* via) {
    if (g_PresentHooked || g_PresentRefused || !present) return;

    const void* p = reinterpret_cast<const void*>(present);
    const auto* sdk = ModuleOf(reinterpret_cast<const void*>(&nvnBootstrapLoader));
    const auto* at  = ModuleOf(p);
    if (!sdk || at != sdk) {
        g_PresentRefused = true;
        WIIXL_LOG("totk: nvnQueuePresentTexture from %s is %p, which is NOT in the "
                  "sdk - another module's wrapper. Not hooking it, so there is NO "
                  "frame source and ticks will never run.", via, p);
        return;
    }

    g_OrigPresent = reinterpret_cast<PresentFn>(
        exl::hook::Hook(const_cast<void*>(p), reinterpret_cast<void*>(&PresentHook), true));
    if (!g_OrigPresent) {
        g_PresentRefused = true;
        WIIXL_LOG("totk: hooking nvnQueuePresentTexture at %p returned no original - "
                  "NOT live", p);
        return;
    }
    g_PresentHooked = true;
    WIIXL_LOG("totk: nvnQueuePresentTexture hooked in the sdk at %p (found via %s)",
              p, via);
}
#endif

inline GenericFn BootstrapHook(const char* name) {
    const BootstrapFn orig = g_OrigBootstrap;
    GenericFn fn = orig ? orig(name) : nullptr;
#if WIIXL_SWITCH
    if (!name || !fn) return fn;

    if (std::strcmp(name, "nvnDeviceGetProcAddress") == 0 && !g_PresentHooked) {
        // The same NULL-device query nvnLoadCProcs is about to make. Passed
        // back to the game untouched - nothing is wrapped.
        const auto getProc = reinterpret_cast<GetProcFn>(fn);
        HookPresentAt(getProc(nullptr, "nvnQueuePresentTexture"),
                      "nvnDeviceGetProcAddress(NULL)");
    } else if (std::strcmp(name, "nvnQueuePresentTexture") == 0) {
        // TotK does not ask the bootstrap for present directly; NVN allows it.
        HookPresentAt(fn, "nvnBootstrapLoader");
    }
#endif
    return fn;
}

} // namespace impl

// Installs the bootstrap hook and nominates present as the tick source. Once.
// False, with the reason logged, if the hook could not be installed - in which
// case NO source is nominated, so Tick::LogState reports registered ticks as
// never going to run instead of naming a source that is not there.
//
// Nominating here, before present is actually hooked, is deliberate:
// Tick::LogState runs at the load point, which is before the game sets up
// NVN. Whether the source really went live is the "hooked in the sdk" and
// "first frame presented" lines, and totk.frame's IsLive.
inline bool Init() {
#if WIIXL_SWITCH
    if (impl::g_BootstrapHooked) return true;

    void* const target = reinterpret_cast<void*>(&nvnBootstrapLoader);

    // A call stub inside this module rather than the sdk's function: hooking
    // it would only catch this module's own calls, of which there are none.
    const auto& self = exl::util::GetSelfModuleInfo().m_Total;
    const uintptr_t t = reinterpret_cast<uintptr_t>(target);
    if (t >= self.m_Start && t < self.GetEnd()) {
        WIIXL_LOG("totk: nvnBootstrapLoader resolved to %p inside THIS module - not "
                  "hooked, so TotK has NO frame source and ticks will never run",
                  target);
        return false;
    }

    impl::g_OrigBootstrap = reinterpret_cast<impl::BootstrapFn>(
        exl::hook::Hook(target, reinterpret_cast<void*>(&impl::BootstrapHook), true));
    if (!impl::g_OrigBootstrap) {
        WIIXL_LOG("totk: the nvnBootstrapLoader hook returned no original - NVN "
                  "would get nothing back, so treating this as fatal for the tick");
        return false;
    }

    impl::g_BootstrapHooked = true;
    WIIXL_LOG("totk: nvnBootstrapLoader hooked at %p", target);
    WiiXLaunch::Tick::NominateSource("totk NVN present (sdk nvnQueuePresentTexture), "
                                     "render thread, before the frame is handed over");
    return true;
#else
    return false;
#endif
}

inline uint32_t FrameCount() { return impl::g_Frames; }
inline bool IsLive() { return impl::g_Frames != 0; }

} // namespace WiiXLaunch::TotK::Present
