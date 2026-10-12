#pragma once

// TotK's frame rate and render resolution: what the game is doing, and held
// overrides for both.
//
// FRAME RATE is engine::framework::PresentationInfo, at Framework+0x248. Its
// update(info, framework) runs once a frame from Framework::procFrame_, picks
// a present interval (vsyncs per frame: 1 = 60 fps, 2 = 30, 3 = 20), hands it
// to nvnWindowSetPresentInterval, and procFrame_ then sets
//     sDeltaFrame = interval * 0.5
// so the game's own clock follows the interval: at 1 it steps half a frame
// per frame and runs at the right speed. The interval is info+0x14.
//
// THAT INTERVAL IS THE GAME'S INTENT, NOT WHAT IS SHOWN. UltraCam runs the
// game at 60 while PresentationInfo still says 2 - it sets the window's
// interval through NVN itself. What the player sees is Present::PeriodUs(),
// measured between presents. Framework+0x118 is a third thing: how long the
// game's last frame took to WORK (update turns it into "vsyncs needed"), seen
// at 9-15 ms on a frame Ryujinx presented every 16.7 ms. When the
// byte at info+0x24 is set, update skips its own adaptive logic and uses
// +0x14 as it stands - that is the path the game's own 30 fps lock takes
// (setPresentInterval(2) plus that byte), and it is the one the override
// takes: before the original, +0x14 = ours and the byte set; after it, the
// byte goes back to what it was. The value +0x14 had when the override began
// is put back on release, unless the game has locked it meanwhile.
//
// A FIXED INTERVAL SLOWS THE GAME when frames take longer: at interval 1 the
// clock steps half a frame per frame, so at 30 real fps it runs at half
// speed (seen: "shown 26 fps, interval 1"). The game already knows how to
// adapt. update reads the last frame's work time (Framework+0x118), turns it
// into vsyncs needed, and moves the interval between a fastest (+0x0) and a
// slowest (+0x4): up after a few frames over, back down - one step at a time,
// toward +0x18, after a cooldown, and only if no listener objects. The clock
// follows whatever it picks. Stock TotK's fastest is 2. So DYNAMIC 60 is
// SetPresentIntervalRange(1, ...): the fastest becomes 1, +0x18 is pointed at
// it so the first step down happens, and the game's own logic does the rest -
// 60 when it can, 30 (or 20) when it cannot, at the right speed either way.
// The game's 30 fps lock (+0x24 set, cutscenes and the like) still wins, as
// it should. Stock limits are put back on release. A fixed interval, if one
// is held, wins over a range.
//
// RESOLUTION is game::gfx::ResolutionMgr::calcDynamicResolution(mgr, info),
// called once a frame by ModelSceneExtension::update. It returns a u64: the
// low 32 bits are the render scale as a float (1.0 = full), bits 32..39 a
// code the caller acts on - 0 = apply this scale now, 1 = reset, 2 = nothing
// changed. The override replaces the scale and returns 0 only on the frame
// it differs from what was last applied, so the caller rebuilds its views
// once, not every frame. On release one more 0 hands the game's own scale
// back.
//
// WHAT 1.0 MEANS. ResolutionParam (Bootup pack, ModelSceneExtensionParam)
// gives each mode a BASE resolution - docked 1600x900, handheld 1280x720 -
// and a FINAL image size, 1920x1080 / 1280x720. The render targets are the
// final size; the game draws into base*scale of them, so docked 1.0 is 900p
// and 1.2 would fill the 1920x1080 targets.
//
// BUT 1.2 CRASHED (2026-10-11, loading a save): the scene's per-frame
// textures come from agl::utl::DynamicTextureAllocator, whose pools are sized
// at boot for the base resolution; at 1920x1080 an alloc came back empty and
// the game faulted inside DynamicTextureAllocator::alloc. So the cap is
// min(final/base, kPoolSafeScale) - 1.0 until those pools are enlarged. The
// per-mode part is read from ModelSceneExtension::updateResolution (hooked:
// base width/height at +0xcc4/+0xcc8, final width at +0xcc0) and applied at
// draw time, so a held scale survives a docked/handheld switch.
//
// BIGGER POOLS: ReserveResolution(w, h). The pool is the heap
// "DynamicTextureAllocatorMem" (111 MB in 1.2.1's HeapDef, a child of the
// "Graphics" module heap), and every heap size the game reads goes through
// HeapSizeDB::getHeapSize - hooked. A reservation grows that pool by the
// pixel ratio w*h / 1600*900 (docked base, where the stock size is enough)
// and grows "Graphics" by the same bytes so the child still fits. Heap sizes
// are read once, early, after romfs mounts - mods' entries run at that mount,
// before them, so ReserveResolution must be called from ModEntry and is
// refused once the sizes have been read. The pools then hold scale
// sqrt(ratio) of base; the cap is the smaller of that and the targets.
//
// A BIGGER OUTPUT: SetOutputResolution(w, h), 16:9, docked only. Three
// places hold 1080p, and each is changed before the game reads it:
//   * the display buffers - AppMain hardcodes 1920x1080 into the framework
//     (+0x9c/+0xa0), and GameFrameworkNx::initializeGraphicsSystem builds
//     the NVN window textures from it. Hooked: the size is rewritten just
//     before the original runs. AppMain calls it after mod entries.
//   * ResolutionParam (Bootup pack): docked Console_Main base 1600x900 /
//     final 1920x1080, and Console_InvalidDynamicResolution 1920x1080 /
//     1920x1080. The getters are hooked and hand back the new size for
//     those docked values; handheld's 1280x720 passes through untouched.
//     Final sizes the render targets, base is what scale 1.0 draws, so the
//     docked game renders AT the output size (dynamic resolution may still
//     drop below it).
//   * the texture pools - SetOutputResolution reserves its own size.
// Anything else in the game that assumes 1080p is not known yet.
//
// VARIABLE FRAME RATE (SetVariableFrameRate) - the step past interval
// stepping. VFRMgr::update(this, interval) is where every game delta starts:
// it stores deltaFrame = interval * 0.5 (1.0 = one 30 fps frame) and
// deltaSec = interval / 60 with one stp, and scales everything else from
// those. An inline hook on that stp measures the real time since the last
// frame, smooths it over kVfrBuffer frames, clamps it to 1/60..1/20 s, and
// stores dt*30 and dt instead - so at 47 fps the game advances 47 fps worth
// of time, not 60 or 30. The present interval is held at 1 (60 cap; the
// game's own 30 fps locks are overridden). Two systems step by a fixed amount
// the VFR does not scale and are corrected at their own instructions: cloth
// (a hard 1/30 s step - which is also wrong at a plain 60) and Ultrahand (a
// per-30fps-frame amount). These sites and the method were found by reading
// UltraCam's hook tables; the code here is our own. With VFR off every hook
// passes through and the game is stock.
//
// BOTH OVERRIDES ARE HELD, NOT LATCHED - the camera rule: they last the
// frames asked for, and "forever" lapses 300 presented frames after the last
// call, so a mod that stops calling cannot leave the game at 20 fps.
//
// ULTRACAM owns frame rate and resolution when it is loaded. Two mods writing
// one setting each frame is a flicker whose winner depends on hook order, so
// while a module named UltraCam is mapped the Set calls are refused and say
// so in the log; reads still work. If UltraCam hooks either function itself,
// its first instruction no longer matches the row and that half is OFF.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/build.hpp>
#include <wiixlaunch/totk/graphics/present.hpp>

