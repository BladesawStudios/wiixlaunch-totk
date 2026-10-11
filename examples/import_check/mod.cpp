// import_check.wxlm - the TotK import headers, used the way a mod author would.
//
// Every other TotK example declares its imports by hand. This one uses only
// the generated headers in include/wiixlaunch/imports/ - one symbol bound from
// each totk.* surface through its WXL_USE_ macro - so if a header is wrong,
// this fails to build; and if a binding is wrong, it fails to resolve on load.
// On a tick it logs one value from each surface, once, a few seconds in.

#include <wiixlaunch/imports/wiixl_core.h>
#include <wiixlaunch/imports/totk_frame.h>
#include <wiixlaunch/imports/totk_input.h>
#include <wiixlaunch/imports/totk_player.h>
#include <wiixlaunch/imports/totk_gamedata.h>
#include <wiixlaunch/imports/totk_world.h>
#include <wiixlaunch/imports/totk_camera.h>
#include <wiixlaunch/imports/totk_actor.h>
#include <wiixlaunch/mod_runtime.h>

namespace Core  { WXL_USE_wiixl_core(Log); WXL_USE_wiixl_core(RegisterTick); }
namespace Frame { WXL_USE_totk_frame(FrameCount); }
namespace Input { WXL_USE_totk_input(IsLive); }
namespace Plr   { WXL_USE_totk_player(GetLife); }
namespace Gd    { WXL_USE_totk_gamedata(GetRupees); }
namespace World { WXL_USE_totk_world(GetGameTimeHM); }
namespace Cam   { WXL_USE_totk_camera(GetPosition); }
namespace Act   { WXL_USE_totk_actor(Count); }

static volatile uint32_t g_Ticks;

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

extern "C" __attribute__((used)) void WiiXLaunch_ModTick() {
    const uint32_t n = g_Ticks + 1;
    g_Ticks = n;
    if (n != 600) return;

    int32_t rupees = -1, h = -1, m = -1;
    float cam[3] = {0, 0, 0};
    Gd::GetRupees(&rupees);
    World::GetGameTimeHM(&h, &m);
    Cam::GetPosition(cam);

    char line[200];
    char* end = line + sizeof(line);
    char* o = Append(line, end, "import_check: frames ");
    o = AppendInt(o, end, static_cast<int32_t>(Frame::FrameCount()));
    o = Append(o, end, ", pad live ");
    o = AppendInt(o, end, static_cast<int32_t>(Input::IsLive()));
    o = Append(o, end, ", life ");
    o = AppendInt(o, end, Plr::GetLife());
    o = Append(o, end, ", rupees ");
    o = AppendInt(o, end, rupees);
    o = Append(o, end, ", time ");
    o = AppendInt(o, end, h);
    o = Append(o, end, ":");
    o = AppendInt(o, end, m);
    o = Append(o, end, ", camera y ");
    o = AppendInt(o, end, static_cast<int32_t>(cam[1]));
    o = Append(o, end, ", actors ");
    o = AppendInt(o, end, static_cast<int32_t>(Act::Count()));
    *o = 0;
    Core::Log(line);
}

extern "C" __attribute__((used)) void WiiXLaunch_ModEntry() {
    if (Core::RegisterTick(&WiiXLaunch_ModTick)) {
        Core::Log("import_check: every totk.* surface bound through its generated header");
    }
}
