// actor_test.wxlm - proves totk.actor walks, spawns, moves and deletes actors.
//
// Every 600 ticks: one status line - live actor count and snapshot generation.
//
// Hold ZL+R to arm (the pad is captured while you do), then the D-pad:
//   Up    spawn an apple (Item_Fruit_A) 3 m in front of Link
//   Right move the nearest apple to 3 m above Link's head - it should drop
//   Down  delete the nearest apple - it should vanish
//   Left  log the 8 nearest actors, with names and distances
// Each action logs what it asked for; the game's answer for a spawn shows up
// in the host log ("totk.actor: spawn ...") and in LastSpawnError.

#include <cstdint>
#include <wiixlaunch/mod_math.h>

extern "C" {
    extern void     wiixl_import__wiixl_core__Log(const char* text);
    extern uint32_t wiixl_import__wiixl_core__RegisterTick(void (*fn)());
    extern uint32_t wiixl_import__totk_input__HeldButtons(void);
    extern uint32_t wiixl_import__totk_input__HoldInputCapture(uint32_t frames);
    extern uint32_t wiixl_import__totk_player__GetPosition(float* out3);
    extern float    wiixl_import__totk_player__GetFacing(void);
    extern uint32_t wiixl_import__totk_actor__Supported(void);
    extern uint32_t wiixl_import__totk_actor__Count(void);
    extern uint32_t wiixl_import__totk_actor__Generation(void);
    extern uint32_t wiixl_import__totk_actor__HandleAt(uint32_t i);
    extern uint32_t wiixl_import__totk_actor__GetName(uint32_t h, char* out, uint32_t cap);
    extern uint32_t wiixl_import__totk_actor__GetPosition(uint32_t h, float* out3);
    extern uint32_t wiixl_import__totk_actor__FindNearest(const char* name, float x, float y,
                                                          float z, float maxDist);
    extern uint32_t wiixl_import__totk_actor__Spawn(const char* name, float x, float y, float z);
    extern uint32_t wiixl_import__totk_actor__Delete(uint32_t h);
    extern uint32_t wiixl_import__totk_actor__Move(uint32_t h, float x, float y, float z);
    extern int32_t  wiixl_import__totk_actor__LastSpawnError(void);
    extern uint32_t wiixl_import__totk_actor__DroppedCount(void);
}

namespace MM = WiiXLaunch::ModMath;

using LogFn = void (*)(const char*);
static LogFn volatile g_Log = &wiixl_import__wiixl_core__Log;
static uint32_t (*volatile g_RegTick)(void (*)()) = &wiixl_import__wiixl_core__RegisterTick;
static uint32_t (*volatile g_Held)(void) = &wiixl_import__totk_input__HeldButtons;
static uint32_t (*volatile g_Capture)(uint32_t) = &wiixl_import__totk_input__HoldInputCapture;
static uint32_t (*volatile g_PlayerPos)(float*) = &wiixl_import__totk_player__GetPosition;
static float (*volatile g_Facing)(void) = &wiixl_import__totk_player__GetFacing;
static uint32_t (*volatile g_Supported)(void) = &wiixl_import__totk_actor__Supported;
static uint32_t (*volatile g_Count)(void) = &wiixl_import__totk_actor__Count;
static uint32_t (*volatile g_Gen)(void) = &wiixl_import__totk_actor__Generation;
static uint32_t (*volatile g_HandleAt)(uint32_t) = &wiixl_import__totk_actor__HandleAt;
static uint32_t (*volatile g_GetName)(uint32_t, char*, uint32_t) = &wiixl_import__totk_actor__GetName;
static uint32_t (*volatile g_GetPos)(uint32_t, float*) = &wiixl_import__totk_actor__GetPosition;
static uint32_t (*volatile g_Nearest)(const char*, float, float, float, float) =
    &wiixl_import__totk_actor__FindNearest;
static uint32_t (*volatile g_Spawn)(const char*, float, float, float) = &wiixl_import__totk_actor__Spawn;
static uint32_t (*volatile g_Delete)(uint32_t) = &wiixl_import__totk_actor__Delete;
static uint32_t (*volatile g_Move)(uint32_t, float, float, float) = &wiixl_import__totk_actor__Move;
static int32_t (*volatile g_SpawnErr)(void) = &wiixl_import__totk_actor__LastSpawnError;
static uint32_t (*volatile g_Dropped)(void) = &wiixl_import__totk_actor__DroppedCount;