#include <cstdint>
#include <cstring>

#if WIIXL_SWITCH
#include <lib.hpp>
#endif

namespace WiiXLaunch::TotK::Display {

constexpr uint32_t kHoldForever = 0xFFFFFFFFu;
constexpr uint32_t kMaxOverrideFrames = 300;
constexpr uint32_t kMinInterval = 1;
constexpr uint32_t kMaxInterval = 4;
constexpr float kMinScale = 0.25f;
// Stock pools hold scale 1.0 (see above); a reservation raises this.
constexpr float kPoolSafeScale = 1.0f;
// Bounds requests; the real cap is per mode and checked when applied.
constexpr float kMaxScale = 2.0f;
// Docked base resolution in 1.2.1's ResolutionParam - the size the stock
// pools are known to hold. Romfs data, not an address, so not in the row.
constexpr uint32_t kStockPoolPixels = 1600u * 900u;
constexpr uint32_t kMaxReserveW = 3840u, kMaxReserveH = 2160u;
constexpr const char* kPoolHeap = "DynamicTextureAllocatorMem";

namespace impl {

struct Snapshot {
    volatile uint32_t fpsPresent;
    volatile uint32_t interval;
    volatile uint32_t workUs;       // last frame's work time, microseconds (Framework+0x118)
    volatile uint32_t resPresent;
    volatile float scale;           // the scale rendered with this frame - ours while overridden
    volatile float gameScale;       // the scale the game's own manager chose this frame
    volatile uint32_t dynamicOn;
    volatile float baseW, baseH;    // this mode's render size at scale 1.0 (0 = not seen yet)
    volatile float maxScale;        // the cap: min(targets, pools) for this mode
};
inline Snapshot g_Snap{};

// ReserveResolution state. Written from ModEntry (one thread, before the
// heaps are sized) and read in the heap hook on the game's init thread.
inline uint32_t g_ReserveW = 0, g_ReserveH = 0;
inline float g_PoolScale = kPoolSafeScale;     // the scale the pools hold
inline volatile bool g_HeapsRead = false;      // a pool size has been handed out
inline int32_t g_Output[2] = {0, 0};            // SetOutputResolution; 0 = stock
inline int32_t g_Render[2] = {0, 0};            // SetDockedRenderSize; 0 = the output size
inline volatile bool g_GraphicsUp = false;      // initializeGraphicsSystem has run
inline int64_t g_PoolExtra = 0;                 // bytes added to the pool (and its parent)

// One slot per override, published under a sequence count like the camera's.
struct Override {
    uint32_t value;   // interval, or the scale's float bits
    uint32_t until;   // presented-frame count it lapses at; 0 = none
};
inline Override g_Fps{}, g_Res{}, g_Range{}, g_Vfr{};
inline volatile uint32_t g_FpsSeq = 0, g_ResSeq = 0, g_RangeSeq = 0, g_VfrSeq = 0;

inline const Build::DisplayLayout* g_L = nullptr;
inline bool g_FpsReady = false;
inline bool g_ResReady = false;
inline bool g_UltraCam = false;

// Game-thread state of each override; touched only inside its hook.
inline bool g_FpsHolding = false;
inline uint32_t g_FpsSaved = 0;
inline bool g_RangeHolding = false;
inline int32_t g_RangeSavedMin = 0, g_RangeSavedMax = 0;
inline bool g_RangeLogged = false;
inline bool g_ResHolding = false;
inline uint32_t g_ResApplied = 0;   // float bits last returned with code 0; 0 = none

inline bool g_FpsLogged = false, g_ResLogged = false;
inline bool g_FpsOvrLogged = false, g_ResOvrLogged = false;

template <typename T>
inline T* Ptr(void* base, uint32_t off) {
    return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off);
}

inline uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline float Float(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

inline void Publish(Override& slot, volatile uint32_t& seq, const Override& o) {
    __atomic_fetch_add(&seq, 1u, __ATOMIC_ACQ_REL);
    slot = o;
    __atomic_fetch_add(&seq, 1u, __ATOMIC_RELEASE);
}

inline bool Read(const Override& slot, volatile uint32_t& seq, Override& out) {
    for (int tries = 0; tries < 4; ++tries) {
        const uint32_t a = __atomic_load_n(&seq, __ATOMIC_ACQUIRE);
        if (a & 1u) continue;
        out = slot;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&seq, __ATOMIC_RELAXED) == a) return true;
    }
    return false;
}

inline bool Active(const Override& slot, volatile uint32_t& seq, Override& o) {
    return Read(slot, seq, o) && o.until != 0 && Present::FrameCount() < o.until;
}

inline uint32_t Until(uint32_t frames) {
    const uint32_t n = frames == 0 ? 1u
                     : frames == kHoldForever ? kMaxOverrideFrames : frames;
    return Present::FrameCount() + n;
}

