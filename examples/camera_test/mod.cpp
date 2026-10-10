// camera_test.wxlm - proves totk.camera reads the active camera and can move
// it: a minimal freecam.
//
// Every 300 ticks: one status line - camera position, forward vector, and
// UpdateCount (which should climb every frame).
//
// ZR+L toggles the freecam. While it is on, the pad is captured (Link stands
// still) and:
//   left stick   fly forward/back and strafe, along where the camera looks
//   right stick  turn and look up/down
//   D-pad Up/Dn  rise / sink
// The camera starts from wherever the game had it. Toggle off and it snaps
// back to Link. If this mod stopped calling Override the camera would go back
// on its own within 8 frames - that is the hold.

#include <cstdint>
#include <wiixlaunch/mod_math.h>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_input__HeldButtons(void);
    extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
    extern uint32_t wiixl_import__totk_input__GetLeftStick(float* out2);
    extern uint32_t wiixl_import__totk_input__GetRightStick(float* out2);
    extern uint32_t wiixl_import__totk_camera__Supported(void);
    extern uint32_t wiixl_import__totk_camera__IsPresent(void);
    extern uint32_t wiixl_import__totk_camera__UpdateCount(void);
    extern uint32_t wiixl_import__totk_camera__GetPosition(float* out3);
    extern uint32_t wiixl_import__totk_camera__GetForward(float* out3);
    extern uint32_t wiixl_import__totk_camera__Override(const float* pos3, const float* at3,
                                                        const float* up3, uint32_t frames);
    extern void     wiixl_import__totk_camera__Release(void);
}

namespace MM = WiiXLaunch::ModMath;

using LogFn = void (*)(const char*);
static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;
static uint32_t (*volatile g_RegTick)(void (*)()) = &wiixl_import__wiixl_core__RegisterTick;
static uint32_t (*volatile g_Held)(void) = &wiixl_import__totk_input__HeldButtons;
static uint32_t (*volatile g_Capture)(uint32_t) = &wiixl_import__totk_input__HoldInputCapture;
static uint32_t (*volatile g_LStick)(float*) = &wiixl_import__totk_input__GetLeftStick;
static uint32_t (*volatile g_RStick)(float*) = &wiixl_import__totk_input__GetRightStick;
static uint32_t (*volatile g_Supported)(void) = &wiixl_import__totk_camera__Supported;
static uint32_t (*volatile g_Present)(void) = &wiixl_import__totk_camera__IsPresent;
static uint32_t (*volatile g_Updates)(void) = &wiixl_import__totk_camera__UpdateCount;
static uint32_t (*volatile g_GetPos)(float*) = &wiixl_import__totk_camera__GetPosition;
static uint32_t (*volatile g_GetFwd)(float*) = &wiixl_import__totk_camera__GetForward;
static uint32_t (*volatile g_Override)(const float*, const float*, const float*, uint32_t) =
    &wiixl_import__totk_camera__Override;
static void (*volatile g_Release)(void) = &wiixl_import__totk_camera__Release;

static const uint32_t kL = 1u << 6, kZR = 1u << 9;
static const uint32_t kDUp = 1u << 13, kDDown = 1u << 15;

static volatile uint32_t g_Ticks;
static volatile uint32_t g_PrevHeld;
static volatile uint32_t g_On;
static float g_Pos[3];
static float g_Yaw, g_Pitch;

static const float kMove = 0.6f;     // metres per frame at full stick
static const float kTurn = 0.035f;   // radians per frame at full stick

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
static char* AppendVec(char* o, char* end, const float* v, float scale) {
    for (int i = 0; i < 3; ++i) {
        if (i) o = Append(o, end, ",");
        o = AppendInt(o, end, static_cast<int32_t>(v[i] * scale));
    }
    return o;
}

static void Status(const char* why) {
    char line[160];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "camera_test: ");
    o = Append(o, end, why);
    float p[3] = {0, 0, 0}, f[3] = {0, 0, 0};
    if (!g_Present() || !g_GetPos(p) || !g_GetFwd(f)) {
        o = Append(o, end, " - no camera yet");
    } else {
        o = Append(o, end, " - pos ");
        o = AppendVec(o, end, p, 1.f);
        o = Append(o, end, ", fwd(x1000) ");
        o = AppendVec(o, end, f, 1000.f);
        o = Append(o, end, ", updates ");
        o = AppendInt(o, end, static_cast<int32_t>(g_Updates()));
    }
    *o = 0;
    g_Log(line);
}

// atan2 for a mod with no libm: good to a few thousandths of a radian, which
// is all a starting yaw needs.
static float Atan2(float y, float x) {
    const float pi = 3.14159265f;
    if (x == 0.f && y == 0.f) return 0.f;
    const float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    const float a = (ax > ay) ? ay / ax : ax / ay;
    const float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = pi / 2 - r;
    if (x < 0) r = pi - r;
    return y < 0 ? -r : r;
}

static void Fly() {
    float ls[2] = {0, 0}, rs[2] = {0, 0};
    g_LStick(ls);
    g_RStick(rs);
    g_Yaw = g_Yaw - rs[0] * kTurn;
    g_Pitch = g_Pitch + rs[1] * kTurn;
    if (g_Pitch > 1.5f) g_Pitch = 1.5f;
    if (g_Pitch < -1.5f) g_Pitch = -1.5f;

    const float cp = MM::Cos(g_Pitch), sp = MM::Sin(g_Pitch);
    const float cy = MM::Cos(g_Yaw), sy = MM::Sin(g_Yaw);
    const float fwd[3] = {sy * cp, sp, cy * cp};
    const float right[3] = {-cy, 0.f, sy};

    for (int i = 0; i < 3; ++i) {
        g_Pos[i] += (fwd[i] * ls[1] + right[i] * ls[0]) * kMove;
    }
    const uint32_t held = g_Held();
    if (held & kDUp) g_Pos[1] += kMove;
    if (held & kDDown) g_Pos[1] -= kMove;

    const float at[3] = {g_Pos[0] + fwd[0], g_Pos[1] + fwd[1], g_Pos[2] + fwd[2]};
    const float up[3] = {0.f, 1.f, 0.f};
    g_Override(g_Pos, at, up, 8);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;
    if (n % 300 == 0) Status(g_On ? "status (freecam ON)" : "status");

    const uint32_t held = g_Held();
    const uint32_t pressed = held & ~g_PrevHeld;
    g_PrevHeld = held;

    const bool chord = (held & (kZR | kL)) == (kZR | kL);
    const bool chordNew = chord && (pressed & (kZR | kL)) != 0;
    if (chordNew) {
        if (!g_On) {
            float f[3] = {0, 0, 1};
            if (g_GetPos(g_Pos) && g_GetFwd(f)) {
                g_Yaw = Atan2(f[0], f[2]);
                g_Pitch = 0.f;   // start level
                g_On = 1;
                Status("freecam ON from");
            } else {
                g_Log("camera_test: no camera to start from yet");
            }
        } else {
            g_On = 0;
            g_Release();
            Status("freecam OFF - back to the game's camera");
        }
    }

    if (g_On) {
        g_Capture(8);
        Fly();
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (!g_Log) return;
    if (!g_Supported()) {
        g_Log("camera_test: totk.camera NOT supported on this build");
        return;
    }
    if (g_RegTick(&WiiXLaunch_ModTick)) {
        g_Log("camera_test: registered - status every 300 ticks; ZR+L toggles the freecam");
    }
}
