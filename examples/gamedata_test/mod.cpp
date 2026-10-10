// gamedata_test.wxlm - proves totk.gamedata reads and writes the save.
//
// At load: logs Hash("PlayerStatus.CurrentRupee"), which must be 0xA77921D7 -
// the value found in GameDataList - or nothing else here can be trusted.
// The first tick GameData is available: logs the rupee count.
//
// Hold ZR+R to arm (the pad is captured while you do), then the D-pad:
//   Up    AddRupees(+100)   the HUD's rupee count should rise by 100
//   Down  AddRupees(-100)
//   Right reads PlayerStatus.Life, .MaxLife, .Stamina, .MaxStamina through
//         GameData - should agree with totk.player
//   Left  reads AisyaRescue_AfterRescue (a real Bool) and NoSuchFlag_WXL (not
//         a flag) - the second MUST come back "no such flag"

#include <cstdint>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_input__HeldButtons(void);
    extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
    extern uint32_t wiixl_import__totk_gamedata__Supported(void);
    extern uint32_t wiixl_import__totk_gamedata__Available(void);
    extern uint32_t wiixl_import__totk_gamedata__Hash(const char* name);
    extern uint32_t wiixl_import__totk_gamedata__GetRupees(int32_t* out);
    extern uint32_t wiixl_import__totk_gamedata__AddRupees(int32_t delta);
    extern uint32_t wiixl_import__totk_gamedata__GetFlagS32(const char* name, int32_t* out);
    extern uint32_t wiixl_import__totk_gamedata__GetFlagF32(const char* name, float* out);
    extern uint32_t wiixl_import__totk_gamedata__GetFlagBool(const char* name, uint32_t* out);
}

using LogFn = void (*)(const char*);
static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;
static uint32_t (*volatile g_RegTick)(void (*)()) = &wiixl_import__wiixl_core__RegisterTick;
static uint32_t (*volatile g_Held)(void) = &wiixl_import__totk_input__HeldButtons;
static uint32_t (*volatile g_Capture)(uint32_t) = &wiixl_import__totk_input__HoldInputCapture;
static uint32_t (*volatile g_Supported)(void) = &wiixl_import__totk_gamedata__Supported;
static uint32_t (*volatile g_Available)(void) = &wiixl_import__totk_gamedata__Available;
static uint32_t (*volatile g_Hash)(const char*) = &wiixl_import__totk_gamedata__Hash;
static uint32_t (*volatile g_GetRupees)(int32_t*) = &wiixl_import__totk_gamedata__GetRupees;
static uint32_t (*volatile g_AddRupees)(int32_t) = &wiixl_import__totk_gamedata__AddRupees;
static uint32_t (*volatile g_GetS32)(const char*, int32_t*) = &wiixl_import__totk_gamedata__GetFlagS32;
static uint32_t (*volatile g_GetF32)(const char*, float*) = &wiixl_import__totk_gamedata__GetFlagF32;
static uint32_t (*volatile g_GetBool)(const char*, uint32_t*) = &wiixl_import__totk_gamedata__GetFlagBool;

static const uint32_t kR = 1u << 7, kZR = 1u << 9;
static const uint32_t kDLeft = 1u << 12, kDUp = 1u << 13, kDRight = 1u << 14, kDDown = 1u << 15;

static volatile uint32_t g_PrevHeld;
static volatile uint32_t g_SawAvailable;

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
static char* AppendHex(char* o, char* end, uint32_t v) {
    o = Append(o, end, "0x");
    for (int s = 28; s >= 0; s -= 4) {
        if (o >= end - 1) break;
        *o++ = "0123456789ABCDEF"[(v >> s) & 0xF];
    }
    return o;
}

// "name = value" or "name = NO SUCH FLAG", for an S32.
static char* S32Field(char* o, char* end, const char* name) {
    int32_t v = 0;
    o = Append(o, end, name);
    o = Append(o, end, "=");
    return g_GetS32(name, &v) ? AppendInt(o, end, v) : Append(o, end, "NONE");
}
static char* F32Field(char* o, char* end, const char* name) {
    float v = 0.f;
    o = Append(o, end, name);
    o = Append(o, end, "=");
    return g_GetF32(name, &v) ? AppendInt(o, end, static_cast<int32_t>(v)) : Append(o, end, "NONE");
}
static char* BoolField(char* o, char* end, const char* name) {
    uint32_t v = 0;
    o = Append(o, end, name);
    o = Append(o, end, "=");
    return g_GetBool(name, &v) ? AppendInt(o, end, static_cast<int32_t>(v))
                               : Append(o, end, "NO SUCH FLAG");
}

static void Rupees(const char* why) {
    char line[128];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "gamedata_test: ");
    o = Append(o, end, why);
    o = Append(o, end, " - rupees ");
    int32_t r = 0;
    o = g_GetRupees(&r) ? AppendInt(o, end, r) : Append(o, end, "UNREADABLE");
    *o = 0;
    g_Log(line);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    if (!g_SawAvailable && g_Available()) {
        g_SawAvailable = 1;
        Rupees("GameData is available");
    }

    const uint32_t held = g_Held();
    const uint32_t pressed = held & ~g_PrevHeld;
    g_PrevHeld = held;
    if ((held & (kZR | kR)) != (kZR | kR)) return;
    g_Capture(8);

    if (pressed & kDUp)   { g_AddRupees(100);  Rupees("D-Up, AddRupees(+100) queued; reads old value until applied"); }
    if (pressed & kDDown) { g_AddRupees(-100); Rupees("D-Down, AddRupees(-100) queued"); }
    if (pressed & kDRight) {
        char line[200];
        char* end = line + sizeof(line);
        char* o = Append(line, end, "gamedata_test: ");
        o = S32Field(o, end, "PlayerStatus.Life");      o = Append(o, end, " ");
        o = S32Field(o, end, "PlayerStatus.MaxLife");   o = Append(o, end, " ");
        o = F32Field(o, end, "PlayerStatus.Stamina");   o = Append(o, end, " ");
        o = F32Field(o, end, "PlayerStatus.MaxStamina");
        *o = 0;
        g_Log(line);
    }
    if (pressed & kDLeft) {
        char line[160];
        char* end = line + sizeof(line);
        char* o = Append(line, end, "gamedata_test: ");
        o = BoolField(o, end, "AisyaRescue_AfterRescue"); o = Append(o, end, ", ");
        o = BoolField(o, end, "NoSuchFlag_WXL");
        *o = 0;
        g_Log(line);
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (!g_Log) return;
    if (!g_Supported()) {
        g_Log("gamedata_test: totk.gamedata NOT supported on this build");
        return;
    }
    char line[128];
    char* end = line + sizeof(line);
    const uint32_t h = g_Hash("PlayerStatus.CurrentRupee");
    char* o = Append(line, end, "gamedata_test: Hash(PlayerStatus.CurrentRupee) = ");
    o = AppendHex(o, end, h);
    o = Append(o, end, h == 0xA77921D7u ? " - matches GameDataList" : " - WRONG, expected 0xA77921D7");
    *o = 0;
    g_Log(line);
    if (g_RegTick(&WiiXLaunch_ModTick)) {
        g_Log("gamedata_test: registered - hold ZR+R and press the D-pad");
    }
}