#if WIIXL_SWITCH
// True if any mapped module is named UltraCam (case-insensitive). Every NSO
// is mapped by rtld before any of them runs, so asking once at boot is enough.
inline bool UltraCamMapped() {
    using exl::util::ModuleIndex;
    for (int i = static_cast<int>(ModuleIndex::Start); i < static_cast<int>(ModuleIndex::End); ++i) {
        if (!exl::util::HasModule(static_cast<ModuleIndex>(i))) continue;
        const auto name = exl::util::GetModuleInfo(static_cast<ModuleIndex>(i)).GetModuleName();
        static constexpr char kWant[] = "ultracam";
        constexpr size_t kLen = sizeof(kWant) - 1;
        for (size_t at = 0; at + kLen <= name.size(); ++at) {
            size_t c = 0;
            for (; c < kLen; ++c) {
                char ch = name[at + c];
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                if (ch != kWant[c]) break;
            }
            if (c == kLen) return true;
        }
    }
    return false;
}

// ---- variable frame rate -------------------------------------------------
constexpr uint32_t kVfrBuffer = 8;              // frames averaged into dt
constexpr float kVfrMinDt = 1.0f / 60.0f;       // 60 fps cap
constexpr float kVfrMaxDt = 1.0f / 20.0f;       // below 20 fps the game slows rather than jumps
constexpr uint64_t kTickHz = 19200000ull;

inline bool g_VfrReady = false;
inline bool g_VfrInstalled[3] = {false, false, false};
inline uint64_t g_VfrLastTick = 0;
inline float g_VfrRing[kVfrBuffer];
inline uint32_t g_VfrFill = 0, g_VfrAt = 0;
// Read by the cloth and Ultrahand hooks (other threads) - written each frame.
inline volatile float g_VfrDt = 1.0f / 30.0f;   // seconds, as applied this frame
inline volatile bool g_VfrApplied = false;      // this frame's deltas are ours
inline bool g_VfrLogged = false;

inline bool VfrActive() {
    Override o;
    return Active(g_Vfr, g_VfrSeq, o);
}

// THE SAVED FLOAT REGISTERS ARE NOT WHERE InlineFloatCtx SAYS. exlaunch's
// float trampoline (inline_asm.s) pushes the general registers (0x100 bytes)
// and THEN the float registers (0x200) below them, and passes sp - so in
// memory q0..q31 come first and x0..x30 follow at +0x200. InlineFloatCtx
// declares them the other way round, so ctx->S[0] is q8's slot. The first
// build used ctx->S: the deltas never reached the stp (the world clock still
// ran at 2 * 0.5 game-seconds per frame at 22 fps) and s8 was clobbered at the
// cloth and Ultrahand sites. These read the layout the assembly actually
// builds; the first delta-store call checks it against values it can predict.
inline float& SavedS(exl::hook::InlineFloatCtx* ctx, int n) {
    return *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(ctx) + n * 16);
}

inline bool g_VfrLayoutChecked = false;
inline bool g_VfrLayoutBad = false;

// At the stp, s0 = interval * 0.5 and s1 = interval / 60 for a whole-number
// interval 1..4 - true of the layout only if we are reading q0 and q1.
inline bool LayoutLooksRight(exl::hook::InlineFloatCtx* ctx) {
    const float f = SavedS(ctx, 0), sec = SavedS(ctx, 1);
    for (int i = 1; i <= 4; ++i) {
        const float wantF = static_cast<float>(i) * 0.5f, wantS = static_cast<float>(i) / 60.0f;
        const float dF = f - wantF, dS = sec - wantS;
        if (dF > -1e-4f && dF < 1e-4f && dS > -1e-5f && dS < 1e-5f) return true;
    }
    return false;
}

// The stp in VFRMgr::update: s0 = deltaFrame, s1 = deltaSec, about to be stored.
inline void VfrDeltaHook(exl::hook::InlineFloatCtx* ctx) {
    if (!g_VfrLayoutChecked) {
        g_VfrLayoutChecked = true;
        if (!LayoutLooksRight(ctx)) {
            g_VfrLayoutBad = true;
            WIIXL_LOG("totk.display: inline-hook register layout check FAILED (s0 %d, s1 %d x1e6) - "
                      "variable frame rate is OFF", static_cast<int>(SavedS(ctx, 0) * 1000000.f),
                      static_cast<int>(SavedS(ctx, 1) * 1000000.f));
        } else {
            WIIXL_LOG("totk.display: inline-hook register layout checked - s0 %d/1000, s1 %d/1000000",
                      static_cast<int>(SavedS(ctx, 0) * 1000.f), static_cast<int>(SavedS(ctx, 1) * 1000000.f));
        }
    }
    if (g_VfrLayoutBad) { g_VfrApplied = false; return; }
    const uint64_t now = svcGetSystemTick();
    const uint64_t last = g_VfrLastTick;
    g_VfrLastTick = now;
    if (!VfrActive() || !last) {
        g_VfrApplied = false;
        g_VfrFill = 0;
        return;
    }
    float raw = static_cast<float>(now - last) / static_cast<float>(kTickHz);
    if (raw > 0.25f) raw = kVfrMaxDt;           // a load or a pause - do not average it in
    g_VfrRing[g_VfrAt] = raw;
    g_VfrAt = (g_VfrAt + 1) % kVfrBuffer;
    if (g_VfrFill < kVfrBuffer) ++g_VfrFill;
    float sum = 0.f;
    for (uint32_t i = 0; i < g_VfrFill; ++i) sum += g_VfrRing[i];
    float dt = sum / static_cast<float>(g_VfrFill);
    if (dt < kVfrMinDt) dt = kVfrMinDt;
    if (dt > kVfrMaxDt) dt = kVfrMaxDt;
    g_VfrDt = dt;
    g_VfrApplied = true;
    SavedS(ctx, 0) = dt * 30.0f;
    SavedS(ctx, 1) = dt;
    if (!g_VfrLogged) {
        g_VfrLogged = true;
        WIIXL_LOG("totk.display: variable frame rate applied - dt %d us (deltaFrame x1000 = %d)",
                  static_cast<int>(dt * 1000000.f), static_cast<int>(dt * 30000.f));
    }
}

