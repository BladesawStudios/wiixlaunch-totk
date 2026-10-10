// input_test.wxlm - proves totk.input reads the pad and can hide it.
//
// Each frame (via wiixl.core's tick):
//   * the first time totk.input reports a live pad, says so;
//   * whenever the held-button mask changes, logs it, with the left stick -
//     capped at 100 lines so it cannot flood the log;
//   * while ZL and ZR are BOTH held, holds input capture. The check is in the
//     game, not the log: Link should stop responding to everything while the
//     two triggers are down, and get control back the moment they are let go,
//     while this mod's own log lines keep showing what is pressed.

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_input__IsLive(void);
    extern uint32_t wiixl_import__totk_input__HeldButtons(void);
    extern uint32_t wiixl_import__totk_input__MaskFor(int32_t button);
    extern uint32_t wiixl_import__totk_input__GetLeftStick(float* out2);
    extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
}

using LogFn     = void (*)(const char*);
using TickRegFn = uint32_t (*)(void (*)());
using U32Fn     = uint32_t (*)(void);
using MaskFn    = uint32_t (*)(int32_t);
using StickFn   = uint32_t (*)(float*);
using CapFn     = uint32_t (*)(uint32_t);

static LogFn     volatile g_Log      = &wiixl_import__wiixl_core__Log;
static TickRegFn volatile g_RegTick  = &wiixl_import__wiixl_core__RegisterTick;
static U32Fn     volatile g_IsLive   = &wiixl_import__totk_input__IsLive;
static U32Fn     volatile g_Held     = &wiixl_import__totk_input__HeldButtons;
static MaskFn    volatile g_MaskFor  = &wiixl_import__totk_input__MaskFor;
static StickFn   volatile g_LStick   = &wiixl_import__totk_input__GetLeftStick;
static CapFn     volatile g_Capture  = &wiixl_import__totk_input__HoldInputCapture;

// Button indices, as totk.input numbers them.
static const int32_t kZL = 8;
static const int32_t kZR = 9;

static volatile uint32_t g_LastHeld;
static volatile uint32_t g_Lines;
static volatile uint32_t g_WasLive;
static volatile uint32_t g_WasCapturing;

static char* Append(char* out, char* end, const char* text) {
    while (text && *text && out < end - 1) *out++ = *text++;
    return out;
}

static char* AppendHex(char* out, char* end, uint32_t v) {
    out = Append(out, end, "0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        if (out >= end - 1) break;
        *out++ = "0123456789ABCDEF"[(v >> shift) & 0xF];
    }
    return out;
}

// A stick axis as a signed percentage, so no float formatting is needed.
static char* AppendAxis(char* out, char* end, float f) {
    int v = static_cast<int>(f * 100.0f);
    if (v < 0) { out = Append(out, end, "-"); v = -v; }
    char d[4]; int n = 0;
    do { d[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 4);
    while (n && out < end - 1) *out++ = d[--n];
    return Append(out, end, "%");
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    LogFn log = g_Log;
    if (!log) return;

    if (!g_WasLive && g_IsLive && g_IsLive()) {
        g_WasLive = 1;
        log("input_test: totk.input is live - a connected pad has been read");
    }

    const uint32_t held = g_Held ? g_Held() : 0;
    if (held != g_LastHeld && g_Lines < 100) {
        g_LastHeld = held;
        g_Lines = g_Lines + 1;
        float stick[2] = {0.f, 0.f};
        if (g_LStick) g_LStick(stick);
        char line[128];
        char* end = line + sizeof(line);
        char* o = Append(line, end, "input_test: held ");
        o = AppendHex(o, end, held);
        o = Append(o, end, ", left stick ");
        o = AppendAxis(o, end, stick[0]);
        o = Append(o, end, " ");
        o = AppendAxis(o, end, stick[1]);
        if (g_Lines == 100) o = Append(o, end, " (quiet from here)");
        *o = 0;
        log(line);
    }

    const uint32_t both = g_MaskFor ? (g_MaskFor(kZL) | g_MaskFor(kZR)) : 0;
    const bool capturing = both != 0 && (held & both) == both;
    if (capturing) {
        const uint32_t ok = g_Capture ? g_Capture(8) : 0;
        if (!g_WasCapturing) {
            log(ok ? "input_test: ZL+ZR held - capturing input, the game should not see the pad"
                   : "input_test: ZL+ZR held but HoldInputCapture REFUSED - no live frame clock");
        }
    } else if (g_WasCapturing) {
        log("input_test: ZL+ZR released - capture lapses within 8 frames");
    }
    g_WasCapturing = capturing ? 1 : 0;
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    LogFn log = g_Log;
    if (!log) return;
    TickRegFn regTick = g_RegTick;
    if (regTick && regTick(&WiiXLaunch_ModTick)) {
        log("input_test: registered - press buttons; hold ZL+ZR to capture the pad");
    } else {
        log("input_test: RegisterTick REFUSED - nothing will be tested");
    }
}
