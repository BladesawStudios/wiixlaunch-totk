// tick_test.wxlm - proves the TotK frame source is running.
//
// Registers a per-frame tick UNCONDITIONALLY and reports frames 1, 60 and 600
// through the host log, with totk.frame's own count beside its own. Two
// independent counters agreeing is the check: the mod's counts calls it
// received, totk.frame's counts presents the host saw.
//
// examples/hook_mod_a was the first choice for this and was wrong for it: it
// returns before RegisterTick when a host has no hook probe target, which no
// Switch host does, so on TotK it registered nothing and the log simply had no
// Tick: line.

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_frame__FrameCount(void);
    extern uint32_t wiixl_import__totk_frame__IsLive(void);
}

using LogFn     = void (*)(const char*);
using TickRegFn = uint32_t (*)(void (*)());
using U32Fn     = uint32_t (*)(void);

// volatile, so the optimizer cannot fold the import addresses away.
static LogFn     volatile g_Log        = &wiixl_import__wiixl_core__Log;
static TickRegFn volatile g_RegTick    = &wiixl_import__wiixl_core__RegisterTick;
static U32Fn     volatile g_FrameCount = &wiixl_import__totk_frame__FrameCount;
static U32Fn     volatile g_IsLive     = &wiixl_import__totk_frame__IsLive;

static volatile uint32_t g_Ticks;

static char* Append(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

static char* AppendU32(char* out, char* end, uint32_t v) {
    char digits[10];
    int n = 0;
    do { digits[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 10);
    while (n && out < end - 1) *out++ = digits[--n];
    return out;
}

static void Report(const char* what, uint32_t ticks) {
    LogFn log = g_Log;
    if (!log) return;
    char line[128];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "tick_test: ");
    o = Append(o, end, what);
    o = Append(o, end, " - my tick count ");
    o = AppendU32(o, end, ticks);
    o = Append(o, end, ", totk.frame FrameCount ");
    o = AppendU32(o, end, g_FrameCount ? g_FrameCount() : 0);
    o = Append(o, end, ", IsLive ");
    o = AppendU32(o, end, g_IsLive ? g_IsLive() : 0);
    *o = 0;
    log(line);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;
    if (n == 1)   Report("first tick", n);
    if (n == 60)  Report("tick 60", n);
    if (n == 600) Report("tick 600 (quiet from here)", n);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;

    TickRegFn regTick = g_RegTick;
    if (!regTick) {
        log("tick_test: wiixl.core has no RegisterTick - nothing to test");
        return;
    }
    if (regTick(&WiiXLaunch_ModTick)) {
        log("tick_test: registered a per-frame tick - expect 'first tick' once "
            "the game presents");
    } else {
        log("tick_test: RegisterTick was REFUSED - see the Tick: line above");
    }
}