// Cloth: s0 holds the fixed 1/30 s step.
inline void VfrClothHook(exl::hook::InlineFloatCtx* ctx) {
    if (g_VfrApplied) SavedS(ctx, 0) = g_VfrDt;
}

// Ultrahand: s0 holds an amount meant for one 30 fps frame.
inline void VfrUltrahandHook(exl::hook::InlineFloatCtx* ctx) {
    if (g_VfrApplied) SavedS(ctx, 0) = SavedS(ctx, 0) * (g_VfrDt * 30.0f);
}

using PresUpdateFn = void (*)(void* info, void* framework);
inline PresUpdateFn g_OrigPres = nullptr;

inline void PresUpdateHook(void* info, void* framework) {
    const Build::DisplayLayout& L = *g_L;
    uint32_t* cur = Ptr<uint32_t>(info, L.presInterval);
    uint8_t* lock = Ptr<uint8_t>(info, L.presLock);

    int32_t* fastest = Ptr<int32_t>(info, L.presMin);
    int32_t* slowest = Ptr<int32_t>(info, L.presMax);
    int32_t* stepTo = Ptr<int32_t>(info, L.presStepDown);

    Override o, range;
    bool fixed = Active(g_Fps, g_FpsSeq, o);
    // Variable frame rate runs on interval 1: the game paces itself by the
    // measured time, and the 60 cap is the display's.
    if (!fixed && g_VfrReady && VfrActive()) {
        fixed = true;
        o.value = 1;
    }
    const bool ranged = !fixed && Active(g_Range, g_RangeSeq, range);
    if (ranged) {
        const int32_t lo = static_cast<int32_t>(range.value & 0xFF);
        const int32_t hi = static_cast<int32_t>((range.value >> 8) & 0xFF);
        if (!g_RangeHolding) {
            g_RangeHolding = true;
            g_RangeSavedMin = *fastest;
            g_RangeSavedMax = *slowest;
            // Point the step-down at the new fastest, or the game never takes
            // the first step from where it sits.
            *stepTo = lo;
            if (!g_RangeLogged) {
                g_RangeLogged = true;
                WIIXL_LOG("totk.display: interval range %d..%d applied (game had %d..%d), now at %u",
                          lo, hi ? hi : g_RangeSavedMax, g_RangeSavedMin, g_RangeSavedMax, *cur);
            }
        }
        *fastest = lo;
        *slowest = hi ? hi : g_RangeSavedMax;
    } else if (g_RangeHolding) {
        g_RangeHolding = false;
        *fastest = g_RangeSavedMin;
        *slowest = g_RangeSavedMax;
        if (*stepTo < g_RangeSavedMin) *stepTo = g_RangeSavedMin;
        if (static_cast<int32_t>(*cur) < g_RangeSavedMin) *cur = static_cast<uint32_t>(g_RangeSavedMin);
    }

    if (fixed) {
        if (!g_FpsHolding) {
            g_FpsHolding = true;
            g_FpsSaved = *cur;
        }
        const uint8_t was = *lock;
        *cur = o.value;
        *lock = 1;
        g_OrigPres(info, framework);
        *lock = was;
        if (!g_FpsOvrLogged) {
            g_FpsOvrLogged = true;
            WIIXL_LOG("totk.display: present interval override applied - %u (game had %u)",
                      o.value, g_FpsSaved);
        }
    } else {
        if (g_FpsHolding) {
            g_FpsHolding = false;
            if (*lock == 0) *cur = g_FpsSaved;
        }
        g_OrigPres(info, framework);
    }

    const int64_t ticks = *Ptr<int64_t>(framework, L.fwFrameTicks);
    // 19.2 MHz system tick: ticks * 10 / 192 is microseconds.
    g_Snap.workUs = ticks > 0 ? static_cast<uint32_t>(ticks * 10 / 192) : 0;
    g_Snap.interval = *cur;
    g_Snap.fpsPresent = 1;
    if (!g_FpsLogged && Present::IsLive()) {
        g_FpsLogged = true;
        WIIXL_LOG("totk.display: frame pacing seen - present interval %u, last frame worked %u us",
                  g_Snap.interval, g_Snap.workUs);
    }
}

using CalcResFn = uint64_t (*)(void* mgr, void* info);
inline CalcResFn g_OrigRes = nullptr;

inline uint64_t CalcResHook(void* mgr, void* info) {
    uint64_t r = g_OrigRes(mgr, info);
    const Build::DisplayLayout& L = *g_L;

    g_Snap.gameScale = Float(static_cast<uint32_t>(r));
    g_Snap.scale = g_Snap.gameScale;
    g_Snap.dynamicOn = (*Ptr<uint32_t>(mgr, L.resFlags) & 1u) ? 1u : 0u;
    g_Snap.resPresent = 1;
    if (!g_ResLogged && Present::IsLive()) {
        g_ResLogged = true;
        WIIXL_LOG("totk.display: render scale seen - %d%%, dynamic resolution %s",
                  static_cast<int>(g_Snap.scale * 100.f), g_Snap.dynamicOn ? "on" : "off");
    }

    const uint64_t code = (r >> 32) & 0xFFu;
    const uint64_t high = r & 0xFFFFFF0000000000ull;
    Override o;
    if (Active(g_Res, g_ResSeq, o)) {
        g_ResHolding = true;
        // The targets' cap for THIS mode; 1.0 until updateResolution is seen.
        const float cap = g_Snap.maxScale > 0.f ? g_Snap.maxScale : 1.0f;
        const uint32_t bits = Float(o.value) > cap ? Bits(cap) : o.value;
        const bool fresh = bits != g_ResApplied;
        g_ResApplied = bits;
        if (!g_ResOvrLogged) {
            g_ResOvrLogged = true;
            WIIXL_LOG("totk.display: render scale override applied - %d%% (%dx%d)",
                      static_cast<int>(Float(bits) * 100.f),
                      static_cast<int>(g_Snap.baseW * Float(bits)),
                      static_cast<int>(g_Snap.baseH * Float(bits)));
        }
        g_Snap.scale = Float(bits);
        return high | ((fresh ? 0ull : code) << 32) | bits;
    }
    if (g_ResHolding) {
        // One frame of "apply" with the game's own scale gives it back.
        g_ResHolding = false;
        g_ResApplied = 0;
        return high | static_cast<uint32_t>(r);
    }
    return r;
}

