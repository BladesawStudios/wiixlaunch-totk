// world_test.wxlm - proves totk.world reads and sets time and weather, and
// pins down which weather number is which.
//
// Every 600 ticks: one status line - day, HH:MM, weather number and the name
// world.hpp thinks it is.
//
// Hold ZL+L to arm (the pad is captured while you do), then the D-pad:
//   Up    clock forward 3 hours - press it repeatedly: the clock must keep
//         running between presses (it froze under the first version, which
//         used the game's sticky requestSetTime)
//   Down  clock to 22:00 - the sky should go dark
//   Right force the next weather (0 Bluesky, 1 Cloudy, ... 10, then back to
//         0), logged with its name - watch the sky match the name
//   Left  ClearWeather - the forecast takes over again - then a status line

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_input__HeldButtons(void);
    extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
    extern uint32_t wiixl_import__totk_world__Supported(void);
    extern uint32_t wiixl_import__totk_world__IsPresent(void);
    extern uint32_t wiixl_import__totk_world__GetGameTimeHM(int32_t* h, int32_t* m);
    extern uint32_t wiixl_import__totk_world__SetGameTimeHM(int32_t h, int32_t m);
    extern uint32_t wiixl_import__totk_world__GetDay(int32_t* out);
    extern uint32_t wiixl_import__totk_world__GetWeather(int32_t* out);
    extern uint32_t wiixl_import__totk_world__SetWeather(int32_t type, uint32_t lockOut);
    extern uint32_t wiixl_import__totk_world__WeatherName(int32_t type, char* out, uint32_t cap);
    extern int32_t  wiixl_import__totk_world__WeatherTypeCount(void);
    extern uint32_t wiixl_import__totk_world__ClearWeather(void);
    extern uint32_t wiixl_import__totk_world__IsWeatherOverridden(void);
}

using LogFn = void (*)(const char*);
static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;
static uint32_t (*volatile g_RegTick)(void (*)()) = &wiixl_import__wiixl_core__RegisterTick;
static uint32_t (*volatile g_Held)(void) = &wiixl_import__totk_input__HeldButtons;
static uint32_t (*volatile g_Capture)(uint32_t) = &wiixl_import__totk_input__HoldInputCapture;
static uint32_t (*volatile g_Supported)(void) = &wiixl_import__totk_world__Supported;
static uint32_t (*volatile g_Present)(void) = &wiixl_import__totk_world__IsPresent;
static uint32_t (*volatile g_GetHM)(int32_t*, int32_t*) = &wiixl_import__totk_world__GetGameTimeHM;
static uint32_t (*volatile g_SetHM)(int32_t, int32_t) = &wiixl_import__totk_world__SetGameTimeHM;
static uint32_t (*volatile g_GetDay)(int32_t*) = &wiixl_import__totk_world__GetDay;
static uint32_t (*volatile g_GetWeather)(int32_t*) = &wiixl_import__totk_world__GetWeather;
static uint32_t (*volatile g_SetWeather)(int32_t, uint32_t) = &wiixl_import__totk_world__SetWeather;
static uint32_t (*volatile g_WeatherName)(int32_t, char*, uint32_t) = &wiixl_import__totk_world__WeatherName;
static int32_t (*volatile g_WeatherCount)(void) = &wiixl_import__totk_world__WeatherTypeCount;
static uint32_t (*volatile g_ClearWeather)(void) = &wiixl_import__totk_world__ClearWeather;
static uint32_t (*volatile g_Forced)(void) = &wiixl_import__totk_world__IsWeatherOverridden;

static const uint32_t kL = 1u << 6, kZL = 1u << 8;
static const uint32_t kDLeft = 1u << 12, kDUp = 1u << 13, kDRight = 1u << 14, kDDown = 1u << 15;

static volatile uint32_t g_Ticks;
static volatile uint32_t g_PrevHeld;
static volatile int32_t g_NextWeather = 0;

static char* Append(char* o, char* end, const char* t) {
    while (t && *t && o < end - 1) *o++ = *t++;
    return o;
}
static char* AppendInt(char* o, char* end, int32_t v, int width) {
    if (v < 0) { o = Append(o, end, "-"); v = -v; }
    char d[11]; int n = 0;
    do { d[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 11);
    while (n < width && n < 11) d[n++] = '0';
    while (n && o < end - 1) *o++ = d[--n];
    return o;
}
static char* AppendWeather(char* o, char* end, int32_t w) {
    char name[24];
    g_WeatherName(w, name, sizeof(name));
    o = AppendInt(o, end, w, 1);
    o = Append(o, end, " (");
    o = Append(o, end, name[0] ? name : "?");
    return Append(o, end, ")");
}

static void Status(const char* why) {
    char line[160];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "world_test: ");
    o = Append(o, end, why);
    if (!g_Present()) {
        o = Append(o, end, " - no world yet");
    } else {
        int32_t day = 0, h = 0, m = 0, w = -1;
        g_GetDay(&day);
        g_GetHM(&h, &m);
        o = Append(o, end, " - day ");
        o = AppendInt(o, end, day, 1);
        o = Append(o, end, ", ");
        o = AppendInt(o, end, h, 2);
        o = Append(o, end, ":");
        o = AppendInt(o, end, m, 2);
        o = Append(o, end, ", weather ");
        o = g_GetWeather(&w) ? AppendWeather(o, end, w) : Append(o, end, "UNREADABLE");
        o = Append(o, end, g_Forced() ? ", FORCED" : ", natural");
    }
    *o = 0;
    g_Log(line);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;
    if (n % 600 == 0) Status("status");

    const uint32_t held = g_Held();
    const uint32_t pressed = held & ~g_PrevHeld;
    g_PrevHeld = held;
    if ((held & (kZL | kL)) != (kZL | kL)) return;
    g_Capture(8);

    if (pressed & kDUp) {
        int32_t h = 0, m = 0;
        g_GetHM(&h, &m);
        g_SetHM((h + 3) % 24, m);
        Status("D-Up, +3 hours requested (lands next frame)");
    }
    if (pressed & kDDown) {
        g_SetHM(22, 0);
        Status("D-Down, 22:00 requested");
    }
    if (pressed & kDRight) {
        const int32_t w = g_NextWeather;
        g_SetWeather(w, 0);
        char line[96];
        char* end = line + sizeof(line);
        char* o = Append(line, end, "world_test: D-Right, SetWeather ");
        o = AppendWeather(o, end, w);
        o = Append(o, end, " - what does the sky do?");
        *o = 0;
        g_Log(line);
        int32_t next = w + 1;
        if (next >= g_WeatherCount()) next = 0;
        g_NextWeather = next;
    }
    if (pressed & kDLeft) {
        g_ClearWeather();
        Status("D-Left, ClearWeather");
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (!g_Log) return;
    if (!g_Supported()) {
        g_Log("world_test: totk.world NOT supported on this build");
        return;
    }
    if (g_RegTick(&WiiXLaunch_ModTick)) {
        g_Log("world_test: registered - status every 600 ticks; ZL+L + D-pad");
    }
}
