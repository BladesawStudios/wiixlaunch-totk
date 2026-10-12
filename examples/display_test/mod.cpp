// display_test.wxlm - TotK set up like the user's UltraCam: 2560x1440 output,
// docked rendering at 1600x900 upscaled by the game, dynamic resolution off,
// and a variable frame rate capped at 60, for as long as the game runs.
//
// ModEntry: SetOutputResolution(2560, 1440) - display buffers, final image
// and texture pools - then SetDockedRenderSize(1600, 900), so the scene is
// drawn at 900p and the game's own super-resolution pass takes it to 1440p.
// Handheld stays 720p.
//
// Every tick (one per presented frame), from tick 600, with HoldForever:
//   SetVariableFrameRate - the game advances by the measured frame time, so
//                          45 fps runs at the right speed, not slow
//   SetResolutionScale(1.0) - dynamic resolution off, as in the UltraCam
//                          config (Main.ini: DynamicResolution = False)
// re-asserted every tick so the 300-frame dead man's switch never lapses.
//
// A status line every 600 ticks: fps shown, the step the game advanced by
// (should match the shown frame time), render size, work time.
//
// And a SPEED line every 5 real seconds - the objective test of "does the
// game slow down when the frame rate drops": frames per real second, world
// time-of-day seconds per real second, and how far Link moved per real
// second. With game speed right, the world rate stays the same at 60 fps
// and at 25, and running covers the same ground per second.
// With UltraCam loaded every Set is refused (and says so once).

#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/imports/totk_display.h>
#include <wiixlaunch/imports/wiixl_time.h>
#include <wiixlaunch/imports/totk_world.h>
#include <wiixlaunch/imports/totk_player.h>
#include <wiixlaunch/mod_runtime.h>

namespace Core { WXL_USE_wiixl_core(Log); WXL_USE_wiixl_core(RegisterTick); }
namespace Time { WXL_USE_wiixl_time(GetMonotonicTicks); WXL_USE_wiixl_time(TicksPerSecond); }
namespace World { WXL_USE_totk_world(GetSecondsOfDay); }
namespace Plr { WXL_USE_totk_player(GetPosition); }
namespace Ds {
WXL_USE_totk_display(FpsSupported);
WXL_USE_totk_display(ResolutionSupported);
WXL_USE_totk_display(IsUltraCamLoaded);
WXL_USE_totk_display(HoldForever);
WXL_USE_totk_display(GetFramePeriodUs);
WXL_USE_totk_display(GetFrameWorkUs);
WXL_USE_totk_display(SetVariableFrameRate);
WXL_USE_totk_display(IsVariableFrameRateOn);
WXL_USE_totk_display(GetGameStepUs);
WXL_USE_totk_display(SetOutputResolution);
WXL_USE_totk_display(SetDockedRenderSize);
WXL_USE_totk_display(GetRenderSize);
WXL_USE_totk_display(SetResolutionScale);
}

static const uint32_t kStart = 600;
static const uint32_t kStatusEvery = 600;

static volatile uint32_t g_Ticks;
static volatile uint32_t g_FpsRefused, g_ResRefused;

static char* Append(char* o, char* end, const char* t) {
    while (t && *t && o < end - 1) *o++ = *t++;
    return o;
}
static char* AppendInt(char* o, char* end, int32_t v) {
    if (v < 0) { o = Append(o, end, "-"); v = -v; }
    char d[11]; int n = 0;
    do { d[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n && o < end - 1) *o++ = d[--n];
    return o;
}

static void Status(const char* why) {
    char line[260];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "display_test: ");
    o = Append(o, end, why);
    const uint32_t us = Ds::GetFramePeriodUs();
    o = Append(o, end, " - shown ");
    o = AppendInt(o, end, us ? static_cast<int32_t>((1000000u + us / 2) / us) : 0);
    o = Append(o, end, " fps (");
    o = AppendInt(o, end, static_cast<int32_t>(us));
    o = Append(o, end, " us), game step ");
    o = AppendInt(o, end, static_cast<int32_t>(Ds::GetGameStepUs()));
    o = Append(o, end, Ds::IsVariableFrameRateOn() ? " us (variable)" : " us (stock)");
    o = Append(o, end, ", render ");
    uint32_t w = 0, h = 0;
    if (Ds::GetRenderSize(&w, &h)) {
        o = AppendInt(o, end, static_cast<int32_t>(w));
        o = Append(o, end, "x");
        o = AppendInt(o, end, static_cast<int32_t>(h));
    } else {
        o = Append(o, end, "unknown");
    }
    o = Append(o, end, ", work ");
    o = AppendInt(o, end, static_cast<int32_t>(Ds::GetFrameWorkUs()));
    o = Append(o, end, " us");
    *o = 0;
    Core::Log(line);
}