using UpdateResFn = void (*)(void* mse, uint32_t mode);
inline UpdateResFn g_OrigUpdateRes = nullptr;

inline void UpdateResHook(void* mse, uint32_t mode) {
    g_OrigUpdateRes(mse, mode);
    const Build::DisplayLayout& L = *g_L;
    const float finalW = *Ptr<float>(mse, L.mseFinalW);
    const float baseW = *Ptr<float>(mse, L.mseBaseW);
    const float baseH = *Ptr<float>(mse, L.mseBaseH);
    if (!(baseW > 0.f && baseH > 0.f && finalW >= baseW)) {
        WIIXL_LOG("totk.display: updateResolution left base %dx%d, final width %d - "
                  "not trusting it, the scale cap stays 1.0",
                  static_cast<int>(baseW), static_cast<int>(baseH), static_cast<int>(finalW));
        g_Snap.maxScale = 0.f;
        return;
    }
    g_Snap.baseW = baseW;
    g_Snap.baseH = baseH;
    const float targets = finalW / baseW;
    g_Snap.maxScale = targets < g_PoolScale ? targets : g_PoolScale;
    WIIXL_LOG("totk.display: resolution mode %u - base %dx%d, targets %d wide, scale cap %d%%",
              mode, static_cast<int>(baseW), static_cast<int>(baseH), static_cast<int>(finalW),
              static_cast<int>(g_Snap.maxScale * 100.f + 0.5f));
}

// HeapSizeDB::getHeapSize returns {s64, bool} through x8, as a type that is
// not trivially copyable would be: the user-written copy constructor makes
// this one non-trivial, so the compiler passes it through x8 too.
struct HeapSize {
    int64_t bytes;
    uint8_t found;
    HeapSize() : bytes(0), found(0) {}
    HeapSize(const HeapSize& o) : bytes(o.bytes), found(o.found) {}
};
using GetHeapSizeFn = HeapSize (*)(void* db, const char** name);
inline GetHeapSizeFn g_OrigHeapSize = nullptr;

inline bool SameName(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

// What a reservation grows. Each heap grows by Extra() of a STOCK size: the
// pool and the driver heap by their own, their parents by the child's, so
// the child still fits. The second boot at 2560x1440 faulted in the NV
// driver (NvRmGpuAddressSpaceAllocationRemap, under agl's virtual memory
// pool for the grown texture pool): the grown size was not page-aligned, and
// the driver's own working heap (NvInitializeGraphics, 16 MB, read by
// AppMain) had not grown with the GPU mappings it tracks. Both fixed here.
struct Growth {
    const char* heap;
    const char* stockOf;
};
inline constexpr Growth kGrowth[] = {
    { "DynamicTextureAllocatorMem", "DynamicTextureAllocatorMem" },
    { "Graphics",                   "DynamicTextureAllocatorMem" },
    { "NvInitializeGraphics",       "NvInitializeGraphics" },
    { "ReservedByFramework",        "NvInitializeGraphics" },
};
// Growth is rounded up to this, so every grown size stays a whole number of
// GPU pages (64 KB) - and of the 2 MB big pages, for good measure.
constexpr int64_t kGrowthAlign = 0x200000;

// stock * (ratio - 1), aligned up.
inline int64_t Extra(int64_t stock) {
    const uint64_t want = static_cast<uint64_t>(g_ReserveW) * g_ReserveH;
    if (stock <= 0 || want <= kStockPoolPixels) return 0;
    const int64_t raw = static_cast<int64_t>(
        (static_cast<unsigned __int128>(stock) * (want - kStockPoolPixels)) / kStockPoolPixels);
    return (raw + kGrowthAlign - 1) & ~(kGrowthAlign - 1);
}

inline uint32_t g_HeapLogged = 0;
constexpr uint32_t kMaxHeapLog = 160;

inline HeapSize GetHeapSizeHook(void* db, const char** name) {
    HeapSize r = g_OrigHeapSize(db, name);
    const char* n = name ? *name : nullptr;

    const Growth* g = nullptr;
    for (const Growth& c : kGrowth) {
        if (SameName(n, c.heap)) { g = &c; break; }
    }
    if (g) g_HeapsRead = true;
    // With a reservation, every lookup is logged (once each, in order) - the
    // order says which reads came before the mods could ask.
    if (g_ReserveW && n && g_HeapLogged < kMaxHeapLog) {
        ++g_HeapLogged;
        WIIXL_LOG("totk.display: heap lookup %s = %d KB%s", n,
                  r.found ? static_cast<int>(r.bytes >> 10) : -1, r.found ? "" : " (not found)");
    }
    if (!g || !g_ReserveW || !r.found || r.bytes <= 0) return r;

    int64_t stock = r.bytes;
    if (!SameName(g->heap, g->stockOf)) {
        const char* other = g->stockOf;
        const HeapSize o = g_OrigHeapSize(db, &other);
        stock = o.found ? o.bytes : 0;
    }
    const int64_t extra = Extra(stock);
    if (extra <= 0) return r;
    if (SameName(g->heap, kPoolHeap)) g_PoolExtra = extra;
    WIIXL_LOG("totk.display: heap %s %d KB -> %d KB, for %ux%u", n,
              static_cast<int>(r.bytes >> 10), static_cast<int>((r.bytes + extra) >> 10),
              g_ReserveW, g_ReserveH);
    r.bytes += extra;
    return r;
}

using InitGraphicsFn = void (*)(void* fw, void* heap, void* heap2, void* arg);
inline InitGraphicsFn g_OrigInitGraphics = nullptr;

inline void InitGraphicsHook(void* fw, void* heap, void* heap2, void* arg) {
    g_GraphicsUp = true;
    if (g_Output[0]) {
        const Build::DisplayLayout& L = *g_L;
        uint32_t* w = Ptr<uint32_t>(fw, L.fwDisplayW);
        uint32_t* h = Ptr<uint32_t>(fw, L.fwDisplayH);
        WIIXL_LOG("totk.display: display buffers %ux%u -> %dx%d", *w, *h, g_Output[0], g_Output[1]);
        *w = static_cast<uint32_t>(g_Output[0]);
        *h = static_cast<uint32_t>(g_Output[1]);
    }
    g_OrigInitGraphics(fw, heap, heap2, arg);
}

using ResGetterFn = const int32_t* (*)(void* param);
inline ResGetterFn g_OrigGetBase = nullptr, g_OrigGetFinal = nullptr;
inline bool g_BaseLogged = false, g_FinalLogged = false;

inline bool Is(const int32_t* r, int32_t w, int32_t h) { return r && r[0] == w && r[1] == h; }

// Docked bases are 1600x900 (Console_Main) and 1920x1080 (no dynamic res).
inline const int32_t* GetBaseHook(void* param) {
    const int32_t* r = g_OrigGetBase(param);
    if (!g_Output[0] || !(Is(r, 1600, 900) || Is(r, 1920, 1080))) return r;
    const int32_t* want = g_Render[0] ? g_Render : g_Output;
    if (!g_BaseLogged) {
        g_BaseLogged = true;
        WIIXL_LOG("totk.display: docked base resolution %dx%d -> %dx%d", r[0], r[1],
                  want[0], want[1]);
    }
    return want;
}
// Docked final image is 1920x1080 in both docked entries.
inline const int32_t* GetFinalHook(void* param) {
    const int32_t* r = g_OrigGetFinal(param);
    if (!g_Output[0] || !Is(r, 1920, 1080)) return r;
    if (!g_FinalLogged) {
        g_FinalLogged = true;
        WIIXL_LOG("totk.display: docked final image %dx%d -> %dx%d", r[0], r[1],
                  g_Output[0], g_Output[1]);
    }
    return g_Output;
}

inline uintptr_t Checked(const Build::Func& f, const char* name) {
    const uintptr_t a = exl::util::GetMainModuleInfo().m_Total.m_Start + f.offset;
    const uint32_t word = *reinterpret_cast<const uint32_t*>(a);
    if (word != f.insn) {
        WIIXL_LOG("totk.display: %s at main+0x%X starts 0x%08X, the row says 0x%08X - "
                  "NOT using it", name, f.offset, word, f.insn);
        return 0;
    }
    return a;
}
#endif

} // namespace impl