static const char kApple[] = "Item_Fruit_A";
static const uint32_t kR = 1u << 7, kZL = 1u << 8;
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
static void Say2(const char* a, int32_t v, const char* b, int32_t w) {
    char line[160];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "actor_test: ");
    o = Append(o, end, a);
    o = AppendInt(o, end, v);
    o = Append(o, end, b);
    o = AppendInt(o, end, w);
    *o = 0;
    g_Log(line);
}

static void Nearest8(const float* me) {
    // Eight smallest squared distances, by a simple insertion into a short list.
    uint32_t hs[8] = {0};
    float ds[8];
    uint32_t n = 0;
    const uint32_t count = g_Count();
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t h = g_HandleAt(i);
        float p[3];
        if (!h || !g_GetPos(h, p)) continue;
        const float dx = p[0] - me[0], dy = p[1] - me[1], dz = p[2] - me[2];
        const float d = dx * dx + dy * dy + dz * dz;
        uint32_t at = n < 8 ? n : 8;
        while (at > 0 && ds[at - 1] > d) {
            if (at < 8) { ds[at] = ds[at - 1]; hs[at] = hs[at - 1]; }
            --at;
        }
        if (at < 8) { ds[at] = d; hs[at] = h; if (n < 8) ++n; }
    }
    for (uint32_t i = 0; i < n; ++i) {
        char name[64];
        g_GetName(hs[i], name, sizeof(name));
        char line[160];
        char* end = line + sizeof(line);
        char* o = Append(line, end, "actor_test:   ");
        o = AppendInt(o, end, static_cast<int32_t>(i + 1));
        o = Append(o, end, ". ");
        o = Append(o, end, name);
        o = Append(o, end, " at ");
        o = AppendInt(o, end, static_cast<int32_t>(MM::Sqrt(ds[i]) * 10.f));
        o = Append(o, end, " dm");
        *o = 0;
        g_Log(line);
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;
    if (n % 600 == 0) {
        Say2("status - ", static_cast<int32_t>(g_Count()), " live actors, snapshot ",
             static_cast<int32_t>(g_Gen()));
    }

    const uint32_t held = g_Held();
    const uint32_t pressed = held & ~g_PrevHeld;
    g_PrevHeld = held;
    if ((held & (kZL | kR)) != (kZL | kR)) return;
    g_Capture(8);

    float me[3] = {0, 0, 0};
    if (!g_PlayerPos(me)) {
        if (pressed & (kDLeft | kDUp | kDRight | kDDown)) g_Log("actor_test: no player");
        return;
    }

    if (pressed & kDUp) {
        const float f = g_Facing();
        const float x = me[0] + MM::Sin(f) * 3.f, z = me[2] + MM::Cos(f) * 3.f;
        const uint32_t ok = g_Spawn(kApple, x, me[1] + 1.f, z);
        Say2(ok ? "D-Up, apple spawn queued; last game answer " : "D-Up, spawn REFUSED (queue full?) ",
             g_SpawnErr(), ", dropped so far ", static_cast<int32_t>(g_Dropped()));
    }
    if (pressed & kDRight) {
        const uint32_t h = g_Nearest(kApple, me[0], me[1], me[2], 100.f);
        if (!h) {
            g_Log("actor_test: D-Right, no apple within 100 m in the snapshot");
        } else {
            const uint32_t ok = g_Move(h, me[0], me[1] + 3.f, me[2]);
            Say2(ok ? "D-Right, move queued for handle " : "D-Right, move REFUSED for handle ",
                 static_cast<int32_t>(h), ", dropped so far ", static_cast<int32_t>(g_Dropped()));
        }
    }
    if (pressed & kDDown) {
        const uint32_t h = g_Nearest(kApple, me[0], me[1], me[2], 100.f);
        if (!h) {
            g_Log("actor_test: D-Down, no apple within 100 m in the snapshot");
        } else {
            const uint32_t ok = g_Delete(h);
            Say2(ok ? "D-Down, delete queued for handle " : "D-Down, delete REFUSED for handle ",
                 static_cast<int32_t>(h), ", dropped so far ", static_cast<int32_t>(g_Dropped()));
        }
    }
    if (pressed & kDLeft) {
        Say2("D-Left - ", static_cast<int32_t>(g_Count()), " live actors, nearest 8 (snapshot ",
             static_cast<int32_t>(g_Gen()));
        Nearest8(me);
    }
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (!g_Log) return;
    if (!g_Supported()) {
        g_Log("actor_test: totk.actor NOT supported on this build");
        return;
    }
    if (g_RegTick(&WiiXLaunch_ModTick)) {
        g_Log("actor_test: registered - ZL+R + D-pad: Up spawn, Right move, Down delete, Left list");
    }
}
