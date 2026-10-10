// player_test.wxlm - proves totk.player reads Link and can write him.
//
// Every 300 ticks: one status line - present, position, facing, life, stamina,
// and UpdateCount, which should climb while playing and stop while paused.
//
// While ZL+ZR are held the pad is captured (so the game ignores the D-pad),
// and each D-pad PRESS writes:
//   Up    AddLife(+4)        one heart, if life is quarter-hearts
//   Down  AddLife(-4)
//   Right SetStamina(0)      the wheel should empty and refill
//   Left  SetStamina(max)
// The check is on screen - the hearts and the wheel - and in the next line,
// which reads the result back through the snapshot.

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_input__HeldButtons(void);
    extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
    extern uint32_t wiixl_import__totk_player__Supported(void);
    extern uint32_t wiixl_import__totk_player__IsPresent(void);
    extern uint32_t wiixl_import__totk_player__UpdateCount(void);
    extern uint32_t wiixl_import__totk_player__GetPosition(float* out3);
    extern float    wiixl_import__totk_player__GetFacing(void);
    extern int32_t  wiixl_import__totk_player__GetLife(void);
    extern int32_t  wiixl_import__totk_player__GetMaxLife(void);
    extern uint32_t wiixl_import__totk_player__AddLife(int32_t delta);
    extern float    wiixl_import__totk_player__GetStamina(void);
    extern float    wiixl_import__totk_player__GetMaxStamina(void);
    extern float    wiixl_import__totk_player__GetExtraStamina(void);
    extern uint32_t wiixl_import__totk_player__SetStamina(float value);
}

using LogFn = void (*)(const char*);
using U32Fn = uint32_t (*)(void);
using I32Fn = int32_t (*)(void);
using F32Fn = float (*)(void);

static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;
static uint32_t (*volatile g_RegTick)(void (*)()) = &wiixl_import__wiixl_core__RegisterTick;
static U32Fn volatile g_Held = &wiixl_import__totk_input__HeldButtons;
static uint32_t (*volatile g_Capture)(uint32_t) = &wiixl_import__totk_input__HoldInputCapture;
static U32Fn volatile g_Supported = &wiixl_import__totk_player__Supported;
static U32Fn volatile g_Present = &wiixl_import__totk_player__IsPresent;
static U32Fn volatile g_Updates = &wiixl_import__totk_player__UpdateCount;
static uint32_t (*volatile g_Pos)(float*) = &wiixl_import__totk_player__GetPosition;
static F32Fn volatile g_Facing = &wiixl_import__totk_player__GetFacing;
static I32Fn volatile g_Life = &wiixl_import__totk_player__GetLife;
static I32Fn volatile g_MaxLife = &wiixl_import__totk_player__GetMaxLife;
static uint32_t (*volatile g_AddLife)(int32_t) = &wiixl_import__totk_player__AddLife;
static F32Fn volatile g_Stamina = &wiixl_import__totk_player__GetStamina;
static F32Fn volatile g_MaxStamina = &wiixl_import__totk_player__GetMaxStamina;
static F32Fn volatile g_Extra = &wiixl_import__totk_player__GetExtraStamina;
static uint32_t (*volatile g_SetStamina)(float) = &wiixl_import__totk_player__SetStamina;

// nn::hid button bits, as totk.input reports them.
static const uint32_t kZL = 1u << 8, kZR = 1u << 9;
static const uint32_t kDLeft = 1u << 12, kDUp = 1u << 13, kDRight = 1u << 14, kDDown = 1u << 15;

static volatile uint32_t g_Ticks;
static volatile uint32_t g_PrevHeld;

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
    LogFn log = g_Log;
    if (!log) return;
    float p[3] = {0, 0, 0};
    const uint32_t present = g_Present();
    g_Pos(p);
    char line[200];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "player_test: ");
    o = Append(o, end, why);
    o = Append(o, end, present ? " - present" : " - NO PLAYER");
    o = Append(o, end, ", pos ");
    o = AppendInt(o, end, static_cast<int32_t>(p[0]));
    o = Append(o, end, ",");
    o = AppendInt(o, end, static_cast<int32_t>(p[1]));
    o = Append(o, end, ",");
    o = AppendInt(o, end, static_cast<int32_t>(p[2]));
    o = Append(o, end, ", facing mrad ");
    o = AppendInt(o, end, static_cast<int32_t>(g_Facing() * 1000.f));
    o = Append(o, end, ", life ");
    o = AppendInt(o, end, g_Life());
    o = Append(o, end, "/");
    o = AppendInt(o, end, g_MaxLife());
    o = Append(o, end, ", stamina ");
    o = AppendInt(o, end, static_cast<int32_t>(g_Stamina()));
    o = Append(o, end, "/");
    o = AppendInt(o, end, static_cast<int32_t>(g_MaxStamina()));
    o = Append(o, end, " +");
    o = AppendInt(o, end, static_cast<int32_t>(g_Extra()));
    o = Append(o, end, ", updates ");
    o = AppendInt(o, end, static_cast<int32_t>(g_Updates()));
    *o = 0;
    log(line);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;
    if (n % 300 == 0) Status("status");

    const uint32_t held = g_Held();
    const uint32_t pressed = held & ~g_PrevHeld;
    g_PrevHeld = held;
    if ((held & (kZL | kZR)) != (kZL | kZR)) return;

    g_Capture(8);
    if (pressed & kDUp)    { g_AddLife(4);   Status("D-Up, AddLife(+4) queued"); }
    if (pressed & kDDown)  { g_AddLife(-4);  Status("D-Down, AddLife(-4) queued"); }
    if (pressed & kDRight) { g_SetStamina(0.f); Status("D-Right, SetStamina(0) queued"); }
    if (pressed & kDLeft)  { g_SetStamina(g_MaxStamina()); Status("D-Left, SetStamina(max) queued"); }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;
    if (!g_Supported()) {
        log("player_test: totk.player is NOT supported on this build - nothing to test");
        return;
    }
    if (g_RegTick(&WiiXLaunch_ModTick)) {
        log("player_test: registered - status every 300 ticks; ZL+ZR + D-pad to write");
    } else {
        log("player_test: RegisterTick REFUSED");
    }
}