inline void Init() {
#if WIIXL_SWITCH
    static bool done = false;
    if (done) return;
    done = true;

    impl::g_UltraCam = impl::UltraCamMapped();
    if (impl::g_UltraCam) {
        WIIXL_LOG("totk.display: UltraCam is loaded - it owns frame rate and resolution, "
                  "so SetPresentInterval and SetResolutionScale will be refused; reads work");
    }

    const Build::Row* row = Build::Current();
    if (!row) {
        WIIXL_LOG("totk.display: no address row for this build - the display layer is OFF");
        return;
    }
    impl::g_L = &row->display;

    if (const uintptr_t a = impl::Checked(row->display.presentUpdate, "PresentationInfo::update")) {
        impl::g_OrigPres = reinterpret_cast<impl::PresUpdateFn>(exl::hook::Hook(
            reinterpret_cast<void*>(a), reinterpret_cast<void*>(&impl::PresUpdateHook), true));
        impl::g_FpsReady = impl::g_OrigPres != nullptr;
    }
    if (const uintptr_t a = impl::Checked(row->display.calcDynamicResolution,
                                          "ResolutionMgr::calcDynamicResolution")) {
        impl::g_OrigRes = reinterpret_cast<impl::CalcResFn>(exl::hook::Hook(
            reinterpret_cast<void*>(a), reinterpret_cast<void*>(&impl::CalcResHook), true));
        impl::g_ResReady = impl::g_OrigRes != nullptr;
    }
    // Only the cap above 1.0 depends on this one; without it the scale still
    // works, held to 1.0.
    if (const uintptr_t a = impl::Checked(row->display.updateResolution,
                                          "ModelSceneExtension::updateResolution")) {
        impl::g_OrigUpdateRes = reinterpret_cast<impl::UpdateResFn>(exl::hook::Hook(
            reinterpret_cast<void*>(a), reinterpret_cast<void*>(&impl::UpdateResHook), true));
    }
    if (impl::g_ResReady && !impl::g_OrigUpdateRes) {
        WIIXL_LOG("totk.display: no updateResolution hook - render scale capped at 1.0");
    }
    // The output-size hooks. Installed always, so a late SetOutputResolution
    // can be told the display buffers already exist.
    {
        const uintptr_t ig = impl::Checked(row->display.initGraphics, "GameFrameworkNx::initializeGraphicsSystem");
        const uintptr_t gb = impl::Checked(row->display.getBaseResolution, "ResolutionParam::getBaseResolution");
        const uintptr_t gf = impl::Checked(row->display.getFinalResolution, "ResolutionParam::getFinalImageResolution");
        if (ig && gb && gf) {
            impl::g_OrigInitGraphics = reinterpret_cast<impl::InitGraphicsFn>(exl::hook::Hook(
                reinterpret_cast<void*>(ig), reinterpret_cast<void*>(&impl::InitGraphicsHook), true));
            impl::g_OrigGetBase = reinterpret_cast<impl::ResGetterFn>(exl::hook::Hook(
                reinterpret_cast<void*>(gb), reinterpret_cast<void*>(&impl::GetBaseHook), true));
            impl::g_OrigGetFinal = reinterpret_cast<impl::ResGetterFn>(exl::hook::Hook(
                reinterpret_cast<void*>(gf), reinterpret_cast<void*>(&impl::GetFinalHook), true));
        } else {
            WIIXL_LOG("totk.display: output resolution is OFF - all three of its functions must check");
        }
    }
    // Variable frame rate: three inline hooks, all-or-nothing - the delta
    // without the cloth/Ultrahand fixes would make those two run fast.
    {
        const uintptr_t d = impl::Checked(row->display.vfrDeltaStore, "VFRMgr::update delta store");
        const uintptr_t c = impl::Checked(row->display.clothStep, "cloth step");
        const uintptr_t u = impl::Checked(row->display.ultrahandStep, "Ultrahand step");
        if (d && c && u && impl::g_FpsReady) {
            exl::hook::HookInline(d, &impl::VfrDeltaHook);
            exl::hook::HookInline(c, &impl::VfrClothHook);
            exl::hook::HookInline(u, &impl::VfrUltrahandHook);
            impl::g_VfrReady = true;
        } else {
            WIIXL_LOG("totk.display: variable frame rate is OFF - all three sites and the "
                      "frame-rate hook must check");
        }
    }
    // Installed even with no reservation, so it can tell a late
    // ReserveResolution that the sizes have already been read.
    if (const uintptr_t a = impl::Checked(row->display.getHeapSize, "HeapSizeDB::getHeapSize")) {
        impl::g_OrigHeapSize = reinterpret_cast<impl::GetHeapSizeFn>(exl::hook::Hook(
            reinterpret_cast<void*>(a), reinterpret_cast<void*>(&impl::GetHeapSizeHook), true));
    }
    WIIXL_LOG("totk.display: frame rate %s, variable frame rate %s, resolution %s, for %s",
              impl::g_FpsReady ? "hooked" : "OFF", impl::g_VfrReady ? "hooked" : "OFF",
              impl::g_ResReady ? "hooked" : "OFF", row->name);
#endif
}

