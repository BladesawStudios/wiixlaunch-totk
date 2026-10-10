#pragma once

// TotK controller input: reading the pad, hiding it from the game, and
// pressing buttons the player did not (injection, below the capture code).
//
// WHERE IT COMES FROM. TotK's pad poll (1.2.1 decompile) asks nn::hid for the
// style set of each npad it cares about and then calls the matching overload:
//
//     style = GetNpadStyleSet(id);
//     FullKey  -> GetNpadStates((NpadFullKeyState*)  buf, 16, id)
//     JoyDual  -> GetNpadStates((NpadJoyDualState*)  buf, 16, id)
//     JoyLeft  -> GetNpadStates((NpadJoyLeftState*)  buf, 16, id)
//     JoyRight -> GetNpadStates((NpadJoyRightState*) buf, 16, id)
//     Handheld -> GetNpadStates((NpadHandheldState*) buf, 16, id)  // id 0x20
//
// All five are sdk EXPORTS (confirmed by name in TotK's sdk), so they are
// hooked by symbol, not by an address in main - the same as present.hpp and
// the host's MountRom hook. Nothing here needs a row in build.hpp, and it
// works on every build. BotW's Switch input hooks main's PLT thunks at
// per-build offsets instead; that is the difference.
//
// THE HOOK RUNS AFTER THE ORIGINAL: nn::hid fills the array, then we read
// sample 0 (the newest) and, if capture is on, blank every sample before the
// game sees any of them. The game keeps all 16, so blanking only the newest
// would leave the history it derives edges from still holding the press.
//
// WHICH PAD. Player 1 (npad id 0) and handheld (0x20) are read separately and
// merged: buttons OR'd, each stick taken from whichever pad tilts it further.
// A player with a Pro Controller and the console undocked is one player.
//
// CAPTURE COUNTS PRESENTED FRAMES, not polls. TotK can poll more than one pad
// per frame, so "8 polls" would be a different length of time depending on
// what is plugged in. HoldInputCapture uses totk.frame's clock, and refuses
// (returns false) if the frame source is not live - a timed capture with no
// clock would never expire. SetInputCapture, the latched form, needs no clock.
//
// THREADS: the hooks run on the game's input thread, mods read from wherever
// they are (a tick runs on the render thread). Every field is a single word,
// so a reader can see buttons from one poll and a stick from the next - never
// a torn value. Nothing stronger is promised.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/totk/graphics/present.hpp>

#include <cstdint>

#if WIIXL_SWITCH
#include <lib.hpp>

// The sdk's overloads, as main calls them. Declared here because nothing in
// exlaunch declares nn::hid. `const unsigned int&` is spelled out rather than
// uint32_t so the mangling is RKj whatever the C library calls uint32_t -
// that is the spelling the sdk exports.
namespace nn::hid {
struct NpadFullKeyState;
struct NpadHandheldState;
struct NpadJoyDualState;
struct NpadJoyLeftState;
struct NpadJoyRightState;
void GetNpadStates(NpadFullKeyState*  out, int count, const unsigned int& id);
void GetNpadStates(NpadHandheldState* out, int count, const unsigned int& id);
void GetNpadStates(NpadJoyDualState*  out, int count, const unsigned int& id);
void GetNpadStates(NpadJoyLeftState*  out, int count, const unsigned int& id);
void GetNpadStates(NpadJoyRightState* out, int count, const unsigned int& id);
} // namespace nn::hid
#endif

