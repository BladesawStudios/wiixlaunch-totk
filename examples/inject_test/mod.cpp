// inject_test.wxlm - proves totk.input 1.1 can press buttons the player did not.
//
// Hold L+R to arm (the pad is captured while you do, so the game ignores the
// chord), then press the D-pad:
//   Up    Hold(X, 6)                          Link jumps once
//   Right Send(left stick forward, 120)       Link walks ~2 s, then stops
//   Down  Send(B + stick fwd, HoldForever)    Link sprints until Release - or
//                                             until the dead man's switch ends
//                                             it 300 frames later
//   Left  Release()                           whatever is running stops
//
// The log says when injection starts and how many ticks it lasted, so the
// dead man's switch is checked by a number, not by eye: Down left alone should
// report ~300.

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_input__HeldButtons(void);
    extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
    extern uint32_t wiixl_import__totk_input__Hold(uint32_t mask, uint32_t frames);
    extern uint32_t wiixl_import__totk_input__Send(uint32_t buttons,
                                                   uint32_t setL, float lx, float ly,
                                                   uint32_t setR, float rx, float ry,
                                                   uint32_t frames);
    extern uint32_t wiixl_import__totk_input__HoldForever(void);
    extern void     wiixl_import__totk_input__Release(void);
    extern uint32_t wiixl_import__totk_input__IsInjecting(void);
    extern uint32_t wiixl_import__totk_input__InjectedButtons(void);
}

using LogFn = void (*)(const char*);
static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;
static uint32_t (*volatile g_RegTick)(void (*)()) = &wiixl_import__wiixl_core__RegisterTick;
static uint32_t (*volatile g_Held)(void) = &wiixl_import__totk_input__HeldButtons;
static uint32_t (*volatile g_Capture)(uint32_t) = &wiixl_import__totk_input__HoldInputCapture;
static uint32_t (*volatile g_Hold)(uint32_t, uint32_t) = &wiixl_import__totk_input__Hold;
static uint32_t (*volatile g_Send)(uint32_t, uint32_t, float, float, uint32_t, float, float,
                                   uint32_t) = &wiixl_import__totk_input__Send;
static uint32_t (*volatile g_Forever)(void) = &wiixl_import__totk_input__HoldForever;
static void (*volatile g_Release)(void) = &wiixl_import__totk_input__Release;
static uint32_t (*volatile g_Injecting)(void) = &wiixl_import__totk_input__IsInjecting;
static uint32_t (*volatile g_Injected)(void) = &wiixl_import__totk_input__InjectedButtons;

// nn::hid button bits.
static const uint32_t kB = 1u << 1, kX = 1u << 2, kL = 1u << 6, kR = 1u << 7;
static const uint32_t kDLeft = 1u << 12, kDUp = 1u << 13, kDRight = 1u << 14, kDDown = 1u << 15;

static volatile uint32_t g_PrevHeld;
static volatile uint32_t g_WasInjecting;
static volatile uint32_t g_InjectTicks;

static char* Append(char* o, char* end, const char* t) {
    while (t && *t && o < end - 1) *o++ = *t++;
    return o;
}
static char* AppendU32(char* o, char* end, uint32_t v) {
    char d[10]; int n = 0;
    do { d[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < 10);
    while (n && o < end - 1) *o++ = d[--n];
    return o;
}
static void Say(const char* a, const char* b, uint32_t v, bool withValue) {
    char line[128];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "inject_test: ");
    o = Append(o, end, a);
    if (withValue) { o = AppendU32(o, end, v); o = Append(o, end, b); }
    *o = 0;
    g_Log(line);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    // How long injection lasted, by this mod's own count.
    const uint32_t injecting = g_Injecting();
    if (injecting) {
        if (!g_WasInjecting) {
            g_InjectTicks = 0;
            Say("injection started, button mask ", " (decimal)", g_Injected(), true);
        }
        g_InjectTicks = g_InjectTicks + 1;
    } else if (g_WasInjecting) {
        Say("injection ended after ", " ticks", g_InjectTicks, true);
    }
    g_WasInjecting = injecting;

    const uint32_t held = g_Held();
    const uint32_t pressed = held & ~g_PrevHeld;
    g_PrevHeld = held;
    if ((held & (kL | kR)) != (kL | kR)) return;

    g_Capture(8);
    if (pressed & kDUp) {
        Say(g_Hold(kX, 6) ? "D-Up: Hold(X, 6) - Link should jump once"
                          : "D-Up: Hold REFUSED", "", 0, false);
    }
    if (pressed & kDRight) {
        Say(g_Send(0, 1, 0.f, 1.f, 0, 0.f, 0.f, 120)
                ? "D-Right: left stick forward for 120 frames - Link should walk, then stop"
                : "D-Right: Send REFUSED", "", 0, false);
    }
    if (pressed & kDDown) {
        Say(g_Send(kB, 1, 0.f, 1.f, 0, 0.f, 0.f, g_Forever())
                ? "D-Down: B + stick forward, HoldForever - Link should sprint until "
                  "Release, or ~300 frames"
                : "D-Down: Send REFUSED", "", 0, false);
    }
    if (pressed & kDLeft) {
        g_Release();
        Say("D-Left: Release()", "", 0, false);
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (!g_Log) return;
    if (g_RegTick(&WiiXLaunch_ModTick)) {
        g_Log("inject_test: registered - hold L+R and press the D-pad");
    } else {
        g_Log("inject_test: RegisterTick REFUSED");
    }
}