inline bool FpsSupported() { return impl::g_FpsReady; }
inline bool ResolutionSupported() { return impl::g_ResReady; }
inline bool IsUltraCamLoaded() { return impl::g_UltraCam; }

inline bool IsFpsPresent() { return impl::g_Snap.fpsPresent != 0; }
inline uint32_t GetPresentInterval() { return IsFpsPresent() ? impl::g_Snap.interval : 0; }
inline uint32_t GetFrameWorkUs() { return IsFpsPresent() ? impl::g_Snap.workUs : 0; }
inline uint32_t GetFramePeriodUs() { return Present::PeriodUs(); }

inline bool IsResolutionPresent() { return impl::g_Snap.resPresent != 0; }
inline bool GetResolutionScale(float* out) {
    if (!out || !IsResolutionPresent()) return false;
    *out = impl::g_Snap.scale;
    return true;
}
inline bool IsDynamicResolutionOn() { return impl::g_Snap.dynamicOn != 0; }

// The most this mode can safely draw at: the targets' size, capped by what
// the texture pools hold (1.0 today). 1.0 until the mode has been seen.
inline float GetMaxResolutionScale() {
    return impl::g_Snap.maxScale > 0.f ? impl::g_Snap.maxScale : 1.0f;
}
// The size the 3D scene is drawn at this frame, in pixels.
inline bool GetRenderSize(uint32_t* w, uint32_t* h) {
    if (!w || !h || !IsResolutionPresent() || !(impl::g_Snap.baseW > 0.f)) return false;
    *w = static_cast<uint32_t>(impl::g_Snap.baseW * impl::g_Snap.scale + 0.5f);
    *h = static_cast<uint32_t>(impl::g_Snap.baseH * impl::g_Snap.scale + 0.5f);
    return true;
}

// Why a Set was refused, for the surface's log line; null = it was accepted.
inline const char* g_LastRefusal = nullptr;

inline bool ReserveResolution(uint32_t w, uint32_t h);

// Makes the docked game render and output at w x h (16:9, 1280x720 up to
// 3840x2160), and reserves pools for it. From ModEntry only: refused once
// the display buffers exist. Handheld keeps its 1280x720.
inline bool SetOutputResolution(uint32_t w, uint32_t h) {
    g_LastRefusal = nullptr;
    if (!impl::g_OrigInitGraphics || !impl::g_OrigGetBase || !impl::g_OrigGetFinal) {
        g_LastRefusal = "the output-resolution hooks are OFF"; return false;
    }
    if (impl::g_UltraCam) { g_LastRefusal = "UltraCam owns the resolution"; return false; }
    if (w < 1280 || h < 720 || w > kMaxReserveW || h > kMaxReserveH || w * 9 != h * 16) {
        g_LastRefusal = "the size is not 16:9 between 1280x720 and 3840x2160"; return false;
    }
    if (impl::g_GraphicsUp) {
        g_LastRefusal = "the display buffers already exist - call it from ModEntry"; return false;
    }
    if (!ReserveResolution(w, h)) return false;
    impl::g_Output[0] = static_cast<int32_t>(w);
    impl::g_Output[1] = static_cast<int32_t>(h);
    return true;
}

// Docked renders its scene at w x h instead of the output size (16:9, from
// 1280x720 up to the output) and the game's own upscaler takes it to the
// output - 900p into 1440p, as UltraCam's default does. Call after
// SetOutputResolution, from ModEntry.
inline bool SetDockedRenderSize(uint32_t w, uint32_t h) {
    g_LastRefusal = nullptr;
    if (!impl::g_Output[0]) { g_LastRefusal = "no output resolution set - call SetOutputResolution first"; return false; }
    if (impl::g_GraphicsUp) {
        g_LastRefusal = "the display buffers already exist - call it from ModEntry"; return false;
    }
    if (w < 1280 || h < 720 || w * 9 != h * 16 ||
        static_cast<int32_t>(w) > impl::g_Output[0] || static_cast<int32_t>(h) > impl::g_Output[1]) {
        g_LastRefusal = "the size is not 16:9 between 1280x720 and the output"; return false;
    }
    impl::g_Render[0] = static_cast<int32_t>(w);
    impl::g_Render[1] = static_cast<int32_t>(h);
    return true;
}