namespace WiiXLaunch::TotK::Input {

// Index into the button mask: MaskFor(b) == 1u << b, which is nn::hid's own
// NpadButton bit layout. Same order as botw.input's Button, so a mod's button
// constants mean the same thing on both games.
enum class Button : int32_t {
    A, B, X, Y, StickL, StickR, L, R, ZL, ZR, Plus, Minus,
    DLeft, DUp, DRight, DDown,
    StickLLeft, StickLUp, StickLRight, StickLDown,
    StickRLeft, StickRUp, StickRRight, StickRDown,
    LeftSL, LeftSR, RightSL, RightSR,
    Count
};

inline uint32_t MaskFor(int32_t button) {
    return (button >= 0 && button < static_cast<int32_t>(Button::Count))
               ? (1u << button) : 0u;
}

namespace impl {

// nn::hid's NpadCommonState - the layout all five state types share.
struct NpadState {
    int64_t  samplingNumber;
    uint64_t buttons;
    int32_t  lx, ly, rx, ry;
    uint32_t attributes;   // bit 0: IsConnected
    uint32_t reserved;
};
static_assert(sizeof(NpadState) == 0x28, "nn::hid::NpadCommonState is 0x28 bytes");

constexpr uint32_t kIdNo1      = 0x00;
constexpr uint32_t kIdHandheld = 0x20;

struct Pad {
    volatile uint32_t buttons;
    volatile float lx, ly, rx, ry;
    volatile uint32_t connected;
    // Game thread only: which injection this pad has started, and the first
    // sample it applies to (see Inject).
    uint32_t injGen;
    int64_t  injStart;
};
inline Pad g_No1{};
inline Pad g_Handheld{};

// ---- injection ------------------------------------------------------------
//
// ONE SLOT, like botw.input: the last Send replaces whatever was there.
// Written by mods (any thread), read by the pad hooks (the input thread), so
// it is published under a sequence count - odd while a write is in progress -
// and the reader copies it whole or not at all. A torn read would be the new
// buttons with the old frame count.
struct Injection {
    uint32_t buttons;
    uint32_t setLeft;  float lx, ly;
    uint32_t setRight; float rx, ry;
    uint32_t until;    // presented-frame count it lapses at; 0 = released
    uint32_t gen;      // bumped on every Send, so each pad notices a new one
};
inline Injection g_Inj{};
inline volatile uint32_t g_InjSeq = 0;
inline uint32_t g_InjGen = 0;

// A dead man's switch, as botw.input has: an "indefinite" hold lapses this many
// frames after the last Send unless it is sent again, so injected input can
// never outlive whatever was driving it. About 5 seconds at 60 fps.
constexpr uint32_t kMaxInjectionFrames = 300;
constexpr uint32_t kHoldForever = 0xFFFFFFFFu;

inline void PublishInjection(const Injection& in) {
    __atomic_fetch_add(&g_InjSeq, 1u, __ATOMIC_ACQ_REL);   // odd: writing
    Injection copy = in;
    copy.gen = ++g_InjGen;
    g_Inj = copy;
    __atomic_fetch_add(&g_InjSeq, 1u, __ATOMIC_RELEASE);   // even: done
}

// A consistent copy of the slot, or false if a write kept getting in the way
// (then this poll goes without injection - the next one picks it up).
inline bool ReadInjection(Injection& out) {
    for (int tries = 0; tries < 4; ++tries) {
        const uint32_t a = __atomic_load_n(&g_InjSeq, __ATOMIC_ACQUIRE);
        if (a & 1u) continue;
        out = g_Inj;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&g_InjSeq, __ATOMIC_RELAXED) == a) return true;
    }
    return false;
}

inline int32_t AxisToRaw(float v) {
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    return static_cast<int32_t>(v * 32767.0f);
}

// nn::hid's stick-tilt bits for one stick (Left/Up/Right/Down, in that bit
// order), recomputed from an injected value so they agree with it. The 0.5
// threshold is this module's, not a measured nn::hid constant.
inline uint64_t TiltBits(float x, float y, int firstBit) {
    uint64_t b = 0;
    if (x < -0.5f) b |= 1ull << (firstBit + 0);
    if (y >  0.5f) b |= 1ull << (firstBit + 1);
    if (x >  0.5f) b |= 1ull << (firstBit + 2);
    if (y < -0.5f) b |= 1ull << (firstBit + 3);
    return b;
}

// Applies the injection to the samples the game is about to read - but only to
// samples taken since the injection began. The game keeps a 16-sample history
// and a press is a CHANGE across it; OR-ing the button into all 16 would hand
// the game a button that was always down, with no edge to react to. Starting
// from the sample current when this pad first saw the injection makes the
// history show a real press, and the release is equally real when it lapses.
inline void Inject(Pad* pad, NpadState* s, int count) {
    Injection in;
    if (!ReadInjection(in) || in.until == 0 || Present::FrameCount() >= in.until) return;

    if (pad->injGen != in.gen) {
        pad->injGen = in.gen;
        pad->injStart = s[0].samplingNumber;
    }
    for (int i = 0; i < count; ++i) {
        if (s[i].samplingNumber < pad->injStart) continue;
        s[i].buttons |= in.buttons;
        if (in.setLeft) {
            s[i].lx = AxisToRaw(in.lx);
            s[i].ly = AxisToRaw(in.ly);
            s[i].buttons = (s[i].buttons & ~(0xFull << 16)) | TiltBits(in.lx, in.ly, 16);
        }
        if (in.setRight) {
            s[i].rx = AxisToRaw(in.rx);
            s[i].ry = AxisToRaw(in.ry);
            s[i].buttons = (s[i].buttons & ~(0xFull << 20)) | TiltBits(in.rx, in.ry, 20);
        }
    }
}