// ---- the speed probe ------------------------------------------------------
static uint64_t g_WinTick;          // window start, monotonic ticks
static uint32_t g_WinFrames;
static float g_WinWorld;            // seconds of day at window start (-1 = none)
static float g_WinPos[3];
static uint32_t g_WinHavePos;

static uint64_t Now() {
    uint32_t hi = 0, lo = 0;
    Time::GetMonotonicTicks(&hi, &lo);
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static float Sqrt(float x) {
    if (x <= 0.f) return 0.f;
    float r = x > 1.f ? x : 1.f;
    for (int i = 0; i < 20; ++i) r = 0.5f * (r + x / r);
    return r;
}

static void SpeedProbe() {
    const uint64_t now = Now();
    ++g_WinFrames;
    const uint32_t hz = Time::TicksPerSecond();
    if (!g_WinTick) {
        g_WinTick = now;
        g_WinFrames = 0;
        if (!World::GetSecondsOfDay(&g_WinWorld)) g_WinWorld = -1.f;
        g_WinHavePos = Plr::GetPosition(g_WinPos);
        return;
    }
    if (!hz || now - g_WinTick < static_cast<uint64_t>(hz) * 5u) return;

    const float real = static_cast<float>(now - g_WinTick) / static_cast<float>(hz);
    float world = -1.f, pos[3] = {0, 0, 0};
    const bool haveWorld = World::GetSecondsOfDay(&world) && g_WinWorld >= 0.f;
    const bool havePos = Plr::GetPosition(pos) && g_WinHavePos;

    char line[200];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "display_test: SPEED - ");
    o = AppendInt(o, end, static_cast<int32_t>(static_cast<float>(g_WinFrames) / real + 0.5f));
    o = Append(o, end, " fps over ");
    o = AppendInt(o, end, static_cast<int32_t>(real * 10.f + 0.5f));
    o = Append(o, end, "/10 s, world ");
    if (haveWorld) {
        float d = world - g_WinWorld;
        if (d < 0.f) d += 86400.f;      // midnight
        o = AppendInt(o, end, static_cast<int32_t>(d / real * 100.f + 0.5f));
        o = Append(o, end, "/100 game-s per s");
    } else {
        o = Append(o, end, "n/a");
    }
    o = Append(o, end, ", Link ");
    if (havePos) {
        const float dx = pos[0] - g_WinPos[0], dz = pos[2] - g_WinPos[2];
        o = AppendInt(o, end, static_cast<int32_t>(Sqrt(dx * dx + dz * dz) / real * 100.f + 0.5f));
        o = Append(o, end, "/100 m per s");
    } else {
        o = Append(o, end, "n/a");
    }
    *o = 0;
    Core::Log(line);

    g_WinTick = now;
    g_WinFrames = 0;
    g_WinWorld = haveWorld || World::GetSecondsOfDay(&world) ? world : -1.f;
    for (int i = 0; i < 3; ++i) g_WinPos[i] = pos[i];
    g_WinHavePos = havePos || Plr::GetPosition(g_WinPos);
}

static void Refused(volatile uint32_t& once, const char* what) {
    if (once) return;
    once = 1;
    char line[120];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "display_test: ");
    o = Append(o, end, what);
    o = Append(o, end, Ds::IsUltraCamLoaded() ? " REFUSED - UltraCam is loaded"
                                              : " REFUSED - see the totk.display line");
    *o = 0;
    Core::Log(line);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;
    if (n < kStart) return;
    const uint32_t forever = Ds::HoldForever();

    if (!Ds::SetVariableFrameRate(forever)) Refused(g_FpsRefused, "variable frame rate");
    if (!Ds::SetResolutionScale(1.0f, forever)) Refused(g_ResRefused, "resolution scale");

    SpeedProbe();

    if (n == kStart + 60) Status("variable frame rate on, 900p render into 1440p");
    else if (n % kStatusEvery == 0) Status("status");
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (!Ds::FpsSupported() && !Ds::ResolutionSupported()) {
        Core::Log("display_test: totk.display NOT supported on this build");
        return;
    }
    const bool out = Ds::SetOutputResolution(2560, 1440) != 0;
    const bool render = out && Ds::SetDockedRenderSize(1600, 900) != 0;
    Core::Log(out && render ? "display_test: output 2560x1440, docked render 1600x900"
              : out         ? "display_test: output 2560x1440, but SetDockedRenderSize REFUSED"
                            : "display_test: SetOutputResolution REFUSED - see the totk.display line");
    if (Core::RegisterTick(&WiiXLaunch_ModTick)) {
        Core::Log("display_test: registered - from tick 600 holds variable frame rate and scale 1.0");
        if (Ds::IsUltraCamLoaded()) Core::Log("display_test: UltraCam is loaded - expect the Sets to be refused");
    }
}