// Grows the render-texture pools so the scene can be drawn at up to w x h.
// From ModEntry only: refused once the game has read its heap sizes. The
// largest request wins. Does not change the output size by itself - it
// raises the scale cap toward what the render targets hold.
inline bool ReserveResolution(uint32_t w, uint32_t h) {
    g_LastRefusal = nullptr;
    if (!impl::g_OrigHeapSize) { g_LastRefusal = "the heap-size hook is OFF"; return false; }
    if (impl::g_UltraCam) { g_LastRefusal = "UltraCam owns the resolution"; return false; }
    if (w == 0 || h == 0 || w > kMaxReserveW || h > kMaxReserveH) {
        g_LastRefusal = "the size is outside 1x1..3840x2160"; return false;
    }
    if (impl::g_HeapsRead) {
        g_LastRefusal = "the heap sizes were already read - call it from ModEntry"; return false;
    }
    const uint64_t want = static_cast<uint64_t>(w) * h;
    if (want > static_cast<uint64_t>(impl::g_ReserveW) * impl::g_ReserveH) {
        impl::g_ReserveW = w;
        impl::g_ReserveH = h;
        const float ratio = static_cast<float>(want) / static_cast<float>(kStockPoolPixels);
        impl::g_PoolScale = ratio > 1.f ? __builtin_sqrtf(ratio) : kPoolSafeScale;
    }
    return true;
}

// Holds the present interval (1 = 60 fps, 2 = 30, 3 = 20, 4 = 15) for `frames`
// presented frames; kHoldForever = until Release, 300 frames after the last
// call at most.
inline bool SetPresentInterval(uint32_t interval, uint32_t frames) {
    g_LastRefusal = nullptr;
    if (!impl::g_FpsReady) { g_LastRefusal = "the frame-rate hook is OFF"; return false; }
    if (impl::g_UltraCam) { g_LastRefusal = "UltraCam owns the frame rate"; return false; }
    if (interval < kMinInterval || interval > kMaxInterval) {
        g_LastRefusal = "the interval is outside 1..4"; return false;
    }
    if (!Present::IsLive()) { g_LastRefusal = "there is no live frame clock yet"; return false; }
    impl::Publish(impl::g_Fps, impl::g_FpsSeq, impl::Override{interval, impl::Until(frames)});
    return true;
}
inline void ReleasePresentInterval() { impl::Publish(impl::g_Fps, impl::g_FpsSeq, impl::Override{}); }

// Lets the game pick its own interval between `fastest` and `slowest`
// (1 = 60 fps .. 4 = 15; slowest 0 = keep the game's), for `frames` presented
// frames like the other holds. Dynamic 60: (1, 0).
inline bool SetPresentIntervalRange(uint32_t fastest, uint32_t slowest, uint32_t frames) {
    g_LastRefusal = nullptr;
    if (!impl::g_FpsReady) { g_LastRefusal = "the frame-rate hook is OFF"; return false; }
    if (impl::g_UltraCam) { g_LastRefusal = "UltraCam owns the frame rate"; return false; }
    if (fastest < kMinInterval || fastest > kMaxInterval || slowest > kMaxInterval ||
        (slowest && slowest < fastest)) {
        g_LastRefusal = "the range is not 1..4 with fastest <= slowest"; return false;
    }
    if (!Present::IsLive()) { g_LastRefusal = "there is no live frame clock yet"; return false; }
    impl::Publish(impl::g_Range, impl::g_RangeSeq,
                  impl::Override{fastest | (slowest << 8), impl::Until(frames)});
    return true;
}
inline void ReleasePresentIntervalRange() {
    impl::Publish(impl::g_Range, impl::g_RangeSeq, impl::Override{});
}
inline bool IsPresentIntervalRangeOverridden() {
    impl::Override o;
    return impl::Active(impl::g_Range, impl::g_RangeSeq, o);
}
inline bool IsPresentIntervalOverridden() {
    impl::Override o;
    return impl::Active(impl::g_Fps, impl::g_FpsSeq, o);
}

// Variable frame rate for `frames` presented frames (see the top of the
// file): the game runs at whatever rate it manages, up to 60, at the right
// speed. A held fixed interval wins over it.
inline bool SetVariableFrameRate(uint32_t frames) {
    g_LastRefusal = nullptr;
    if (!impl::g_VfrReady) { g_LastRefusal = "the variable-frame-rate hooks are OFF"; return false; }
    if (impl::g_UltraCam) { g_LastRefusal = "UltraCam owns the frame rate"; return false; }
    if (!Present::IsLive()) { g_LastRefusal = "there is no live frame clock yet"; return false; }
    impl::Publish(impl::g_Vfr, impl::g_VfrSeq, impl::Override{1u, impl::Until(frames)});
    return true;
}
inline void ReleaseVariableFrameRate() { impl::Publish(impl::g_Vfr, impl::g_VfrSeq, impl::Override{}); }
inline bool IsVariableFrameRateOn() { return impl::g_VfrApplied; }
// The frame time the game advanced by last frame, in microseconds - measured
// and smoothed while variable frame rate is on, 0 otherwise.
inline uint32_t GetGameStepUs() {
    return impl::g_VfrApplied ? static_cast<uint32_t>(impl::g_VfrDt * 1000000.f) : 0u;
}

// Holds the render scale (0.25..2.0 of this mode's base size, capped per mode) the same way.
// Above GetMaxResolutionScale() it is clamped when applied, not refused, so
// a held value survives a mode switch.
inline bool SetResolutionScale(float scale, uint32_t frames) {
    g_LastRefusal = nullptr;
    if (!impl::g_ResReady) { g_LastRefusal = "the resolution hook is OFF"; return false; }
    if (impl::g_UltraCam) { g_LastRefusal = "UltraCam owns the resolution"; return false; }
    if (!(scale >= kMinScale && scale <= kMaxScale)) {
        g_LastRefusal = "the scale is outside 0.25..2.0"; return false;
    }
    if (!Present::IsLive()) { g_LastRefusal = "there is no live frame clock yet"; return false; }
    impl::Publish(impl::g_Res, impl::g_ResSeq, impl::Override{impl::Bits(scale), impl::Until(frames)});
    return true;
}
inline void ReleaseResolutionScale() { impl::Publish(impl::g_Res, impl::g_ResSeq, impl::Override{}); }
inline bool IsResolutionScaleOverridden() {
    impl::Override o;
    return impl::Active(impl::g_Res, impl::g_ResSeq, o);
}

} // namespace WiiXLaunch::TotK::Display