// Capture. kForever is the latched form; otherwise "captured while the frame
// count is below this".
constexpr uint32_t kForever = 0xFFFFFFFFu;
inline volatile uint32_t g_CaptureUntil = 0;

inline bool g_FirstReadLogged = false;
inline uint32_t g_HooksInstalled = 0;

inline bool Captured() {
    const uint32_t until = g_CaptureUntil;
    if (until == kForever) return true;
    return until != 0 && Present::FrameCount() < until;
}

inline void Process(void* array, int count, uint32_t id, const char* style) {
    if (!array || count <= 0) return;
    Pad* pad = id == kIdNo1 ? &g_No1 : id == kIdHandheld ? &g_Handheld : nullptr;
    if (!pad) return;

    auto* s = static_cast<NpadState*>(array);
    const bool connected = (s[0].attributes & 1u) != 0;
    pad->connected = connected ? 1u : 0u;
    if (connected) {
        pad->buttons = static_cast<uint32_t>(s[0].buttons);
        pad->lx = static_cast<float>(s[0].lx) / 32767.0f;
        pad->ly = static_cast<float>(s[0].ly) / 32767.0f;
        pad->rx = static_cast<float>(s[0].rx) / 32767.0f;
        pad->ry = static_cast<float>(s[0].ry) / 32767.0f;
        if (!g_FirstReadLogged) {
            g_FirstReadLogged = true;
            WIIXL_LOG("totk.input: first pad read - %s, npad id 0x%X", style, id);
        }
    }

    if (Captured()) {
        for (int i = 0; i < count; ++i) {
            s[i].buttons = 0;
            s[i].lx = s[i].ly = s[i].rx = s[i].ry = 0;
        }
    }

    // After capture, so a mod that has hidden the player's input can still
    // drive the game itself. The pad's own readings above stay the player's.
    if (connected) Inject(pad, s, count);
}

#if WIIXL_SWITCH
using StatesFn = void (*)(void*, int, const unsigned int&);

// One original per overload, and a callback that knows which one it is.
template <int Style>
struct Hook {
    static inline StatesFn orig = nullptr;
    static void Callback(void* array, int count, const unsigned int& id) {
        const StatesFn o = orig;
        if (o) o(array, count, id);
        static constexpr const char* kNames[] = {
            "FullKey", "Handheld", "JoyDual", "JoyLeft", "JoyRight"};
        Process(array, count, id, kNames[Style]);
    }
};

template <int Style, typename State>
inline void InstallOne(void (*target)(State*, int, const unsigned int&), const char* name) {
    void* const t = reinterpret_cast<void*>(target);
    const auto& self = exl::util::GetSelfModuleInfo().m_Total;
    const uintptr_t a = reinterpret_cast<uintptr_t>(t);
    if (a >= self.m_Start && a < self.GetEnd()) {
        WIIXL_LOG("totk.input: GetNpadStates(%s) resolved inside THIS module - not hooked",
                  name);
        return;
    }
    Hook<Style>::orig = reinterpret_cast<StatesFn>(
        exl::hook::Hook(t, reinterpret_cast<void*>(&Hook<Style>::Callback), true));
    if (!Hook<Style>::orig) {
        WIIXL_LOG("totk.input: hooking GetNpadStates(%s) returned no original", name);
        return;
    }
    ++g_HooksInstalled;
}
#endif

} // namespace impl

// Installs the five GetNpadStates hooks. Once. Logs how many took - a pad
// style whose hook did not take is a controller the module cannot see.
inline void Init() {
#if WIIXL_SWITCH
    static bool done = false;
    if (done) return;
    done = true;

    using namespace nn::hid;
    impl::InstallOne<0, NpadFullKeyState>(&GetNpadStates, "FullKey");
    impl::InstallOne<1, NpadHandheldState>(&GetNpadStates, "Handheld");
    impl::InstallOne<2, NpadJoyDualState>(&GetNpadStates, "JoyDual");
    impl::InstallOne<3, NpadJoyLeftState>(&GetNpadStates, "JoyLeft");
    impl::InstallOne<4, NpadJoyRightState>(&GetNpadStates, "JoyRight");
    WIIXL_LOG("totk.input: %u of 5 GetNpadStates overloads hooked%s",
              impl::g_HooksInstalled,
              impl::g_HooksInstalled == 5 ? "" : " - some controller styles are INVISIBLE");
#endif
}

// Has a connected pad been read since boot? The liveness half: every other
// call answers "nothing held" on a host whose hooks never fired, too.
inline bool IsLive() { return impl::g_FirstReadLogged; }

inline uint32_t HeldButtons() { return impl::g_No1.buttons | impl::g_Handheld.buttons; }

inline bool IsPressed(int32_t button) {
    const uint32_t m = MaskFor(button);
    return m != 0 && (HeldButtons() & m) != 0;
}

// Each axis pair from whichever pad tilts that stick further.
inline void GetLeftStick(float& x, float& y) {
    const impl::Pad& a = impl::g_No1;
    const impl::Pad& b = impl::g_Handheld;
    const float ma = a.lx * a.lx + a.ly * a.ly;
    const float mb = b.lx * b.lx + b.ly * b.ly;
    if (mb > ma) { x = b.lx; y = b.ly; } else { x = a.lx; y = a.ly; }
}

inline void GetRightStick(float& x, float& y) {
    const impl::Pad& a = impl::g_No1;
    const impl::Pad& b = impl::g_Handheld;
    const float ma = a.rx * a.rx + a.ry * a.ry;
    const float mb = b.rx * b.rx + b.ry * b.ry;
    if (mb > ma) { x = b.rx; y = b.ry; } else { x = a.rx; y = a.ry; }
}

// Hide the pad from the game for the next `frames` presented frames, to be
// called again every frame it should stay on. If the caller stops, input
// comes back by itself. False when there is no live frame clock to count with.
inline bool HoldInputCapture(uint32_t frames) {
    if (frames == 0) return true;
    if (!Present::IsLive()) return false;
    if (impl::g_CaptureUntil == impl::kForever) return true;
    const uint32_t until = Present::FrameCount() + frames;
    if (until > impl::g_CaptureUntil) impl::g_CaptureUntil = until;
    return true;
}

// Latched: on until turned off. Prefer HoldInputCapture - a mod that latches
// and then crashes leaves the player with no controls.
inline void SetInputCapture(bool on) { impl::g_CaptureUntil = on ? impl::kForever : 0; }

inline bool IsInputCaptured() { return impl::Captured(); }

// ---- injection ------------------------------------------------------------
//
// Buttons are OR'd onto the player's own; a stick is replaced only when its
// set flag is on, because centring a stick and leaving it alone are different
// instructions. `frames` counts presented frames; kHoldForever means "until
// Release", which the dead man's switch still ends 300 frames after the last
// Send. False when there is no live frame clock to count with.
constexpr uint32_t kHoldForever = impl::kHoldForever;

struct SendArgs {
    uint32_t buttons = 0;
    bool  setLeft = false;  float lx = 0.f, ly = 0.f;
    bool  setRight = false; float rx = 0.f, ry = 0.f;
    uint32_t frames = 1;
};

inline bool Send(const SendArgs& s) {
    if (!Present::IsLive()) return false;
    impl::Injection in{};
    in.buttons = s.buttons;
    in.setLeft = s.setLeft ? 1u : 0u;  in.lx = s.lx; in.ly = s.ly;
    in.setRight = s.setRight ? 1u : 0u; in.rx = s.rx; in.ry = s.ry;
    const uint32_t frames = s.frames == 0 ? 1u
                          : s.frames == kHoldForever ? impl::kMaxInjectionFrames
                          : s.frames;
    in.until = Present::FrameCount() + frames;
    impl::PublishInjection(in);
    return true;
}

inline bool Hold(uint32_t buttonMask, uint32_t frames) {
    SendArgs s;
    s.buttons = buttonMask;
    s.frames = frames;
    return Send(s);
}

// Ends injection now. The next poll applies nothing, so the game's sample
// history shows the release as a real edge.
inline void Release() { impl::PublishInjection(impl::Injection{}); }

inline bool IsInjecting() {
    impl::Injection in;
    return impl::ReadInjection(in) && in.until != 0 && Present::FrameCount() < in.until;
}

inline uint32_t InjectedButtons() {
    impl::Injection in;
    if (!impl::ReadInjection(in) || in.until == 0 || Present::FrameCount() >= in.until) return 0;
    return in.buttons;
}

} // namespace WiiXLaunch::TotK::Input
